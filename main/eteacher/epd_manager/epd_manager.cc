#include "epd_manager/epd_manager.h"
#include "eteacher/app_ui/debug.h"

#include <esp_log.h>
#include <esp_timer.h>

namespace {

static const char* TAG = "EpdManager";

constexpr uint32_t kQueueLength = 3;

// Min refresh interval constraints (seconds):
// - Partial: 0.3s
// - Fast:    1.5s
// - Full:    3.0s
constexpr int64_t kPartialMinIntervalUs = 0;
constexpr int64_t kFastMinIntervalUs = 0;
constexpr int64_t kFullMinIntervalUs = 0;

constexpr uint32_t kTaskStack = 4096;
constexpr UBaseType_t kTaskPrio = 1;

TickType_t DelayTicksFromUs(int64_t us) {
	if (us <= 0) {
		return 0;
	}
	// Round up to at least 1 tick when us > 0.
	const int64_t ms = (us + 999) / 1000;
	return pdMS_TO_TICKS(ms);
}

} // namespace

EpdManager& EpdManager::GetInstance() {
	static EpdManager inst;
	return inst;
}

void EpdManager::Init(CustomEpdDisplay* epd) {
	if (inited_) {
		return;
	}
	if (epd == nullptr) {
		ESP_LOGE(TAG, "Init failed: epd is null");
		return;
	}

	epd_ = epd;
	queue_ = xQueueCreate(kQueueLength, sizeof(TaskItem));
	if (queue_ == nullptr) {
		ESP_LOGE(TAG, "Failed to create queue");
		return;
	}

	BaseType_t ok = xTaskCreate(&EpdManager::TaskEntry, "epd_mgr", kTaskStack, this, kTaskPrio, &task_);
	if (ok != pdPASS) {
		ESP_LOGE(TAG, "Failed to create task");
		vQueueDelete(queue_);
		queue_ = nullptr;
		return;
	}

	inited_ = true;
	ESP_LOGI(TAG,
		 "Initialized (queue=%u, partial_force_fast_every_n=%u, partial_clear_before_draw=%u)",
		 (unsigned)kQueueLength,
		 (unsigned)partial_force_fast_every_n_,
		 GetPartialClearBeforeDraw() ? 1u : 0u);
}

void EpdManager::SetPartialForceFastEveryN(uint32_t n) {
	if (n == 0) {
		n = 1;
	}
	partial_force_fast_every_n_ = n;
}

bool EpdManager::Schedule(TaskType type,
					  Epd::DrawCallback cb,
					  void* ctx,
					  void (*ctx_deleter)(void*),
					  Rect rect,
					  RefreshDoneCallback done_cb,
					  void* done_ctx) {
	if (!inited_ || queue_ == nullptr || epd_ == nullptr) {
		if (ctx_deleter && ctx) {
			ctx_deleter(ctx);
		}
		return false;
	}
	if (cb == nullptr) {
		if (ctx_deleter && ctx) {
			ctx_deleter(ctx);
		}
		return false;
	}

	TaskItem item{type, rect, cb, ctx, ctx_deleter, done_cb, done_ctx};

	// Reject input when queue already holds the maximum number of tasks.
	if (uxQueueMessagesWaiting(queue_) >= kQueueLength) {
		if (ctx_deleter && ctx) {
			ctx_deleter(ctx);
		}
		return false;
	}

	if (xQueueSend(queue_, &item, 0) != pdTRUE) {
		if (ctx_deleter && ctx) {
			ctx_deleter(ctx);
		}
		return false;
	}
	return true;
}

void EpdManager::TaskEntry(void* arg) {
	auto* self = static_cast<EpdManager*>(arg);
	self->Run();
	vTaskDelete(nullptr);
}

EpdManager::TaskType EpdManager::ResolveType(TaskType requested) {
	if (requested != TaskType::kPartial) {
		partial_count_since_fast_ = 0;
		return requested;
	}

	if (partial_count_since_fast_ >= partial_force_fast_every_n_) {
		// Force fast refresh on this task even if requested partial.
		partial_count_since_fast_ = 0;
		return TaskType::kFast;
	}
	return TaskType::kPartial;
}

