#include "ui/epd_manager.h"

#include "board.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "assets.h"
#include "storage/sd_resource.h"
#include "settings.h"

#include "driver/spi_master.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <new>

// EPD helpers (DrawMixedString) - optional
#include "ui/epd_renderer.h"

static inline bool IsUtf8ContinuationByte(unsigned char b) {
    return (b & 0xC0) == 0x80;
}

// Robust UTF-8 decode for layout/wrapping:
// - Always advances by >=1 when input remains
// - Collapses invalid sequences (including stray continuation runs) into ONE replacement
// - Marks replacement as "wide" to avoid undercounting width
static uint32_t DecodeUtf8ForWrap(const char* buf, size_t size, size_t& i, size_t& out_bytes, bool& out_replacement, bool& out_wide_replacement) {
    out_bytes = 0;
    out_replacement = false;
    out_wide_replacement = false;
    if (!buf || i >= size) {
        return 0;
    }

    const size_t start = i;
    const unsigned char b0 = static_cast<unsigned char>(buf[i]);
    if (b0 < 0x80) {
        i += 1;
        out_bytes = 1;
        return b0;
    }

    // Stray continuation bytes: consume the whole run as a single replacement.
    if (IsUtf8ContinuationByte(b0)) {
        i += 1;
        while (i < size && IsUtf8ContinuationByte(static_cast<unsigned char>(buf[i]))) {
            i += 1;
        }
        out_bytes = i - start;
        out_replacement = true;
        out_wide_replacement = true;
        return 0xFFFD;
    }

    int expected = 0;
    uint32_t min_cp = 0;
    if ((b0 & 0xE0) == 0xC0) {
        expected = 2;
        min_cp = 0x80;
    } else if ((b0 & 0xF0) == 0xE0) {
        expected = 3;
        min_cp = 0x800;
    } else if ((b0 & 0xF8) == 0xF0) {
        expected = 4;
        min_cp = 0x10000;
    } else {
        // Invalid leading byte.
        i += 1;
        out_bytes = 1;
        out_replacement = true;
        out_wide_replacement = true;
        return 0xFFFD;
    }

    // Consume up to expected bytes, but stop early if continuation bytes are missing.
    size_t end = start + 1;
    while (end < size && (end - start) < static_cast<size_t>(expected) && IsUtf8ContinuationByte(static_cast<unsigned char>(buf[end]))) {
        end += 1;
    }

    if ((end - start) != static_cast<size_t>(expected)) {
        // Incomplete or malformed sequence: consume what we have so far (>=1) as one replacement.
        i = end;
        out_bytes = i - start;
        out_replacement = true;
        out_wide_replacement = true;
        return 0xFFFD;
    }

    uint32_t cp = 0;
    if (expected == 2) {
        cp = ((b0 & 0x1F) << 6) | (static_cast<unsigned char>(buf[start + 1]) & 0x3F);
    } else if (expected == 3) {
        cp = ((b0 & 0x0F) << 12) |
             ((static_cast<unsigned char>(buf[start + 1]) & 0x3F) << 6) |
             (static_cast<unsigned char>(buf[start + 2]) & 0x3F);
    } else {
        cp = ((b0 & 0x07) << 18) |
             ((static_cast<unsigned char>(buf[start + 1]) & 0x3F) << 12) |
             ((static_cast<unsigned char>(buf[start + 2]) & 0x3F) << 6) |
             (static_cast<unsigned char>(buf[start + 3]) & 0x3F);
    }

    // Basic validity checks: overlong, surrogate, and out-of-range.
    const bool invalid = (cp < min_cp) || (cp > 0x10FFFF) || (cp >= 0xD800 && cp <= 0xDFFF);
    i = start + static_cast<size_t>(expected);
    out_bytes = static_cast<size_t>(expected);
    if (invalid) {
        out_replacement = true;
        out_wide_replacement = true;
        return 0xFFFD;
    }

    return cp;
}

