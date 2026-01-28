#include "eteacher/apps/free_conversation/free_conversation.h"

#include <Adafruit_GFX.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <esp_log.h>

#include "assets.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_manager/app_manager.h"
#include "eteacher/app_manager/menu.h"
#include "eteacher/app_ui/status_bar.h"
#include "eteacher/app_service/app_service.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "eteacher/font_manager/font_manager.h"
#include "wifi_manager.h"

namespace {

static constexpr const char* kTag = "FreeConversation";
static constexpr const char* kStatusFont = "wenquanyi_9pt";
static constexpr const char* kTextFont = "wenquanyi_11pt";

static constexpr int kMaxHistoryLines = 100;
static constexpr int kAvatarGap = 6;
static constexpr int kLineGap = 4;

struct BinImage {
	const uint8_t* data = nullptr;
	uint16_t width = 0;
	uint16_t height = 0;
	size_t data_size = 0;
};

static inline uint16_t ReadLE16(const uint8_t* p) {
	return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8));
}

bool LoadBinImage(const std::string& name, BinImage* out) {
	if (!out) {
		return false;
	}
	void* ptr = nullptr;
	size_t size = 0;
	if (!Assets::GetInstance().GetAssetData(name, ptr, size) || !ptr || size < 4) {
		return false;
	}
	const auto* data = static_cast<const uint8_t*>(ptr);
	const uint16_t w = ReadLE16(data);
	const uint16_t h = ReadLE16(data + 2);
	const size_t stride = (w + 7u) / 8u;
	const size_t bytes = static_cast<size_t>(stride) * h;
	if (size < 4 + bytes) {
		return false;
	}
	out->data = data + 4;
	out->width = w;
	out->height = h;
	out->data_size = bytes;
	return true;
}

bool LoadBinImageFallback(const std::vector<std::string>& names, BinImage* out) {
	for (const auto& name : names) {
		if (LoadBinImage(name, out)) {
			return true;
		}
	}
	return false;
}


int16_t GetFontHeight(std::string_view font_name) {
	const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
	if (!font || !font->Ready()) {
		return 12;
	}
	const auto& header = font->Header();
	return static_cast<int16_t>(header.ascent + header.descent);
}

int16_t GetFontAscent(std::string_view font_name) {
	const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
	if (!font || !font->Ready()) {
		return 9;
	}
	return static_cast<int16_t>(font->Header().ascent);
}

void DrawBatteryIcon(Adafruit_GFX& gfx, int16_t right_x, int16_t top_y, int16_t bar_h, int level) {
	const int16_t battery_w = 22;
	const int16_t battery_h = 12;
	const int16_t nub_w = 3;
	const int16_t nub_h = std::max<int16_t>(2, static_cast<int16_t>(battery_h / 2));
	const int16_t radius = 2;

	const int16_t x = static_cast<int16_t>(right_x - (battery_w + nub_w));
	const int16_t y = static_cast<int16_t>(top_y + (bar_h - battery_h) / 2);

	gfx.drawRoundRect(x, y, battery_w, battery_h, radius, GxEPD_BLACK);
	const int16_t nub_y = static_cast<int16_t>(y + (battery_h - nub_h) / 2);
	gfx.fillRoundRect(static_cast<int16_t>(x + battery_w - 1), nub_y, nub_w, nub_h, radius, GxEPD_BLACK);

	if (level < 0) {
		return;
	}
	const int scaled2 = level * 2;
	int blocks = 0;
	if (scaled2 < 25) {
		blocks = 0;
	} else if (scaled2 < 75) {
		blocks = 1;
	} else if (scaled2 < 125) {
		blocks = 2;
	} else if (scaled2 < 175) {
		blocks = 3;
	} else {
		blocks = 4;
	}
	const int16_t padding = 1;
	const int16_t inner_x = static_cast<int16_t>(x + padding);
	const int16_t inner_y = static_cast<int16_t>(y + padding);
	const int16_t inner_w = static_cast<int16_t>(battery_w - padding * 2);
	const int16_t inner_h = static_cast<int16_t>(battery_h - padding * 2);
	const int16_t gap = 1;
	gfx.fillRect(inner_x, inner_y, inner_w, inner_h, GxEPD_WHITE);
	const int16_t block_w = static_cast<int16_t>((inner_w - gap * 3) / 4);
	if (block_w <= 0 || inner_h <= 0) {
		return;
	}
	for (int i = 0; i < blocks; ++i) {
		const int16_t bx = static_cast<int16_t>(inner_x + i * (block_w + gap));
		gfx.fillRect(bx, inner_y, block_w, inner_h, GxEPD_BLACK);
	}
}