void EpdManager::Run() {
	while (true) {
		TaskItem item;
		if (xQueueReceive(queue_, &item, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		TaskType effective = ResolveType(item.type);

		int64_t now = esp_timer_get_time();
		int64_t earliest = now;
		int64_t* last_ts = nullptr;
		int64_t min_interval = 0;

		switch (effective) {
		case TaskType::kPartial:
			last_ts = &last_partial_us_;
			min_interval = kPartialMinIntervalUs;
			break;
		case TaskType::kFast:
			last_ts = &last_fast_us_;
			min_interval = kFastMinIntervalUs;
			break;
		case TaskType::kFull:
			last_ts = &last_full_us_;
			min_interval = kFullMinIntervalUs;
			break;
		}

		if (last_ts) {
			earliest = (*last_ts) + min_interval;
		}
		if (now < earliest) {
			vTaskDelay(DelayTicksFromUs(earliest - now));
		}

		// Execute
		bool ok = false;
		const int64_t refresh_start_us = esp_timer_get_time();
		{
			DisplayLockGuard guard(epd_);
			auto& gfx = epd_->Driver();
			switch (effective) {
			case TaskType::kPartial:
			{
				// Partial refresh: update only a window.
				// Use GxEPD2 paged drawing (firstPage/nextPage) so we don't rely on full-framebuffer APIs.
				// Requirement: use input x/y/w/h directly; no clamp/align logic here.
				const Rect rect = item.rect;
				if (rect.w > 0 && rect.h > 0) {
					#if APP_UI_DEBUG
					ESP_LOGW(TAG, "EPD partial refresh: x=%d y=%d w=%d h=%d", (int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h);
					#endif
					gfx.epd2.selectFastFullUpdate(false);
					gfx.setPartialWindow(rect.x, rect.y, rect.w, rect.h);
					gfx.firstPage();
					do {
						if (GetPartialClearBeforeDraw()) {
							gfx.fillRect(rect.x, rect.y, rect.w, rect.h, GxEPD_WHITE);
						}
						item.cb(gfx, item.ctx);
					} while (gfx.nextPage());
					ok = true;
					partial_count_since_fast_++;
				}
			}
				break;
			case TaskType::kFast:
				#if APP_UI_DEBUG
				ESP_LOGW(TAG, "EPD fast refresh (full screen, partial-update mode)");
				#endif
				// Fast refresh: full-screen refresh using a "faster" waveform policy.
				// Implemented via GxEPD2 paged drawing (firstPage/nextPage).
				gfx.epd2.selectFastFullUpdate(true);
				gfx.setFullWindow();
				gfx.firstPage();
				do {
					gfx.fillScreen(GxEPD_WHITE);
					item.cb(gfx, item.ctx);
				} while (gfx.nextPage());
				ok = true;
				partial_count_since_fast_ = 0;
				break;
			case TaskType::kFull:
				#if APP_UI_DEBUG
				ESP_LOGW(TAG, "EPD full refresh (full update waveform)");
				#endif
				// Full refresh: whole screen, full update waveform.
				// Implemented via GxEPD2 paged drawing (firstPage/nextPage).
				gfx.epd2.selectFastFullUpdate(false);
				gfx.setFullWindow();
				gfx.firstPage();
				do {
					gfx.fillScreen(GxEPD_WHITE);
					item.cb(gfx, item.ctx);
				} while (gfx.nextPage());
				ok = true;
				partial_count_since_fast_ = 0;
				break;
			}
		}

		now = esp_timer_get_time();
		const int64_t refresh_end_us = now;
		switch (effective) {
		case TaskType::kPartial:
			last_partial_us_ = now;
			break;
		case TaskType::kFast:
			last_fast_us_ = now;
			break;
		case TaskType::kFull:
			last_full_us_ = now;
			break;
		}

		if (!ok) {
			#if APP_UI_DEBUG
			ESP_LOGW(TAG, "Refresh failed (type=%u)", (unsigned)effective);
			#endif
		}

		if (item.done_cb) {
			item.done_cb(item.type, effective, refresh_start_us, refresh_end_us, ok, item.done_ctx);
		}

		if (item.ctx_deleter && item.ctx) {
			item.ctx_deleter(item.ctx);
		}
	}
}