namespace {
constexpr size_t kCommandQueueLength = 10;
constexpr TickType_t kQueueWaitTicks = pdMS_TO_TICKS(100);
constexpr uint32_t kTaskStackSize = 4096;
constexpr UBaseType_t kTaskPriority = 4;
constexpr const char* kUserAvatarPath = "resource/image/student.bin";
constexpr const char* kAiAvatarPath = "resource/image/teacher.bin";

// Wenquanyi BDF binary fonts stored in the firmware "assets" partition (SPIFFS image).
// Note: assets packing currently flattens directories, so the asset name is the filename.
static int ClampWqyPt(int pt) {
    if (pt < 9) return 9;
    if (pt > 13) return 13;
    return pt;
}

static EpdRenderer::FontPt ToFontPt(int pt) {
    switch (ClampWqyPt(pt)) {
        case 9:  return EpdRenderer::FontPt::k9;
        case 10: return EpdRenderer::FontPt::k10;
        case 11: return EpdRenderer::FontPt::k11;
        case 12: return EpdRenderer::FontPt::k12;
        case 13: return EpdRenderer::FontPt::k13;
        default: return EpdRenderer::FontPt::k11;
    }
}

static void MakeWqyAssetName(int pt, bool use_px_suffix, char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    const int safe_pt = ClampWqyPt(pt);
    // Supports both naming rules:
    // - wenquanyi_11pt.bin (preferred)
    // - wenquanyi_11px.bin (fallback; some generators use px)
    snprintf(out, out_size, "wenquanyi_%d%s.bin", safe_pt, use_px_suffix ? "px" : "pt");
}

static int AsciiWidthFromChineseWidth(int chinese_width) {
    // Simple heuristic for chat layout (roughly half-width Latin glyphs).
    return std::max(1, (chinese_width + 1) / 2);
}

static void MetricsForWenquanyiPt(EpdRenderer::FontPt font_pt, int& out_chinese_width, int& out_ascii_width, int& out_line_height) {
    // User-provided Wenquanyi metrics table (width * height):
    // 9pt  12*14
    // 10pt 13*14
    // 11pt 16*18
    // 12pt 16*19
    // 13pt 14*15
    int w = 16;
    int h = 18;
    switch (font_pt) {
        case EpdRenderer::FontPt::k9:  w = 12; h = 14; break;
        case EpdRenderer::FontPt::k10: w = 13; h = 14; break;
        case EpdRenderer::FontPt::k11: w = 16; h = 18; break;
        case EpdRenderer::FontPt::k12: w = 16; h = 19; break;
        case EpdRenderer::FontPt::k13: w = 14; h = 15; break;
        default: w = 16; h = 18; break;
    }
    out_chinese_width = w;
    out_line_height = h;
    out_ascii_width = AsciiWidthFromChineseWidth(w);
}
}

struct EpdManager::Command {
    enum class Type {
        SHOW_MAIN_MENU_DEFAULT,
        SHOW_MAIN_MENU_DYNAMIC,
        SHOW_WORD_CARD,
        UPDATE_CONVERSATION,
        SET_ACTIVE_SCREEN,
        SET_BUTTON_HINTS,
        DRAW_BUTTON_HINTS,
    } type;

    std::vector<std::string> menu_items;
    int selected_index = 0;
    bool is_user = false;
    std::string text;
    std::string card_html;
    std::array<std::string, EpdManager::kButtonCount> hints = {"", "", "", "", "", ""};
    int screen_id = 0;
};

static const char* TAG = "EpdManager";

EpdManager& EpdManager::GetInstance() {
    static EpdManager inst;
    return inst;
}

void EpdManager::Init() {
    if (initialized_) {
        ESP_LOGI(TAG, "EpdManager already initialized");
        return;
    }

    ESP_LOGI(TAG, "EpdManager init");
    EpdRenderer::Init();
    EnsureTaskCreated();

    if (command_queue_ && task_handle_) {
        initialized_ = true;
    } else {
        ESP_LOGE(TAG, "Failed to launch EPD command task");
    }
}

void EpdManager::EnsureTaskCreated() {
    if (!command_queue_) {
        command_queue_ = xQueueCreate(kCommandQueueLength, sizeof(Command*));
        if (!command_queue_) {
            ESP_LOGE(TAG, "Failed to create EPD command queue");
            return;
        }
    }

    if (!task_handle_) {
        BaseType_t res = xTaskCreate(&EpdManager::TaskEntry, "epd_mgr", kTaskStackSize, this, kTaskPriority, &task_handle_);
        if (res != pdPASS) {
            ESP_LOGE(TAG, "Failed to create EPD task (%d)", res);
            task_handle_ = nullptr;
        }
    }
}

void EpdManager::TaskEntry(void* arg) {
    auto* self = static_cast<EpdManager*>(arg);
    if (self) {
        self->TaskLoop();
    }
    vTaskDelete(nullptr);
}