void DrawSelectionRect(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t border) {
	if (w <= 0 || h <= 0 || border <= 0) {
		return;
	}
	int16_t radius = static_cast<int16_t>(std::min<int16_t>(8, std::min<int16_t>(w, h) / 4));
	for (int i = 0; i < border; ++i) {
		gfx.drawRoundRect(x - i, y - i, w + i * 2, h + i * 2, radius + i, GxEPD_BLACK);
	}
}

void DrawAvatar(Adafruit_GFX& gfx, const BinImage& img, int16_t x, int16_t y) {
	if (!img.data || img.width == 0 || img.height == 0) {
		return;
	}
	const size_t gray_size = static_cast<size_t>(img.width) * img.height;
	if (img.data_size == gray_size) {
		for (int16_t yy = 0; yy < static_cast<int16_t>(img.height); ++yy) {
			for (int16_t xx = 0; xx < static_cast<int16_t>(img.width); ++xx) {
				const uint8_t v = img.data[yy * img.width + xx];
				if (v < 128) {
					gfx.drawPixel(x + xx, y + yy, GxEPD_BLACK);
				}
			}
		}
		return;
	}
	gfx.drawBitmap(x, y, img.data, img.width, img.height, GxEPD_BLACK);
}

size_t Utf8CharLen(uint8_t c) {
	if ((c & 0x80) == 0x00) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	if ((c & 0xF8) == 0xF0) return 4;
	return 1;
}

std::vector<std::string> WrapText(CustomEpdDisplay* epd, const std::string& text, int max_width, std::string_view font) {
	std::vector<std::string> lines;
	if (!epd) {
		lines.push_back(text);
		return lines;
	}
	if (text.empty()) {
		lines.emplace_back("");
		return lines;
	}
	std::string line;
	size_t i = 0;
	while (i < text.size()) {
		const size_t len = Utf8CharLen(static_cast<uint8_t>(text[i]));
		const std::string ch = text.substr(i, len);
		if (line.empty()) {
			line = ch;
		} else {
			const std::string candidate = line + ch;
			if (epd->MeasureUtf8Width(candidate, font) > max_width) {
				lines.push_back(line);
				line = ch;
			} else {
				line = candidate;
			}
		}
		i += len;
	}
	if (!line.empty()) {
		lines.push_back(line);
	}
	return lines;
}

std::string EscapeJson(const std::string& s) {
	std::string out;
	out.reserve(s.size());
	for (unsigned char c : s) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char buf[7];
				std::snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out.push_back(static_cast<char>(c));
			}
			break;
		}
	}
	return out;
}

struct ConversationEntry {
	bool is_teacher = false;
	std::string text;
	std::string translated;
	bool show_translation = false;
	std::vector<std::string> lines;
	std::vector<std::string> translated_lines;
};

struct RenderLine {
	int entry_index = 0;
	std::string text;
	bool is_teacher = false;
	int total_lines_in_entry = 0;
};

struct RenderSnapshot {
	CustomEpdDisplay* epd = nullptr;
	BinImage teacher_avatar;
	BinImage student_avatar;
	std::vector<RenderLine> lines;
	int screen_w = 0;
	int screen_h = 0;
	int content_top = 0;
	int content_bottom = 0;
	int line_height = 0;
	int text_font_ascent = 0;
	int text_font_height = 0;
	int view_line_offset = 0;
	int visible_lines = 0;
	int selected_entry = -1;
	int selected_entry_first_line = -1;
	int selected_entry_last_line = -1;
	eteacher::app_menu::MenuStatus status;
	std::string footer_text;
	int slot_index = -1;
};

static std::array<RenderSnapshot, 2> g_snapshots;
static std::atomic_bool g_snapshot_in_use[2] = {false, false};

static RenderSnapshot* AcquireSnapshot() {
	for (int i = 0; i < static_cast<int>(g_snapshots.size()); ++i) {
		bool expected = false;
		if (g_snapshot_in_use[i].compare_exchange_strong(expected, true)) {
			auto* snap = &g_snapshots[i];
			snap->lines.clear();
			snap->footer_text.clear();
			snap->slot_index = i;
			return snap;
		}
	}
	return nullptr;
}

void DeleteSnapshot(void* ctx) {
	auto* snap = static_cast<RenderSnapshot*>(ctx);
	if (!snap) {
		return;
	}
	if (snap->slot_index >= 0 && snap->slot_index < static_cast<int>(g_snapshots.size())) {
		snap->lines.clear();
		snap->footer_text.clear();
		g_snapshot_in_use[snap->slot_index].store(false);
	}
}

} // namespace

