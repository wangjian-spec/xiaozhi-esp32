#include "eteacher/apps/words_game/words_game.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <mutex>
#include <new>

#include <Adafruit_GFX.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"

namespace {

constexpr const char* kTag = "WordsGameEpdBench";
constexpr int64_t kDispatchTimerPeriodUs = 50 * 1000;
constexpr size_t kRefreshTotal = 25;
constexpr size_t kBenchModeCount = 2;

struct BenchMode {
	const char* name;
	bool partial_clear_before_draw;
};

constexpr std::array<BenchMode, kBenchModeCount> kBenchModes = {{
	{"clear_on", true},
	{"clear_off", false},
}};

uint32_t ToUs32(int64_t us) {
	if (us <= 0) {
		return 0;
	}
	if (us > 0xFFFFFFFFLL) {
		return 0xFFFFFFFFu;
	}
	return static_cast<uint32_t>(us);
}

uint32_t ToMs32(int64_t us) {
	return ToUs32(us) / 1000u;
}

bool IsClickLike(const ButtonEvent& event) {
	return event.action == ButtonAction::Click ||
		   event.action == ButtonAction::PressDown ||
		   event.action == ButtonAction::LongPress;
}

const char* TaskTypeName(EpdManager::TaskType type) {
	switch (type) {
		case EpdManager::TaskType::kPartial:
			return "partial";
		case EpdManager::TaskType::kFast:
			return "fast";
		case EpdManager::TaskType::kFull:
			return "full";
		default:
			return "unknown";
	}
}

class WordsGameEpdBenchApp : public AppBase {
public:
	MenuMeta GetMenuMeta() const override {
		return MenuMeta{"words_game", "单词游戏", "EPD局刷25次测速 串口报告"};
	}

	void OnEnter(AppContext& ctx) override {
		std::lock_guard<std::mutex> lock(mutex_);
		ResetStateLocked();
		epd_ = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());
		if (epd_ == nullptr) {
			ESP_LOGE(kTag, "EPD display unavailable");
			ctx.board.GetDisplay()->SetChatMessage("system", "WordsGame: EPD不可用");
			return;
		}

		epd_width_ = static_cast<int16_t>(epd_->width());
		epd_height_ = static_cast<int16_t>(epd_->height());
		session_start_us_ = esp_timer_get_time();
		saved_partial_force_fast_every_n_ = EpdManager::GetInstance().GetPartialForceFastEveryN();
		saved_partial_clear_before_draw_ = EpdManager::GetInstance().GetPartialClearBeforeDraw();
		EpdManager::GetInstance().SetPartialForceFastEveryN(std::numeric_limits<uint32_t>::max());
		current_mode_index_ = 0;
		for (size_t i = 0; i < kBenchModeCount; ++i) {
			avg_request_to_done_us_by_mode_[i] = 0;
			avg_manager_us_by_mode_[i] = 0;
		}
		ResetPhaseStateLocked();
		running_ = true;
		pending_dispatch_ = true;
		EpdManager::GetInstance().SetPartialClearBeforeDraw(kBenchModes[current_mode_index_].partial_clear_before_draw);

		char msg[128] = {0};
		std::snprintf(msg,
				  sizeof(msg),
				  "WordsGame: EPD测速 phase1/2 (%s, 25次)",
				  kBenchModes[current_mode_index_].name);
		ctx.board.GetDisplay()->SetChatMessage("system", msg);
		ESP_LOGW(kTag,
			 "Start phase %u/%u, mode=%s partial_clear_before_draw=%d",
			 static_cast<unsigned>(current_mode_index_ + 1),
			 static_cast<unsigned>(kBenchModeCount),
			 kBenchModes[current_mode_index_].name,
			 kBenchModes[current_mode_index_].partial_clear_before_draw ? 1 : 0);

		EnsureTimerCreated();
		StartTimer();
	}