void EpdManager::TaskLoop() {
    while (true) {
        Command* raw_cmd = nullptr;
        if (xQueueReceive(command_queue_, &raw_cmd, portMAX_DELAY) == pdTRUE && raw_cmd) {
            ProcessCommand(*raw_cmd);
            delete raw_cmd;
        }
    }
}

bool EpdManager::LoadAvatarFromSd(const std::string& relative_path, AvatarData& dst, const char* label) {
    const char* safe_label = label ? label : "unknown";

    std::vector<uint8_t> buf;
    if (!SdResource::GetInstance().LoadBinary(relative_path, buf, safe_label)) {
        dst.ok = false;
        dst.buffer.clear();
        dst.data = nullptr;
        dst.size = 0;
        return false;
    }

    dst.buffer = std::move(buf);
    dst.size = dst.buffer.size();
    dst.data = dst.buffer.empty() ? nullptr : dst.buffer.data();
    dst.ok = dst.data != nullptr && dst.size > 0;

    if (!dst.ok) {
        ESP_LOGW(TAG, "%s avatar data pointer is null after load", safe_label);
        return false;
    }

    ESP_LOGW(TAG, "%s avatar load success path=%s size=%zu ptr=%p", safe_label, relative_path.c_str(), dst.size, dst.data);
    return true; 
}

void EpdManager::EnsureAvatarsLoaded() {
    if (!user_avatar_.ok) {
        LoadAvatarFromSd(kUserAvatarPath, user_avatar_, "user");
    }
    if (!ai_avatar_.ok) {
        LoadAvatarFromSd(kAiAvatarPath, ai_avatar_, "ai");
    }
}

void EpdManager::DispatchCommand(Command* cmd) {
    if (!cmd) {
        return;
    }
    EnsureTaskCreated();
    if (!command_queue_ || !task_handle_) {
        ProcessCommand(*cmd);
        delete cmd;
        return;
    }
    if (xQueueSend(command_queue_, &cmd, kQueueWaitTicks) != pdPASS) {
        ESP_LOGW(TAG, "EPD queue busy, discard latest command");
        delete cmd;
    }
}