struct FreeConversationApp::Impl : public CustomEpdDisplay::ChatMessageListener {
	AppContext* ctx = nullptr;
	std::vector<ConversationEntry> entries;
	std::vector<int> line_to_entry;
	std::vector<int> entry_start_line;
	int cursor_line_index = 0;
	int view_line_offset = 0;
	int visible_lines = 1;
	uint32_t tick_accum_ms = 0;
	bool line_index_dirty = true;
	bool scroll_to_latest_pending = false;
	int last_screen_w = 0;
	int last_screen_h = 0;
	bool recording = false;
	bool avatars_loaded = false;
	BinImage teacher_avatar;
	BinImage student_avatar;

	EteacherState last_state = kEteacherStateUnknown;
	int last_minute = -1;
	bool last_wifi_connected = false;
	int last_battery_level = -1;
	std::string last_volume_text;
	std::shared_ptr<ConversationTranslator> translator;
	std::shared_ptr<int> alive_token = std::make_shared<int>(0);

	CustomEpdDisplay* hooked_epd = nullptr;

	void EnsureAvatarsLoaded() {
		if (avatars_loaded) {
			return;
		}
		avatars_loaded = true;
		LoadBinImageFallback({ "teacher.bin"}, &teacher_avatar);
		LoadBinImageFallback({ "student.bin"}, &student_avatar);
		if (student_avatar.data == nullptr && teacher_avatar.data != nullptr) {
			student_avatar = teacher_avatar;
		}
	}

	void InstallDisplayHook() {
		if (!ctx) {
			return;
		}
		auto* epd = dynamic_cast<CustomEpdDisplay*>(ctx->board.GetDisplay());
		if (!epd || hooked_epd) {
			return;
		}
		epd->SetChatMessageListener(this);
		hooked_epd = epd;
	}

	void RemoveDisplayHook() {
		if (!hooked_epd) {
			hooked_epd = nullptr;
			return;
		}
		hooked_epd->SetChatMessageListener(nullptr);
		hooked_epd = nullptr;
	}

	void OnChatMessage(const char* role, const char* content) override {
		if (!ctx || role == nullptr || content == nullptr) {
			return;
		}
		if (std::strlen(content) == 0) {
			return;
		}
		if (std::strcmp(role, "assistant") == 0) {
			AddEntry(true, content);
		} else if (std::strcmp(role, "user") == 0) {
			AddEntry(false, content);
		} else {
			return;
		}
		Render(*ctx);
	}

	void StartListening() {
		AppService::GetInstance().StartListening();
		recording = true;
	}

	void StopListening() {
		AppService::GetInstance().StopListening();
		recording = false;
	}

	void AddEntry(bool is_teacher, const std::string& text) {
		ConversationEntry entry;
		entry.is_teacher = is_teacher;
		entry.text = text;
		entries.push_back(std::move(entry));
		line_index_dirty = true;
		scroll_to_latest_pending = true;
	}

	void MoveCursorLine(int delta) {
		if (line_to_entry.empty()) {
			return;
		}
		cursor_line_index = std::max<int>(0, std::min<int>(static_cast<int>(line_to_entry.size()) - 1, cursor_line_index + delta));
		EnsureCursorVisible();
	}

	void MoveCursorToLatest() {
		if (line_to_entry.empty()) {
			cursor_line_index = 0;
			view_line_offset = 0;
			return;
		}
		cursor_line_index = static_cast<int>(line_to_entry.size()) - 1;
		EnsureCursorVisible();
	}

	void PageScroll(int pages) {
		if (visible_lines <= 0 || line_to_entry.empty()) {
			return;
		}
		const int max_offset = std::max<int>(0, static_cast<int>(line_to_entry.size()) - visible_lines);
		view_line_offset = std::max<int>(0, std::min<int>(max_offset, view_line_offset + pages * visible_lines));
		cursor_line_index = std::max<int>(view_line_offset, std::min<int>(view_line_offset + visible_lines - 1, cursor_line_index));
	}

	void EnsureCursorVisible() {
		if (visible_lines <= 0) {
			return;
		}
		if (cursor_line_index < view_line_offset) {
			view_line_offset = cursor_line_index;
		} else if (cursor_line_index >= view_line_offset + visible_lines) {
			view_line_offset = cursor_line_index - visible_lines + 1;
		}
	}