	void OnExit(AppContext& ctx) override {
		(void)ctx;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			running_ = false;
			pending_dispatch_ = false;
			waiting_refresh_done_ = false;
			epd_ = nullptr;
		}
		EpdManager::GetInstance().SetPartialForceFastEveryN(saved_partial_force_fast_every_n_);
		EpdManager::GetInstance().SetPartialClearBeforeDraw(saved_partial_clear_before_draw_);
		StopTimer();
		DeleteTimer();
	}

	void OnButton(AppContext& ctx, const ButtonEvent& event) override {
		if (!IsClickLike(event)) {
			return;
		}
		if (event.id == AppButton::Start) {
			OnEnter(ctx);
		}
	}

private:
	struct RefreshRecord {
		int64_t request_start_us = 0;
		int64_t manager_start_us = 0;
		int64_t manager_end_us = 0;
		int64_t request_to_done_us = 0;
		int64_t manager_duration_us = 0;
		EpdManager::TaskType requested = EpdManager::TaskType::kPartial;
		EpdManager::TaskType effective = EpdManager::TaskType::kPartial;
		bool ok = false;
	};

	struct RefreshTaskContext {
		WordsGameEpdBenchApp* app = nullptr;
		size_t index = 0;
		int64_t request_start_us = 0;
		int16_t draw_x = 0;
		int16_t draw_y = 0;
		int16_t draw_w = 0;
		int16_t draw_h = 0;
	};

	void ResetStateLocked() {
		epd_ = nullptr;
		epd_width_ = 0;
		epd_height_ = 0;
		current_mode_index_ = 0;
		for (size_t i = 0; i < kBenchModeCount; ++i) {
			avg_request_to_done_us_by_mode_[i] = 0;
			avg_manager_us_by_mode_[i] = 0;
		}
		ResetPhaseStateLocked();
		session_start_us_ = 0;
	}

	void ResetPhaseStateLocked() {
		running_ = false;
		waiting_refresh_done_ = false;
		pending_dispatch_ = false;
		sent_count_ = 0;
		finished_count_ = 0;
		total_request_to_done_us_ = 0;
		total_manager_us_ = 0;
		for (auto& item : records_) {
			item = RefreshRecord{};
		}
	}

	void EnsureTimerCreated() {
		if (dispatch_timer_ != nullptr) {
			return;
		}
		esp_timer_create_args_t args = {
			.callback = &WordsGameEpdBenchApp::DispatchTimerCallback,
			.arg = this,
			.dispatch_method = ESP_TIMER_TASK,
			.name = "wg_epd_tick",
			.skip_unhandled_events = true,
		};
		if (esp_timer_create(&args, &dispatch_timer_) != ESP_OK) {
			dispatch_timer_ = nullptr;
			ESP_LOGE(kTag, "Failed to create dispatch timer");
		}
	}

	void StartTimer() {
		if (dispatch_timer_ == nullptr) {
			return;
		}
		(void)esp_timer_stop(dispatch_timer_);
		if (esp_timer_start_periodic(dispatch_timer_, kDispatchTimerPeriodUs) != ESP_OK) {
			ESP_LOGE(kTag, "Failed to start dispatch timer");
		}
	}

	void StopTimer() {
		if (dispatch_timer_ == nullptr) {
			return;
		}
		(void)esp_timer_stop(dispatch_timer_);
	}

	void DeleteTimer() {
		if (dispatch_timer_ == nullptr) {
			return;
		}
		(void)esp_timer_delete(dispatch_timer_);
		dispatch_timer_ = nullptr;
	}

	static void DispatchTimerCallback(void* arg) {
		auto* app = static_cast<WordsGameEpdBenchApp*>(arg);
		if (app == nullptr) {
			return;
		}
		app->OnDispatchTimer();
	}

	void OnDispatchTimer() {
		RefreshTaskContext* task_ctx = nullptr;
		EpdManager::Rect rect;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!running_ || epd_ == nullptr) {
				return;
			}
			if (waiting_refresh_done_ || !pending_dispatch_) {
				return;
			}
			if (sent_count_ >= kRefreshTotal) {
				return;
			}

			int16_t x = 0;
			int16_t y = 0;
			int16_t w = 0;
			int16_t h = 0;
			BuildRefreshRect(sent_count_, x, y, w, h);

			task_ctx = new (std::nothrow) RefreshTaskContext();
			if (task_ctx == nullptr) {
				ESP_LOGE(kTag, "Failed to allocate task context");
				return;
			}
			task_ctx->app = this;
			task_ctx->index = sent_count_;
			task_ctx->request_start_us = esp_timer_get_time();
			task_ctx->draw_x = x;
			task_ctx->draw_y = y;
			task_ctx->draw_w = w;
			task_ctx->draw_h = h;

			rect = EpdManager::Rect(x, y, w, h);
			waiting_refresh_done_ = true;
			pending_dispatch_ = false;
		}

		const bool queued = EpdManager::GetInstance().Schedule(
			EpdManager::TaskType::kPartial,
			&WordsGameEpdBenchApp::DrawPartialCallback,
			task_ctx,
			&WordsGameEpdBenchApp::DeleteTaskContext,
			rect,
			&WordsGameEpdBenchApp::RefreshDoneCallback,
			task_ctx);

		if (!queued) {
			DeleteTaskContext(task_ctx);
			std::lock_guard<std::mutex> lock(mutex_);
			if (running_) {
				waiting_refresh_done_ = false;
				pending_dispatch_ = true;
			}
			ESP_LOGW(kTag, "Schedule partial refresh failed, will retry");
			return;
		}

		std::lock_guard<std::mutex> lock(mutex_);
		if (!running_) {
			return;
		}
		sent_count_++;
		ESP_LOGI(kTag,
			 "Sent refresh [%u/%u], mode=%s request_start_ms=%u, rect=(%d,%d,%d,%d)",
			 static_cast<unsigned>(sent_count_),
			 static_cast<unsigned>(kRefreshTotal),
			 kBenchModes[current_mode_index_].name,
			 ToMs32(task_ctx->request_start_us),
			 static_cast<int>(task_ctx->draw_x),
			 static_cast<int>(task_ctx->draw_y),
			 static_cast<int>(task_ctx->draw_w),
			 static_cast<int>(task_ctx->draw_h));
	}

	void BuildRefreshRect(size_t index, int16_t& x, int16_t& y, int16_t& w, int16_t& h) const {
		const int16_t min_w = 120;
		const int16_t min_h = 50;
		w = std::max<int16_t>(min_w, static_cast<int16_t>(epd_width_ / 2));
		h = std::max<int16_t>(min_h, static_cast<int16_t>(epd_height_ / 4));
		x = static_cast<int16_t>((epd_width_ - w) / 2);
		const int16_t y_span = std::max<int16_t>(1, static_cast<int16_t>(epd_height_ - h - 8));
		y = static_cast<int16_t>(4 + static_cast<int16_t>((index * 6) % y_span));
	}

	static void DrawPartialCallback(Adafruit_GFX& gfx, void* ctx) {
		auto* task_ctx = static_cast<RefreshTaskContext*>(ctx);
		if (task_ctx == nullptr) {
			return;
		}
		gfx.setTextColor(GxEPD_BLACK);
		gfx.setCursor(static_cast<int16_t>(task_ctx->draw_x + 8), static_cast<int16_t>(task_ctx->draw_y + 20));
		gfx.print("EPD partial test");

		char line[48] = {0};
		std::snprintf(line,
				  sizeof(line),
				  "round: %u/%u",
				  static_cast<unsigned>(task_ctx->index + 1),
				  static_cast<unsigned>(kRefreshTotal));
		gfx.setCursor(static_cast<int16_t>(task_ctx->draw_x + 8), static_cast<int16_t>(task_ctx->draw_y + 40));
		gfx.print(line);
		gfx.drawRect(task_ctx->draw_x, task_ctx->draw_y, task_ctx->draw_w, task_ctx->draw_h, GxEPD_BLACK);
	}

	static void DeleteTaskContext(void* ctx) {
		auto* task_ctx = static_cast<RefreshTaskContext*>(ctx);
		delete task_ctx;
	}

	static void RefreshDoneCallback(EpdManager::TaskType requested,
							EpdManager::TaskType effective,
							int64_t manager_start_us,
							int64_t manager_end_us,
							bool ok,
							void* user_ctx) {
		auto* task_ctx = static_cast<RefreshTaskContext*>(user_ctx);
		if (task_ctx == nullptr || task_ctx->app == nullptr) {
			return;
		}
		task_ctx->app->HandleRefreshDone(*task_ctx, requested, effective, manager_start_us, manager_end_us, ok);
	}

	void HandleRefreshDone(const RefreshTaskContext& task_ctx,
				       EpdManager::TaskType requested,
				       EpdManager::TaskType effective,
				       int64_t manager_start_us,
				       int64_t manager_end_us,
				       bool ok) {
		bool all_done = false;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!running_) {
				return;
			}

			const size_t index = task_ctx.index;
			if (index >= records_.size()) {
				return;
			}

			auto& record = records_[index];
			record.request_start_us = task_ctx.request_start_us;
			record.manager_start_us = manager_start_us;
			record.manager_end_us = manager_end_us;
			record.request_to_done_us = std::max<int64_t>(0, manager_end_us - task_ctx.request_start_us);
			record.manager_duration_us = std::max<int64_t>(0, manager_end_us - manager_start_us);
			record.requested = requested;
			record.effective = effective;
			record.ok = ok;

			total_request_to_done_us_ += record.request_to_done_us;
			total_manager_us_ += record.manager_duration_us;

			finished_count_ = std::min<size_t>(kRefreshTotal, finished_count_ + 1);
			waiting_refresh_done_ = false;
			all_done = (finished_count_ >= kRefreshTotal);
			if (all_done) {
				running_ = false;
				pending_dispatch_ = false;
			} else {
				pending_dispatch_ = true;
			}

			const uint32_t request_to_done_ms = ToUs32(record.request_to_done_us) / 1000u;
			const uint32_t request_to_done_us_rem = ToUs32(record.request_to_done_us) % 1000u;
			const uint32_t manager_ms = ToUs32(record.manager_duration_us) / 1000u;
			const uint32_t manager_us_rem = ToUs32(record.manager_duration_us) % 1000u;
			ESP_LOGI(kTag,
				 "Done refresh [%u/%u], mode=%s requested=%s effective=%s ok=%d start_ms=%u end_ms=%u request_to_done=%u.%03ums manager=%u.%03ums",
				 static_cast<unsigned>(index + 1),
				 static_cast<unsigned>(kRefreshTotal),
				 kBenchModes[current_mode_index_].name,
				 TaskTypeName(requested),
				 TaskTypeName(effective),
				 ok ? 1 : 0,
				 ToMs32(record.request_start_us),
				 ToMs32(record.manager_end_us),
				 request_to_done_ms,
				 request_to_done_us_rem,
				 manager_ms,
				 manager_us_rem);
		}

		if (all_done) {
			StopTimer();
			OnPhaseCompleted();
		}
	}

	void OnPhaseCompleted() {
		size_t finished_mode = 0;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			finished_mode = current_mode_index_;
		}

		PrintSummary(finished_mode);

		bool has_next_mode = false;
		size_t next_mode = 0;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			next_mode = finished_mode + 1;
			if (next_mode < kBenchModeCount) {
				current_mode_index_ = next_mode;
				ResetPhaseStateLocked();
				running_ = true;
				pending_dispatch_ = true;
				has_next_mode = true;
			}
		}

		if (has_next_mode) {
			EpdManager::GetInstance().SetPartialClearBeforeDraw(kBenchModes[next_mode].partial_clear_before_draw);
			ESP_LOGW(kTag,
				 "Start phase %u/%u, mode=%s partial_clear_before_draw=%d",
				 static_cast<unsigned>(next_mode + 1),
				 static_cast<unsigned>(kBenchModeCount),
				 kBenchModes[next_mode].name,
				 kBenchModes[next_mode].partial_clear_before_draw ? 1 : 0);
			StartTimer();
			return;
		}

		PrintCompareSummary();
		EpdManager::GetInstance().SetPartialForceFastEveryN(saved_partial_force_fast_every_n_);
		EpdManager::GetInstance().SetPartialClearBeforeDraw(saved_partial_clear_before_draw_);
	}

	void PrintSummary(size_t mode_index) {
		std::lock_guard<std::mutex> lock(mutex_);
		const size_t count = finished_count_;
		if (count == 0) {
			ESP_LOGW(kTag, "No completed refresh records");
			return;
		}

		ESP_LOGI(kTag, "========= EPD PARTIAL REFRESH BENCH REPORT =========");
		ESP_LOGI(kTag,
			 "session_start_ms=%u mode=%s total_runs=%u",
			 ToMs32(session_start_us_),
			 kBenchModes[mode_index].name,
			 static_cast<unsigned>(count));
		for (size_t i = 0; i < count; ++i) {
			const auto& item = records_[i];
			const uint32_t request_to_done_ms = ToUs32(item.request_to_done_us) / 1000u;
			const uint32_t request_to_done_us_rem = ToUs32(item.request_to_done_us) % 1000u;
			const uint32_t manager_ms = ToUs32(item.manager_duration_us) / 1000u;
			const uint32_t manager_us_rem = ToUs32(item.manager_duration_us) % 1000u;
			ESP_LOGI(kTag,
				 "[%02u] start_ms=%u end_ms=%u request_to_done=%u.%03ums manager=%u.%03ums requested=%s effective=%s ok=%d",
				 static_cast<unsigned>(i + 1),
				 ToMs32(item.request_start_us),
				 ToMs32(item.manager_end_us),
				 request_to_done_ms,
				 request_to_done_us_rem,
				 manager_ms,
				 manager_us_rem,
				 TaskTypeName(item.requested),
				 TaskTypeName(item.effective),
				 item.ok ? 1 : 0);
		}
		const int64_t avg_request_to_done_us = total_request_to_done_us_ / static_cast<int64_t>(count);
		const int64_t avg_manager_us = total_manager_us_ / static_cast<int64_t>(count);
		const uint32_t avg_request_to_done_ms = ToUs32(avg_request_to_done_us) / 1000u;
		const uint32_t avg_request_to_done_us_rem = ToUs32(avg_request_to_done_us) % 1000u;
		const uint32_t avg_manager_ms = ToUs32(avg_manager_us) / 1000u;
		const uint32_t avg_manager_us_rem = ToUs32(avg_manager_us) % 1000u;
		int64_t partial_only_total_request_to_done_us = 0;
		int64_t partial_only_total_manager_us = 0;
		size_t partial_only_count = 0;
		for (size_t i = 0; i < count; ++i) {
			const auto& item = records_[i];
			if (item.effective == EpdManager::TaskType::kPartial) {
				partial_only_total_request_to_done_us += item.request_to_done_us;
				partial_only_total_manager_us += item.manager_duration_us;
				partial_only_count++;
			}
		}
		avg_request_to_done_us_by_mode_[mode_index] = avg_request_to_done_us;
		avg_manager_us_by_mode_[mode_index] = avg_manager_us;
		ESP_LOGI(kTag,
			 "Average request_to_done: %u.%03ums (%u runs)",
			 avg_request_to_done_ms,
			 avg_request_to_done_us_rem,
			 static_cast<unsigned>(count));
		ESP_LOGI(kTag,
			 "Average manager_duration: %u.%03ums (%u runs)",
			 avg_manager_ms,
			 avg_manager_us_rem,
			 static_cast<unsigned>(count));
		if (partial_only_count > 0 && partial_only_count < count) {
			const int64_t partial_only_avg_request_to_done_us =
				partial_only_total_request_to_done_us / static_cast<int64_t>(partial_only_count);
			const int64_t partial_only_avg_manager_us =
				partial_only_total_manager_us / static_cast<int64_t>(partial_only_count);
			ESP_LOGI(kTag,
				 "Average(partial-only) request_to_done=%u.%03ums manager=%u.%03ums (%u runs)",
				 ToUs32(partial_only_avg_request_to_done_us) / 1000u,
				 ToUs32(partial_only_avg_request_to_done_us) % 1000u,
				 ToUs32(partial_only_avg_manager_us) / 1000u,
				 ToUs32(partial_only_avg_manager_us) % 1000u,
				 static_cast<unsigned>(partial_only_count));
		}
		ESP_LOGI(kTag, "=====================================================");
	}

	void PrintCompareSummary() const {
		std::lock_guard<std::mutex> lock(mutex_);
		ESP_LOGI(kTag, "========= EPD PARTIAL REFRESH MODE COMPARE =========");
		for (size_t i = 0; i < kBenchModeCount; ++i) {
			const uint32_t req_ms = ToUs32(avg_request_to_done_us_by_mode_[i]) / 1000u;
			const uint32_t req_us = ToUs32(avg_request_to_done_us_by_mode_[i]) % 1000u;
			const uint32_t mgr_ms = ToUs32(avg_manager_us_by_mode_[i]) / 1000u;
			const uint32_t mgr_us = ToUs32(avg_manager_us_by_mode_[i]) % 1000u;
			ESP_LOGI(kTag,
				 "Mode[%u] %s partial_clear_before_draw=%d avg_request_to_done=%u.%03ums avg_manager=%u.%03ums",
				 static_cast<unsigned>(i + 1),
				 kBenchModes[i].name,
				 kBenchModes[i].partial_clear_before_draw ? 1 : 0,
				 req_ms,
				 req_us,
				 mgr_ms,
				 mgr_us);
		}
		const int64_t delta_req = avg_request_to_done_us_by_mode_[1] - avg_request_to_done_us_by_mode_[0];
		const int64_t delta_mgr = avg_manager_us_by_mode_[1] - avg_manager_us_by_mode_[0];
		const int32_t delta_req_ms = static_cast<int32_t>(delta_req / 1000);
		const int32_t delta_req_us_rem = static_cast<int32_t>((delta_req % 1000) >= 0 ? (delta_req % 1000) : -(delta_req % 1000));
		const int32_t delta_mgr_ms = static_cast<int32_t>(delta_mgr / 1000);
		const int32_t delta_mgr_us_rem = static_cast<int32_t>((delta_mgr % 1000) >= 0 ? (delta_mgr % 1000) : -(delta_mgr % 1000));
		ESP_LOGI(kTag,
			 "Delta(clear_off-clear_on): request_to_done=%d.%03dms manager=%d.%03dms",
			 static_cast<int>(delta_req_ms),
			 static_cast<int>(delta_req_us_rem),
			 static_cast<int>(delta_mgr_ms),
			 static_cast<int>(delta_mgr_us_rem));
		ESP_LOGI(kTag, "=====================================================");
	}

	mutable std::mutex mutex_{};
	CustomEpdDisplay* epd_ = nullptr;
	esp_timer_handle_t dispatch_timer_ = nullptr;
	int16_t epd_width_ = 0;
	int16_t epd_height_ = 0;
	size_t current_mode_index_ = 0;
	uint32_t saved_partial_force_fast_every_n_ = 30;
	bool saved_partial_clear_before_draw_ = true;

	bool running_ = false;
	bool waiting_refresh_done_ = false;
	bool pending_dispatch_ = false;
	size_t sent_count_ = 0;
	size_t finished_count_ = 0;
	int64_t total_request_to_done_us_ = 0;
	int64_t total_manager_us_ = 0;
	int64_t session_start_us_ = 0;
	std::array<int64_t, kBenchModeCount> avg_request_to_done_us_by_mode_{};
	std::array<int64_t, kBenchModeCount> avg_manager_us_by_mode_{};
	std::array<RefreshRecord, kRefreshTotal> records_{};
};

} // namespace

std::unique_ptr<AppBase> MakeWordsGameApp() {
	return std::make_unique<WordsGameEpdBenchApp>();
}