void EpdManager::ProcessCommand(Command& cmd) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    LogSystemStatus();

    switch (cmd.type) {
        case Command::Type::SHOW_MAIN_MENU_DEFAULT:
            display->SetChatMessage("system", "English Teacher - Main Menu");
            break;
        case Command::Type::SHOW_MAIN_MENU_DYNAMIC: {
            std::string buf;
            for (size_t i = 0; i < cmd.menu_items.size(); ++i) {
                buf += ((int)i == cmd.selected_index) ? "> " : "  ";
                buf += cmd.menu_items[i];
                if (i + 1 < cmd.menu_items.size()) buf += "\n";
            }
            if (EpdRenderer::Available()) {
                const int64_t now_us = esp_timer_get_time();
                if (IsRefreshAllowed(now_us)) {
                    EpdRenderer::DrawText(buf, 0, 10, EpdRenderer::FontSize::k16);
                    EpdRenderer::Display(true);
                    UpdateLastRefreshTime(now_us);
                }
            } else {
                display->SetChatMessage("system", buf.c_str());
            }
            break;
        }
        case Command::Type::SHOW_WORD_CARD: {
            if (EpdRenderer::Available()) {
                const int64_t now_us = esp_timer_get_time();
                if (IsRefreshAllowed(now_us)) {
                    std::string plain = cmd.card_html;
                    std::string out;
                    size_t p = 0;
                    while (p < plain.size()) {
                        if (plain[p] == '<') {
                            while (p < plain.size() && plain[p] != '>') ++p;
                            if (p < plain.size()) ++p;
                        } else {
                            out += plain[p++];
                        }
                    }
                    EpdRenderer::DrawText(out, 0, 20, EpdRenderer::FontSize::k16);
                    EpdRenderer::Display(true);
                    UpdateLastRefreshTime(now_us);
                }
            } else {
                display->SetChatMessage("system", cmd.card_html.c_str());
            }
            break;
        }
        case Command::Type::UPDATE_CONVERSATION: {
            std::string safe_text = cmd.text;
            if (safe_text.empty()) safe_text = "(empty)";
            ESP_LOGI(TAG, "Conversation %s: text='%s'",
                cmd.is_user ? "user" : "assistant",
                safe_text.c_str());

            static bool avatars_ready = false;
            if (!avatars_ready) {
                EnsureAvatarsLoaded();
                avatars_ready = true;
            }

            const int64_t now_us = esp_timer_get_time();

            constexpr int region_width = 400;
            constexpr int region_height = 300;
            constexpr int bubble_padding_y = 1;
            constexpr int bubble_gap_y = 3;
            constexpr int margin_x = 8;
            constexpr int margin_y = 8;
            constexpr int max_history_lines = 50;

            constexpr int avatar_ai_w = 20;
            constexpr int avatar_ai_h = 20;
            constexpr int avatar_user_w = 20;
            constexpr int avatar_user_h = 20;
            constexpr int gap_ai_text = 5;
            constexpr int gap_user_text = 5;

            // BDF font selection is controlled by EpdRenderer::FontPt (5 sizes).
            // It can be changed by setting NVS key epd.wqy_pt=9..13.
            {
                Settings epd_settings("epd", false);
                //const int cfg_pt = epd_settings.GetInt("wqy_pt", 0);
                const int cfg_pt = 11;
                if (cfg_pt >= 9 && cfg_pt <= 13) {
                    EpdRenderer::SetBdfFontPt(ToFontPt(cfg_pt));
                }
            }

            const auto bdf_font_pt = EpdRenderer::GetBdfFontPt();

            // Layout metrics (used for wrapping and baseline).
            // Use the explicit per-pt Wenquanyi width/height table.
            struct FontMetrics { int chinese_width; int ascii_width; int line_height; };
            FontMetrics metrics{};
            MetricsForWenquanyiPt(bdf_font_pt, metrics.chinese_width, metrics.ascii_width, metrics.line_height);

            const int line_height = metrics.line_height;

            static bool bdf_font_ok = false;
            if (EpdRenderer::Available()) {
                static char loaded_bdf_asset_name[32] = {0};
                static EpdRenderer::FontPt loaded_bdf_font_pt = EpdRenderer::FontPt::k11;
                static bool loaded_bdf_font_pt_valid = false;

                char desired_asset_buf[32];
                char alt_asset_buf[32];
                MakeWqyAssetName(static_cast<int>(bdf_font_pt), false, desired_asset_buf, sizeof(desired_asset_buf));
                MakeWqyAssetName(static_cast<int>(bdf_font_pt), true, alt_asset_buf, sizeof(alt_asset_buf));

                const bool pt_changed = (!loaded_bdf_font_pt_valid) || (loaded_bdf_font_pt != bdf_font_pt);
                if (!EpdRenderer::BdfIsLoaded() || !bdf_font_ok || pt_changed) {
                    void* font_ptr = nullptr;
                    size_t font_size_bytes = 0;

                    const char* asset_name_used = desired_asset_buf;
                    bool found = Assets::GetInstance().GetAssetData(desired_asset_buf, font_ptr, font_size_bytes) && font_ptr && font_size_bytes > 0;
                    if (!found) {
                        // Fallback: some generators produce "px" names.
                        asset_name_used = alt_asset_buf;
                        found = Assets::GetInstance().GetAssetData(alt_asset_buf, font_ptr, font_size_bytes) && font_ptr && font_size_bytes > 0;
                    }

                    if (found) {
                        bdf_font_ok = EpdRenderer::BdfLoadFont(font_ptr, font_size_bytes);
                        strncpy(loaded_bdf_asset_name, asset_name_used, sizeof(loaded_bdf_asset_name));
                        loaded_bdf_asset_name[sizeof(loaded_bdf_asset_name) - 1] = '\0';
                        loaded_bdf_font_pt = bdf_font_pt;
                        loaded_bdf_font_pt_valid = true;
                        ESP_LOGI(TAG, "BDF font load from assets: name=%s size=%u ok=%d", asset_name_used, (unsigned)font_size_bytes, (int)bdf_font_ok);
                    } else {
                        bdf_font_ok = false;
                        loaded_bdf_asset_name[0] = '\0';
                        loaded_bdf_font_pt_valid = false;
                        ESP_LOGW(TAG, "BDF font not found in assets: %s", desired_asset_buf);
                    }
                }
            }
            const bool use_bdf_font = bdf_font_ok && EpdRenderer::BdfIsLoaded();

            const int text_area_left = margin_x + avatar_ai_w + gap_ai_text;
            const int text_area_right = region_width - margin_x - avatar_user_w - gap_user_text;
            const int text_area_width = std::max(1, text_area_right - text_area_left);

            auto char_width_estimate = [&](const uint32_t cp) {
                if (cp < 0x80) {
                    return metrics.ascii_width;
                }
                if (cp >= 0x1F000) {
                    return metrics.chinese_width * 2;
                }
                if ((cp >= 0x1100 && cp <= 0x11FF) ||
                    (cp >= 0x2E80 && cp <= 0xA4CF) ||
                    (cp >= 0xAC00 && cp <= 0xD7AF)) {
                    return metrics.chinese_width;
                }
                if ((cp >= 0x00A0 && cp <= 0x04FF) || (cp >= 0x1E00 && cp <= 0x1FFF)) {
                    return metrics.ascii_width + std::max(0, metrics.chinese_width - metrics.ascii_width) / 2;
                }
                return metrics.chinese_width;
            };

            auto char_width_for = [&](const uint32_t cp) {
                const int fallback = char_width_estimate(cp);
                if (use_bdf_font) {
                    // Use actual glyph advance when available.
                    return EpdRenderer::BdfGlyphAdvance(cp, fallback);
                }
                return fallback;
            };

            auto wrap_entry = [&](ConversationEntry& entry, const std::string& text) {
                entry.line_count = 0;
                char current_line[kMaxLineLen] = {0};
                int current_len = 0;
                int current_width = 0;
                int last_space_pos = -1;
                const char* data = text.data();
                const size_t text_size = text.size();
                size_t idx = 0;
                const auto clamp_line_width = [&](int width) {
                    return std::max(0, std::min(width, text_area_width));
                };

                const auto recompute_line_state = [&](const char* buf, size_t len, int& out_width, int& out_last_space_pos) {
                    out_width = 0;
                    out_last_space_pos = -1;
                    size_t scan = 0;
                    while (scan < len) {
                        const size_t prev = scan;
                        size_t bytes = 0;
                        bool repl = false;
                        bool wide_repl = false;
                        const uint32_t cp = DecodeUtf8ForWrap(buf, len, scan, bytes, repl, wide_repl);
                        if (bytes == 0) {
                            break;
                        }
                        if (cp == ' ' || cp == '\t') {
                            out_last_space_pos = static_cast<int>(prev);
                        }
                        const int cw = (repl && wide_repl) ? metrics.chinese_width : char_width_for(cp);
                        out_width += cw;
                    }
                };

                const auto compute_width_prefix = [&](const char* buf, size_t len) {
                    int width = 0;
                    int dummy_space = -1;
                    recompute_line_state(buf, len, width, dummy_space);
                    return width;
                };

                const auto is_ascii_space_or_tab_at = [&](const char* buf, size_t pos, size_t len) {
                    if (!buf || pos >= len) {
                        return false;
                    }
                    return buf[pos] == ' ' || buf[pos] == '\t';
                };

                const auto trim_trailing_spaces = [&](const char* buf, size_t len) {
                    size_t trimmed = len;
                    while (trimmed > 0 && (buf[trimmed - 1] == ' ' || buf[trimmed - 1] == '\t')) {
                        trimmed -= 1;
                    }
                    return trimmed;
                };

                auto flush_current = [&]() {
                    if (current_len == 0) {
                        return;
                    }
                    const size_t trimmed_len = trim_trailing_spaces(current_line, static_cast<size_t>(current_len));
                    const int trimmed_width = compute_width_prefix(current_line, trimmed_len);
                    AppendLineToEntry(entry, current_line, trimmed_len, clamp_line_width(trimmed_width));
                    current_len = 0;
                    current_width = 0;
                    last_space_pos = -1;
                    memset(current_line, 0, kMaxLineLen);
                };

                while (idx < text_size && entry.line_count < kMaxLinesPerMessage) {
                    const size_t start = idx;
                    size_t bytes = 0;
                    bool repl = false;
                    bool wide_repl = false;
                    const uint32_t cp = DecodeUtf8ForWrap(data, text_size, idx, bytes, repl, wide_repl);
                    if (bytes == 0) {
                        break;
                    }
                    if (cp == '\r') {
                        continue;
                    }
                    if (cp == '\n') {
                        flush_current();
                        continue;
                    }

                    const int cw = (repl && wide_repl) ? metrics.chinese_width : char_width_for(cp);
                    const bool is_space = (cp == ' ' || cp == '\t');

                    // Avoid lines that start with invisible width.
                    if (current_len == 0 && is_space) {
                        continue;
                    }

                    if (current_width + cw > text_area_width && current_len > 0) {
                        if (last_space_pos >= 0) {
                            // Trim any trailing whitespace before the chosen break.
                            size_t break_len = static_cast<size_t>(last_space_pos);
                            break_len = trim_trailing_spaces(current_line, break_len);
                            const int break_width = compute_width_prefix(current_line, break_len);
                            AppendLineToEntry(entry, current_line, break_len, clamp_line_width(break_width));

                            // Skip ALL consecutive spaces/tabs after the break so we don't carry invisible width.
                            size_t skip = static_cast<size_t>(last_space_pos) + 1;
                            while (skip < static_cast<size_t>(current_len) && is_ascii_space_or_tab_at(current_line, skip, static_cast<size_t>(current_len))) {
                                skip += 1;
                            }

                            const size_t remaining = static_cast<size_t>(current_len) - skip;
                            if (remaining > 0) {
                                memmove(current_line, current_line + skip, remaining);
                            }
                            current_len = static_cast<int>(remaining);
                            // Recompute width and last break point for the remaining buffer.
                            recompute_line_state(current_line, static_cast<size_t>(current_len), current_width, last_space_pos);
                        } else {
                            flush_current();
                        }
                    }

                    if (current_len + static_cast<int>(bytes) >= kMaxLineLen) {
                        flush_current();
                        if (bytes >= static_cast<size_t>(kMaxLineLen)) {
                            continue;
                        }
                    }

                    if (current_len + static_cast<int>(bytes) >= kMaxLineLen) {
                        continue;
                    }

                    memcpy(current_line + current_len, data + start, bytes);
                    const int pos_before = current_len;
                    current_len += static_cast<int>(bytes);
                    current_width += cw;
                    if (is_space) {
                        last_space_pos = pos_before;
                    }
                }

                if (current_len > 0 && entry.line_count < kMaxLinesPerMessage) {
                    const size_t trimmed_len = trim_trailing_spaces(current_line, static_cast<size_t>(current_len));
                    const int trimmed_width = compute_width_prefix(current_line, trimmed_len);
                    AppendLineToEntry(entry, current_line, trimmed_len, clamp_line_width(trimmed_width));
                }
                if (entry.line_count == 0) {
                    AppendLineToEntry(entry, nullptr, 0, 0);
                }
            };

            if (conversation_count_ == kMaxConversationHistory) {
                RemoveOldestEntry();
            }
            const size_t insert_index = HistoryIndex(conversation_count_);
            std::memset(&conversation_history_[insert_index], 0, sizeof(ConversationEntry));
            auto& new_entry = conversation_history_[insert_index];
            new_entry.is_user = cmd.is_user;
            wrap_entry(new_entry, cmd.text);
            if (conversation_count_ < kMaxConversationHistory) {
                ++conversation_count_;
            }
            total_history_lines_ += new_entry.line_count;

            while (total_history_lines_ > max_history_lines && conversation_count_ > 0) {
                RemoveOldestEntry();
            }

            if (!IsRefreshAllowed(now_us)) {
                return;
            }

            size_t render_count = 0;

            int used_height = 0;
            for (size_t rel = 0; rel < conversation_count_; ++rel) {
                const size_t history_idx = (conversation_start_ + conversation_count_ - 1 - rel + kMaxConversationHistory) % kMaxConversationHistory;
                const auto& entry = conversation_history_[history_idx];
                const int line_count = std::max(1, (int)entry.line_count);
                const int text_height = line_count * line_height;
                const int avatar_h = entry.is_user ? avatar_user_h : avatar_ai_h;
                const int content_h = std::max(text_height, avatar_h);
                const int bubble_height = bubble_padding_y * 2 + content_h;
                const int needed = (render_count == 0 ? 0 : bubble_gap_y) + bubble_height;
                if (used_height + needed > region_height - margin_y * 2) {
                    break;
                }
                render_items_[render_count++] = {history_idx, text_height, bubble_height};
                used_height += needed;
            }

            EpdRenderer::Clear();
            int cursor_y = margin_y;
            for (size_t render_i = render_count; render_i > 0; --render_i) {
                const auto& item = render_items_[render_i - 1];
                const auto& entry = conversation_history_[item.index];
                const bool is_user = entry.is_user;
                const int avatar_w = is_user ? avatar_user_w : avatar_ai_w;
                const int avatar_h = is_user ? avatar_user_h : avatar_ai_h;
                const int avatar_x = is_user ? (region_width - margin_x - avatar_w) : margin_x;
                const int avatar_y = cursor_y + bubble_padding_y;

                if (is_user) {
                    if (user_avatar_.ok && user_avatar_.data && user_avatar_.size > 0) {
                        EpdRenderer::DrawBitmap(user_avatar_.data, avatar_x, avatar_y, avatar_w, avatar_h, 0);
                    } else if (!warned_user_avatar_) {
                        ESP_LOGW(TAG, "User avatar not drawn (asset missing or invalid)");
                        warned_user_avatar_ = true;
                    }
                } else {
                    if (ai_avatar_.ok && ai_avatar_.data && ai_avatar_.size > 0) {
                        EpdRenderer::DrawBitmap(ai_avatar_.data, avatar_x, avatar_y, avatar_w, avatar_h, 0);
                    } else if (!warned_ai_avatar_) {
                        ESP_LOGW(TAG, "AI avatar not drawn (asset missing or invalid)");
                        warned_ai_avatar_ = true;
                    }
                }

                const int text_x_left = text_area_left;
                const int text_x_right = text_area_right;
                const int text_x_left_user = text_x_right - text_area_width;
                const int content_height = std::max(item.text_height, avatar_h);
                int text_y = avatar_y + std::max(0, (content_height - item.text_height) / 2);

                const bool single_line_user = is_user && entry.line_count <= 1;

                for (size_t line_index = 0; line_index < entry.line_count; ++line_index) {
                    const int line_px = entry.line_width_px[line_index];

                    int text_x;
                    if (is_user) {
                        if (single_line_user) {
                            text_x = std::max(text_x_left_user, text_x_right - line_px);
                        } else {
                            text_x = text_x_left_user;
                        }
                    } else {
                        text_x = text_x_left;
                    }

                    if (use_bdf_font) {
                        // BDF drawing uses baseline Y; treat text_y as top of the line.
                        const int baseline_y = text_y + line_height;
                        EpdRenderer::DrawBdfText(entry.lines[line_index], text_x, baseline_y, 0);
                    } else {
                        // GT30 fallback only supports 12/16/24/32.
                        const auto font_size = (line_height <= 14) ? EpdRenderer::FontSize::k12
                                          : (line_height <= 18) ? EpdRenderer::FontSize::k16
                                          : (line_height <= 24) ? EpdRenderer::FontSize::k24
                                                               : EpdRenderer::FontSize::k32;
                        EpdRenderer::DrawText(entry.lines[line_index], text_x, text_y, font_size);
                    }
                    text_y += line_height;
                }

                cursor_y += item.bubble_height + bubble_gap_y;
            }

            int window_height = std::min(region_height, cursor_y + margin_y);
            EpdRenderer::DisplayWindow(0, 0, region_width, region_height, true);
            UpdateLastRefreshTime(now_us);
            break;
        }
        case Command::Type::SET_ACTIVE_SCREEN:
            active_screen_ = cmd.screen_id;
            break;
        case Command::Type::SET_BUTTON_HINTS:
            button_hints_ = cmd.hints;
            break;
        case Command::Type::DRAW_BUTTON_HINTS: {
            std::string line;
            for (int i = 0; i < kButtonCount; ++i) {
                if (!button_hints_[i].empty()) {
                    if (!line.empty()) line += " | ";
                    line += "B" + std::to_string(i + 1) + ":" + button_hints_[i];
                }
            }

            if (line.empty()) {
                if (!EpdRenderer::Available()) {
                    display->SetChatMessage("system", "");
                }
            } else {
                if (EpdRenderer::Available()) {
                    const int64_t now_us = esp_timer_get_time();
                    if (IsRefreshAllowed(now_us)) {
                        EpdRenderer::DrawText(line, 0, display->height() - 40, EpdRenderer::FontSize::k16);
                        EpdRenderer::Display(true);
                        UpdateLastRefreshTime(now_us);
                    }
                } else {
                    display->SetChatMessage("system", line.c_str());
                }
            }
            break;
        }
    }
}