	void ToggleTranslationSelected() {
		int entry_index = GetSelectedEntry();
		if (entry_index < 0 || entry_index >= static_cast<int>(entries.size())) {
			return;
		}
		auto& entry = entries[entry_index];
		if (entry.show_translation) {
			entry.show_translation = false;
			line_index_dirty = true;
			return;
		}
		if (!entry.translated.empty()) {
			entry.show_translation = true;
			line_index_dirty = true;
			return;
		}
		if (!translator) {
			ESP_LOGW(kTag, "Translation provider not set");
			return;
		}
		entry.show_translation = true;
		line_index_dirty = true;
		const std::string original = entry.text;
		const int target_index = entry_index;
		auto weak_token = std::weak_ptr<int>(alive_token);
		translator->TranslateAsync(entry.text, [this, weak_token, target_index, original](std::string translated) mutable {
			if (!weak_token.lock() || translated.empty()) {
				return;
			}
			auto& app = AppService::GetInstance();
			app.Schedule([this, weak_token, target_index, original, translated = std::move(translated)]() mutable {
				if (!weak_token.lock() || !ctx) {
					return;
				}
				if (target_index < 0 || target_index >= static_cast<int>(entries.size())) {
					return;
				}
				auto& target = entries[target_index];
				if (target.text != original) {
					return;
				}
				target.translated = std::move(translated);
				target.translated_lines.clear();
				target.lines.clear();
				target.show_translation = true;
				line_index_dirty = true;
				Render(*ctx);
			});
		});
	}

	void SpeakSelected() {
		int entry_index = GetSelectedEntry();
		if (entry_index < 0 || entry_index >= static_cast<int>(entries.size())) {
			return;
		}
		const auto& entry = entries[entry_index];
		const std::string& content = entry.show_translation && !entry.translated.empty() ? entry.translated : entry.text;
		std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"self.tts.speak\",\"arguments\":{\"text\":\"";
		payload += EscapeJson(content);
		payload += "\"}}}";
		auto& app = AppService::GetInstance();
		app.Schedule([payload = std::move(payload), &app]() mutable {
			app.SendMcpMessage(payload);
		});
	}

	int GetSelectedEntry() const {
		if (line_to_entry.empty() || cursor_line_index < 0 || cursor_line_index >= static_cast<int>(line_to_entry.size())) {
			return -1;
		}
		return line_to_entry[cursor_line_index];
	}

	void RebuildLineIndex() {
		auto* epd = ctx ? dynamic_cast<CustomEpdDisplay*>(ctx->board.GetDisplay()) : nullptr;
		if (!epd) {
			line_to_entry.clear();
			entry_start_line.clear();
			return;
		}
		EnsureAvatarsLoaded();

		const int screen_w = epd->width();
		const int padding = eteacher::app_menu::MenuStyle{}.padding;
		const int avatar_w_left = teacher_avatar.width > 0 ? teacher_avatar.width : 24;
		const int avatar_w_right = student_avatar.width > 0 ? student_avatar.width : 24;
		const int text_left_teacher = padding + avatar_w_left + kAvatarGap;
		const int teacher_max_w = std::max<int>(20, screen_w - padding - text_left_teacher);
		const int student_avatar_x = screen_w - padding - avatar_w_right;
		const int student_max_w = std::max<int>(20, student_avatar_x - kAvatarGap - padding);

		line_to_entry.clear();
		entry_start_line.clear();
		entry_start_line.resize(entries.size());

		int line_index = 0;
		for (size_t i = 0; i < entries.size(); ++i) {
			auto& entry = entries[i];
			const int max_w = entry.is_teacher ? teacher_max_w : student_max_w;

			if (entry.lines.empty()) {
				entry.lines = WrapText(epd, entry.text, max_w, kTextFont);
			}
			if (!entry.translated.empty() && entry.translated_lines.empty()) {
				entry.translated_lines = WrapText(epd, entry.translated, max_w, kTextFont);
			}
			entry_start_line[i] = line_index;
			const auto& lines = (entry.show_translation && !entry.translated_lines.empty()) ? entry.translated_lines : entry.lines;
			line_index += static_cast<int>(std::max<size_t>(1, lines.size()));
			for (size_t j = 0; j < std::max<size_t>(1, lines.size()); ++j) {
				line_to_entry.push_back(static_cast<int>(i));
			}
		}
	}

	bool TrimHistoryToMaxLines() {
		if (line_to_entry.empty()) {
			return false;
		}
		const int extra = static_cast<int>(line_to_entry.size()) - kMaxHistoryLines;
		if (extra <= 0 || entries.empty()) {
			return false;
		}
		int lines_removed = 0;
		int entries_removed = 0;
		const int total_lines = static_cast<int>(line_to_entry.size());
		for (size_t i = 0; i < entry_start_line.size() && lines_removed < extra; ++i) {
			const int start = entry_start_line[i];
			const int next_start = (i + 1 < entry_start_line.size()) ? entry_start_line[i + 1] : total_lines;
			const int lines_in_entry = std::max<int>(1, next_start - start);
			lines_removed += lines_in_entry;
			++entries_removed;
		}
		if (entries_removed <= 0) {
			return false;
		}
		entries.erase(entries.begin(), entries.begin() + entries_removed);
		cursor_line_index = std::max<int>(0, cursor_line_index - lines_removed);
		view_line_offset = std::max<int>(0, view_line_offset - lines_removed);
		return true;
	}

	std::string BuildFooterText(AppContext& ctx) const {
		std::string status;
		auto state = AppService::GetInstance().GetDeviceState();
		if (recording) {
			status = "录音中";
		} else if (state == kEteacherStateListening) {
			status = "聆听中";
		} else if (state == kEteacherStateSpeaking) {
			status = "说话中";
		}

		std::string footer = "A按住说话  上下选  B翻译  D朗读  左右翻页";
		if (!status.empty()) {
			footer += " | ";
			footer += status;
		}

		auto* epd = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());
		if (!epd) {
			return footer;
		}
		const int max_width = epd->width() - eteacher::app_menu::MenuStyle{}.padding * 2;
		if (epd->MeasureUtf8Width(footer, kStatusFont) <= max_width) {
			return footer;
		}
		std::string trimmed = footer;
		while (!trimmed.empty() && epd->MeasureUtf8Width(trimmed + "...", kStatusFont) > max_width) {
			trimmed.pop_back();
		}
		return trimmed + "...";
	}

	eteacher::app_menu::MenuStatus BuildStatus(AppContext& ctx) {
		return eteacher::app_ui::BuildMenuStatus(ctx.board);
	}

	void Render(AppContext& ctx) {
		auto* epd = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());
		if (!epd) {
			RenderTextFallback(ctx);
			return;
		}

		EnsureAvatarsLoaded();

		const int screen_w = epd->width();
		const int screen_h = epd->height();
		if (screen_w != last_screen_w || screen_h != last_screen_h) {
			last_screen_w = screen_w;
			last_screen_h = screen_h;
			line_index_dirty = true;
		}
		if (line_index_dirty) {
			RebuildLineIndex();
			if (TrimHistoryToMaxLines()) {
				RebuildLineIndex();
			}
			line_index_dirty = false;
			if (scroll_to_latest_pending) {
				MoveCursorToLatest();
				scroll_to_latest_pending = false;
			}
		}
		const int text_font_height = GetFontHeight(kTextFont);
		const int text_font_ascent = GetFontAscent(kTextFont);
		const int line_height = text_font_height + kLineGap;
		const auto style = eteacher::app_menu::MenuStyle{};

		const int content_top = style.top_height + 2;
		const int content_bottom = screen_h - style.bottom_height - 2;
		visible_lines = std::max<int>(1, (content_bottom - content_top) / std::max(1, line_height));
		if (view_line_offset < 0) {
			view_line_offset = 0;
		}
		if (view_line_offset > std::max<int>(0, static_cast<int>(line_to_entry.size()) - visible_lines)) {
			view_line_offset = std::max<int>(0, static_cast<int>(line_to_entry.size()) - visible_lines);
		}
		EnsureCursorVisible();

		auto* snap = AcquireSnapshot();
		if (!snap) {
			return;
		}
		snap->epd = epd;
		snap->teacher_avatar = teacher_avatar;
		snap->student_avatar = student_avatar;
		snap->screen_w = screen_w;
		snap->screen_h = screen_h;
		snap->content_top = content_top;
		snap->content_bottom = content_bottom;
		snap->line_height = line_height;
		snap->text_font_ascent = text_font_ascent;
		snap->text_font_height = text_font_height;
		snap->view_line_offset = view_line_offset;
		snap->visible_lines = visible_lines;
		snap->selected_entry = GetSelectedEntry();
		if (snap->selected_entry >= 0 && snap->selected_entry < static_cast<int>(entry_start_line.size())) {
			const int start = entry_start_line[snap->selected_entry];
			const auto& entry = entries[snap->selected_entry];
			const auto& lines = (entry.show_translation && !entry.translated_lines.empty()) ? entry.translated_lines : entry.lines;
			const int count = std::max<int>(1, static_cast<int>(lines.size()));
			snap->selected_entry_first_line = start;
			snap->selected_entry_last_line = start + count - 1;
		}
		snap->status = BuildStatus(ctx);
		snap->footer_text = BuildFooterText(ctx);

		const int total_lines = static_cast<int>(line_to_entry.size());
		const int end_line = std::min<int>(total_lines, view_line_offset + visible_lines);
		for (int line_idx = view_line_offset; line_idx < end_line; ++line_idx) {
			const int entry_idx = line_to_entry[line_idx];
			if (entry_idx < 0 || entry_idx >= static_cast<int>(entries.size())) {
				continue;
			}
			const auto& entry = entries[entry_idx];
			const auto& lines = (entry.show_translation && !entry.translated_lines.empty()) ? entry.translated_lines : entry.lines;
			const int line_in_entry = line_idx - entry_start_line[entry_idx];
			RenderLine rl;
			rl.entry_index = entry_idx;
			if (line_in_entry >= 0 && line_in_entry < static_cast<int>(lines.size())) {
				rl.text = lines[line_in_entry];
			} else if (!lines.empty()) {
				rl.text = lines.front();
			}
			rl.is_teacher = entry.is_teacher;
			rl.total_lines_in_entry = std::max<int>(1, static_cast<int>(lines.size()));
			snap->lines.push_back(std::move(rl));
		}

		EpdManager::GetInstance().Schedule(
			EpdManager::TaskType::kPartial,
			&DrawConversationCb,
			snap,
			&DeleteSnapshot,
			EpdManager::Rect(0, 0, screen_w, screen_h));
	}

	static void DrawConversationCb(Adafruit_GFX& gfx, void* ctx) {
		auto* snap = static_cast<RenderSnapshot*>(ctx);
		if (!snap || !snap->epd) {
			return;
		}
		auto* epd = snap->epd;
		const auto style = eteacher::app_menu::MenuStyle{};

		const int16_t screen_w = static_cast<int16_t>(snap->screen_w);
		const int16_t screen_h = static_cast<int16_t>(snap->screen_h);

		const int16_t frame_w = 400;
		const int16_t frame_h = 300;
		const int16_t frame_x = static_cast<int16_t>((screen_w - frame_w) / 2);
		const int16_t frame_y = static_cast<int16_t>((screen_h - frame_h) / 2);
		gfx.drawRect(frame_x, frame_y, frame_w, frame_h, GxEPD_BLACK);

		const int16_t status_font_ascent = GetFontAscent(kStatusFont);
		const int16_t status_font_height = GetFontHeight(kStatusFont);
		const int16_t status_baseline = static_cast<int16_t>((style.top_height - status_font_height) / 2 + status_font_ascent);

		const int16_t vol_batt_gap = 50;
		const int16_t batt_wifi_gap = 35;
		const int16_t status_right = static_cast<int16_t>(screen_w - style.padding);
		const int16_t volume_group_right = status_right;
		const int16_t battery_right = static_cast<int16_t>(status_right - vol_batt_gap);
		const int16_t wifi_right = static_cast<int16_t>(battery_right - batt_wifi_gap);

		BinImage volume_icon;
		if (LoadBinImage("volume.bin", &volume_icon) && volume_icon.data && volume_icon.width > 0) {
			const int16_t icon_w = static_cast<int16_t>(volume_icon.width);
			const int16_t icon_h = static_cast<int16_t>(volume_icon.height);
			const int16_t icon_text_gap = 4;
			const int16_t fixed_text_w = 20;
			const int16_t text_w = snap->status.volume_text.empty() ? 0 : fixed_text_w;
			const int16_t total_w = icon_w + (text_w > 0 ? (icon_text_gap + text_w) : 0);
			const int16_t icon_x = static_cast<int16_t>(volume_group_right - total_w);
			const int16_t icon_y = static_cast<int16_t>((style.top_height - icon_h) / 2);
			gfx.drawBitmap(icon_x, icon_y, volume_icon.data, icon_w, icon_h, GxEPD_BLACK);
			if (!snap->status.volume_text.empty()) {
				const int16_t area_x = static_cast<int16_t>(icon_x + icon_w + icon_text_gap);
				const int16_t area_right = static_cast<int16_t>(area_x + text_w);
				const int16_t measured_w = epd->MeasureUtf8Width(snap->status.volume_text, kStatusFont);
				const int16_t draw_x = static_cast<int16_t>(std::max<int16_t>(area_x, area_right - measured_w));
				epd->DrawUtf8(draw_x, status_baseline, snap->status.volume_text, kStatusFont, GxEPD_BLACK);
			}
		}

		DrawBatteryIcon(gfx, battery_right, 0, style.top_height, snap->status.battery_level);
		BinImage wifi_icon;
		if (LoadBinImage(snap->status.wifi_connected ? "wifi_on.bin" : "wifi_off.bin", &wifi_icon) && wifi_icon.data) {
			const int16_t icon_w = static_cast<int16_t>(wifi_icon.width);
			const int16_t icon_h = static_cast<int16_t>(wifi_icon.height);
			const int16_t icon_x = static_cast<int16_t>(wifi_right - icon_w);
			const int16_t icon_y = static_cast<int16_t>((style.top_height - icon_h) / 2);
			gfx.drawBitmap(icon_x, icon_y, wifi_icon.data, icon_w, icon_h, GxEPD_BLACK);
		}

		if (!snap->status.time_text.empty()) {
			epd->DrawUtf8(style.padding, status_baseline, snap->status.time_text, kStatusFont, GxEPD_BLACK);
		}

		gfx.drawFastHLine(0, static_cast<int16_t>(screen_h - style.bottom_height), screen_w, GxEPD_BLACK);

		if (!snap->footer_text.empty()) {
			const int16_t footer_font_ascent = GetFontAscent(kStatusFont);
			const int16_t footer_font_height = GetFontHeight(kStatusFont);
			const int16_t footer_baseline = static_cast<int16_t>(screen_h - (style.bottom_height - footer_font_height) / 2 - footer_font_height + footer_font_ascent);
			const int16_t footer_width = epd->MeasureUtf8Width(snap->footer_text, kStatusFont);
			const int16_t footer_x = static_cast<int16_t>((screen_w - footer_width) / 2);
			epd->DrawUtf8(footer_x, footer_baseline, snap->footer_text, kStatusFont, GxEPD_BLACK);
		}

		const int avatar_w_left = snap->teacher_avatar.width > 0 ? snap->teacher_avatar.width : 24;
		const int avatar_w_right = snap->student_avatar.width > 0 ? snap->student_avatar.width : 24;
		const int text_left_teacher = style.padding + avatar_w_left + kAvatarGap;
		const int student_avatar_x = screen_w - style.padding - avatar_w_right;
		const int student_text_right = student_avatar_x - kAvatarGap;
		const int student_text_left = style.padding;

		std::vector<bool> avatar_drawn;
		avatar_drawn.resize(std::max<int>(1, snap->selected_entry + 1), false);
		if (snap->selected_entry >= 0 && snap->selected_entry < static_cast<int>(avatar_drawn.size())) {
			avatar_drawn[snap->selected_entry] = false;
		}

		for (size_t i = 0; i < snap->lines.size(); ++i) {
			const auto& line = snap->lines[i];
			const int line_y = snap->content_top + static_cast<int>(i) * snap->line_height;
			const int baseline = line_y + snap->text_font_ascent;
			if (line.is_teacher) {
				if (line.entry_index >= 0) {
					if (line.entry_index >= static_cast<int>(avatar_drawn.size())) {
						avatar_drawn.resize(static_cast<size_t>(line.entry_index + 1), false);
					}
					if (!avatar_drawn[line.entry_index] && snap->teacher_avatar.data) {
						DrawAvatar(gfx, snap->teacher_avatar, style.padding, line_y);
						avatar_drawn[line.entry_index] = true;
					}
				}
				epd->DrawUtf8(text_left_teacher, baseline, line.text, kTextFont, GxEPD_BLACK);
			} else {
				if (line.entry_index >= 0) {
					if (line.entry_index >= static_cast<int>(avatar_drawn.size())) {
						avatar_drawn.resize(static_cast<size_t>(line.entry_index + 1), false);
					}
					if (!avatar_drawn[line.entry_index] && snap->student_avatar.data) {
						DrawAvatar(gfx, snap->student_avatar, student_avatar_x, line_y);
						avatar_drawn[line.entry_index] = true;
					}
				}
				int text_x = student_text_left;
				if (line.total_lines_in_entry == 1) {
					const int text_w = epd->MeasureUtf8Width(line.text, kTextFont);
					text_x = std::max<int>(student_text_left, student_text_right - text_w);
				}
				epd->DrawUtf8(text_x, baseline, line.text, kTextFont, GxEPD_BLACK);
			}
		}

		if (snap->selected_entry >= 0 && snap->selected_entry_first_line >= 0 && snap->selected_entry_last_line >= 0) {
			const int first_visible = std::max<int>(snap->selected_entry_first_line, snap->view_line_offset);
			const int last_visible = std::min<int>(snap->selected_entry_last_line, snap->view_line_offset + snap->visible_lines - 1);
			if (first_visible <= last_visible) {
				const int top_y = snap->content_top + (first_visible - snap->view_line_offset) * snap->line_height;
				const int bottom_y = snap->content_top + (last_visible - snap->view_line_offset) * snap->line_height + snap->text_font_height;
				const int16_t rect_x = style.padding - 2;
				const int16_t rect_y = static_cast<int16_t>(top_y - 2);
				const int16_t rect_w = static_cast<int16_t>(screen_w - style.padding * 2 + 4);
				const int16_t rect_h = static_cast<int16_t>(bottom_y - top_y + 4);
				DrawSelectionRect(gfx, rect_x, rect_y, rect_w, rect_h, 1);
			}
		}
	}

	void RenderTextFallback(AppContext& ctx) {
		std::string msg = "自由对话\n";
		for (const auto& entry : entries) {
			msg += entry.is_teacher ? "T: " : "S: ";
			msg += entry.show_translation && !entry.translated.empty() ? entry.translated : entry.text;
			msg += "\n";
		}
		msg += "\nA按住说话, B翻译, D朗读, 左右翻页";
		ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
	}
};