void EpdManager::ShowMainMenu() {
    Command* cmd = new (std::nothrow) Command();
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to allocate command for ShowMainMenu");
        return;
    }
    cmd->type = Command::Type::SHOW_MAIN_MENU_DEFAULT;
    DispatchCommand(cmd);
}

void EpdManager::ShowMainMenu(const std::vector<std::string>& items, int selected_index) {
    Command* cmd = new (std::nothrow) Command();
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to allocate command for ShowMainMenu(items)");
        return;
    }
    cmd->type = Command::Type::SHOW_MAIN_MENU_DYNAMIC;
    cmd->menu_items = items;
    cmd->selected_index = selected_index;
    DispatchCommand(cmd);
}

void EpdManager::ShowWordCard(const std::string& card_html) {
    Command* cmd = new (std::nothrow) Command();
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to allocate command for ShowWordCard");
        return;
    }
    cmd->type = Command::Type::SHOW_WORD_CARD;
    cmd->card_html = card_html;
    DispatchCommand(cmd);
}

void EpdManager::UpdateConversation(bool is_user, const std::string& text) {
    Command* cmd = new (std::nothrow) Command();
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to allocate command for UpdateConversationSide");
        return;
    }
    cmd->type = Command::Type::UPDATE_CONVERSATION;
    cmd->is_user = is_user;
    cmd->text = text;
    DispatchCommand(cmd);
}

void EpdManager::SetRefreshIntervalMs(int interval_ms) {
    if (interval_ms < 0) {
        interval_ms = 0;
    }
    refresh_interval_ms_ = interval_ms;
}

void EpdManager::SetActiveScreen(int screen_id) {
    Command* cmd = new (std::nothrow) Command();
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to allocate command for SetActiveScreen");
        return;
    }
    cmd->type = Command::Type::SET_ACTIVE_SCREEN;
    cmd->screen_id = screen_id;
    DispatchCommand(cmd);
}

void EpdManager::SetButtonHints(const std::array<std::string, 6>& hints) {
    Command* cmd = new (std::nothrow) Command();
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to allocate command for SetButtonHints");
        return;
    }
    cmd->type = Command::Type::SET_BUTTON_HINTS;
    cmd->hints = hints;
    DispatchCommand(cmd);
}

void EpdManager::DrawButtonHints() {
    Command* cmd = new (std::nothrow) Command();
    if (!cmd) {
        ESP_LOGE(TAG, "Failed to allocate command for DrawButtonHints");
        return;
    }
    cmd->type = Command::Type::DRAW_BUTTON_HINTS;
    DispatchCommand(cmd);
}

size_t EpdManager::HistoryIndex(size_t relative_index) const {
    return (conversation_start_ + relative_index) % kMaxConversationHistory;
}