FreeConversationApp::FreeConversationApp() : impl_(std::make_unique<Impl>()) {}

FreeConversationApp::~FreeConversationApp() = default;

MenuMeta FreeConversationApp::GetMenuMeta() const {
	return MenuMeta{"free_conversation", "自由对话", "A 按住说话"};
}

void FreeConversationApp::OnEnter(AppContext &ctx) {
	impl_->ctx = &ctx;
	impl_->recording = false;
	impl_->last_state = AppService::GetInstance().GetDeviceState();
	impl_->EnsureAvatarsLoaded();
	impl_->InstallDisplayHook();
	impl_->Render(ctx);
}

void FreeConversationApp::OnExit(AppContext &ctx) {
	(void)ctx;
	impl_->RemoveDisplayHook();
	impl_->ctx = nullptr;
}

void FreeConversationApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
	bool need_render = false;
	switch (event.id) {
	case AppButton::Up:
		impl_->MoveCursorLine(-1);
		need_render = true;
		break;
	case AppButton::Down:
		impl_->MoveCursorLine(1);
		need_render = true;
		break;
	case AppButton::Left:
		impl_->PageScroll(-1);
		need_render = true;
		break;
	case AppButton::Right:
		impl_->PageScroll(1);
		need_render = true;
		break;
	case AppButton::A:
		if (event.action == ButtonAction::PressDown) {
			impl_->StartListening();
			need_render = true;
		} else if (event.action == ButtonAction::PressUp) {
			impl_->StopListening();
			need_render = true;
		}
		break;
	case AppButton::B:
		impl_->ToggleTranslationSelected();
		need_render = true;
		break;
	case AppButton::D:
		impl_->SpeakSelected();
		need_render = true;
		break;
	case AppButton::Select:
	case AppButton::Start:
	case AppButton::C:
	case AppButton::VolumeUp:
	case AppButton::VolumeDown:
	default:
		break;
	}

	if (need_render) {
		impl_->Render(ctx);
	}
}

void FreeConversationApp::OnTick(AppContext &ctx) {
	// AppManager provides a fixed 1s tick; emulate previous accumulation behavior.
	impl_->tick_accum_ms += 1000;
	if (impl_->tick_accum_ms < 500) {
		return;
	}
	impl_->tick_accum_ms = 0;

	const auto state = AppService::GetInstance().GetDeviceState();
	if (state != impl_->last_state) {
		impl_->last_state = state;
		impl_->Render(ctx);
		return;
	}

	const int minute_now = eteacher::app_ui::GetCurrentMinuteOfDay();
	if (minute_now >= 0 && minute_now != impl_->last_minute) {
		impl_->last_minute = minute_now;
		impl_->Render(ctx);
		return;
	}

	const bool wifi_connected = WifiManager::GetInstance().IsConnected();
	const int battery_level = eteacher::app_ui::GetBatteryLevelPercent(ctx.board);
	const std::string volume = eteacher::app_ui::FormatVolumeText(ctx.board);
	if (wifi_connected != impl_->last_wifi_connected || battery_level != impl_->last_battery_level || volume != impl_->last_volume_text) {
		impl_->last_wifi_connected = wifi_connected;
		impl_->last_battery_level = battery_level;
		impl_->last_volume_text = volume;
		impl_->Render(ctx);
	}
}

void FreeConversationApp::SetTranslator(std::shared_ptr<ConversationTranslator> translator) {
	impl_->translator = std::move(translator);
}

void FreeConversationApp::OnChatMessage(const char* role, const char* content) {
	(void)role;
	(void)content;
	// Intentionally no-op: chat messages are handled via CustomEpdDisplay::ChatMessageListener.
}

std::unique_ptr<AppBase> MakeFreeConversationApp() {
	return std::make_unique<FreeConversationApp>();
}