void EpdManager::RemoveOldestEntry() {
    if (conversation_count_ == 0) {
        return;
    }
    auto& oldest = conversation_history_[conversation_start_];
    total_history_lines_ -= oldest.line_count;
    oldest.line_count = 0;
    conversation_start_ = (conversation_start_ + 1) % kMaxConversationHistory;
    --conversation_count_;
}

void EpdManager::AppendLineToEntry(ConversationEntry& entry, const char* data, size_t len, int width) {
    if (entry.line_count >= kMaxLinesPerMessage) {
        return;
    }
    const size_t copy_len = std::min(len, static_cast<size_t>(kMaxLineLen) - 1);
    const size_t idx = entry.line_count;
    if (data && copy_len > 0) {
        memcpy(entry.lines[idx], data, copy_len);
    }
    entry.lines[idx][copy_len] = '\0';
    entry.line_length[idx] = static_cast<uint8_t>(copy_len);
    entry.line_width_px[idx] = static_cast<uint16_t>(std::max(0, width));
    entry.line_count = static_cast<uint8_t>(idx + 1);
}

bool EpdManager::IsRefreshAllowed(int64_t now_us) const {
    if (refresh_interval_ms_ <= 0) {
        return true;
    }
    if (last_refresh_time_us_ == 0) {
        return true;
    }
    const int64_t interval_us = static_cast<int64_t>(refresh_interval_ms_) * 1000;
    return now_us - last_refresh_time_us_ >= interval_us;
}

void EpdManager::UpdateLastRefreshTime(int64_t now_us) {
    last_refresh_time_us_ = now_us;
}

void EpdManager::LogSystemStatus() const {
    ESP_LOGI("SYS",
        "heap=%u min=%u stack=%u",
        esp_get_free_heap_size(),
        heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT),
        uxTaskGetStackHighWaterMark(NULL));
}
