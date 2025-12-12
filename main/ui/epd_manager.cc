#include "ui/epd_manager.h"

#include "board.h"
#include "display.h"
#include "esp_log.h"
#include "assets.h"
#include "storage/sd_resource.h"

#include "driver/spi_master.h"

#include <algorithm>
#include <new>
#include <unordered_map>

// EPD helpers (DrawMixedString) - optional
#include "ui/epd_renderer.h"

namespace {
constexpr size_t kCommandQueueLength = 10;
constexpr TickType_t kQueueWaitTicks = pdMS_TO_TICKS(100);
constexpr uint32_t kTaskStackSize = 4096;
constexpr UBaseType_t kTaskPriority = 4;
constexpr const char* kUserAvatarPath = "resource/image/student.bin";
constexpr const char* kAiAvatarPath = "resource/image/teacher.bin";
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
    // LoadAvatarFromSd(kUserAvatarPath, user_avatar_, "user");
    // LoadAvatarFromSd(kAiAvatarPath, ai_avatar_, "ai");
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
                EpdRenderer::DrawText(buf, 0, 10, EpdRenderer::FontSize::k16);
                EpdRenderer::Display(true);
            } else {
                display->SetChatMessage("system", buf.c_str());
            }
            break;
        }
        case Command::Type::SHOW_WORD_CARD: {
            if (EpdRenderer::Available()) {
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

            // Load avatars from SD so that 20x20 bitmaps can be drawn beside messages
            EnsureAvatarsLoaded();

            // Configurable area, avatars, and history caps
            constexpr int region_width = 400;    // 对话区域宽度，可调
            constexpr int region_height = 300;   // 对话区域高度，可调
            constexpr int bubble_padding_y = 0;  // 气泡内边距（垂直）
            constexpr int bubble_gap_y = 3;     // 气泡间垂直间距
            constexpr int margin_x = 8;         // 左右边距
            constexpr int margin_y = 8;         // 上下边距
            constexpr int max_history_lines = 160; // 历史总行数上限，可调

            // Avatar configuration
            constexpr int avatar_ai_w = 20;
            constexpr int avatar_ai_h = 20;
            constexpr int avatar_user_w = 20;
            constexpr int avatar_user_h = 20;
            constexpr int gap_ai_text = 5;    // AI头像与文字间距
            constexpr int gap_user_text = 5;  // 用户头像与文字间距

            const auto font_size = EpdRenderer::FontSize::k16;
            struct FontMetrics { int chinese_width; int ascii_width; int line_height; };
            const auto metrics = [&]() {
                switch (font_size) {
                    case EpdRenderer::FontSize::k12: return FontMetrics{12, 6, 12};
                    case EpdRenderer::FontSize::k16: return FontMetrics{16, 8, 16};
                    case EpdRenderer::FontSize::k24: return FontMetrics{24, 12, 24};
                    case EpdRenderer::FontSize::k32: return FontMetrics{32, 16, 32};
                    default: return FontMetrics{16, 8, 16};
                }
            }();

            const int line_height = metrics.line_height; // 行高与字体高度保持一致

            // Text区域留出左右头像和间隙
            const int text_area_left = margin_x + avatar_ai_w + gap_ai_text;
            const int text_area_right = region_width - margin_x - avatar_user_w - gap_user_text;
            const int text_area_width = std::max(1, text_area_right - text_area_left);
            auto decode_utf8 = [](const std::string& s, size_t& i) -> uint32_t {
                const unsigned char c0 = static_cast<unsigned char>(s[i]);
                if (c0 < 0x80) { i += 1; return c0; }
                if ((c0 >> 5) == 0x6 && i + 1 < s.size()) { // 2-byte
                    uint32_t cp = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
                    i += 2; return cp;
                }
                if ((c0 >> 4) == 0xE && i + 2 < s.size()) { // 3-byte
                    uint32_t cp = ((c0 & 0x0F) << 12) |
                                   ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                                   (static_cast<unsigned char>(s[i + 2]) & 0x3F);
                    i += 3; return cp;
                }
                if ((c0 >> 3) == 0x1E && i + 3 < s.size()) { // 4-byte
                    uint32_t cp = ((c0 & 0x07) << 18) |
                                   ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
                                   ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
                                   (static_cast<unsigned char>(s[i + 3]) & 0x3F);
                    i += 4; return cp;
                }
                // fallback: invalid byte, treat as single
                i += 1; return c0;
            };

            auto char_width_for = [&](const uint32_t cp) {
                if (cp < 0x80) { // ASCII
                    return metrics.ascii_width;
                }
                // Emoji / surrogate range -> wider
                if (cp >= 0x1F300 || cp > 0xFFFF) {
                    return metrics.chinese_width * 2;
                }
                // CJK Unified, Hangul, Hiragana, Katakana etc.
                if ((cp >= 0x1100 && cp <= 0x11FF) || // Hangul Jamo
                    (cp >= 0x2E80 && cp <= 0xA4CF) || // CJK, Kangxi, radicals, bopomofo
                    (cp >= 0xAC00 && cp <= 0xD7AF)) { // Hangul syllables
                    return metrics.chinese_width;
                }
                // Latin extended, Greek, Cyrillic: use a slightly wider than ASCII
                if ((cp >= 0x00A0 && cp <= 0x04FF) || (cp >= 0x1E00 && cp <= 0x1FFF)) {
                    return metrics.ascii_width + std::max(0, metrics.chinese_width - metrics.ascii_width) / 2;
                }
                // default fall back
                return metrics.chinese_width;
            };

            auto wrap_text = [&](const std::string& text) {
                std::vector<std::string> lines;
                if (text_area_width <= 0) return lines;
                std::string current;
                int current_width = 0;
                size_t idx = 0;
                size_t last_space_byte = std::string::npos;
                int last_space_width = 0;

                while (idx < text.size()) {
                    const size_t start = idx;
                    const uint32_t cp = decode_utf8(text, idx);
                    const int cw = char_width_for(cp);
                    const bool is_space = (cp == ' ' || cp == '\t');

                    if (current_width + cw > text_area_width && !current.empty()) {
                        if (last_space_byte != std::string::npos) {
                            // wrap at last space
                            lines.push_back(current.substr(0, last_space_byte));
                            // remove space itself from new line
                            std::string carry = current.substr(last_space_byte + 1);
                            current.swap(carry);
                            current_width = 0;
                            size_t tmp_idx = 0;
                            while (tmp_idx < current.size()) {
                                const uint32_t cp2 = decode_utf8(current, tmp_idx);
                                current_width += char_width_for(cp2);
                            }
                        } else {
                            lines.push_back(current);
                            current.clear();
                            current_width = 0;
                        }
                        last_space_byte = std::string::npos;
                        last_space_width = 0;
                        // re-check after wrap if this char still doesn't fit alone
                        if (cw > text_area_width && current.empty()) {
                            // force break single wide char
                            std::string tmp(text.substr(start, idx - start));
                            lines.push_back(tmp);
                            continue;
                        }
                    }

                    current.append(text, start, idx - start);
                    current_width += cw;
                    if (is_space) {
                        last_space_byte = current.size() - (idx - start);
                        last_space_width = current_width;
                    }
                }
                if (!current.empty()) lines.push_back(current);
                return lines;
            };

            std::unordered_map<const ConversationEntry*, std::vector<std::string>> wrap_cache;
            auto get_lines = [&](const ConversationEntry& entry) -> const std::vector<std::string>& {
                auto it = wrap_cache.find(&entry);
                if (it != wrap_cache.end()) return it->second;
                auto lines = wrap_text(entry.text);
                auto res = wrap_cache.emplace(&entry, std::move(lines));
                return res.first->second;
            };

            auto count_lines = [&](const ConversationEntry& entry) {
                int lines = (int)get_lines(entry).size();
                return std::max(lines, 1); // 至少一行
            };

            // 记录新消息
            conversation_history_.push_back({cmd.is_user, cmd.text});

            // 历史按总行数截断
            int total_lines = 0;
            for (const auto& e : conversation_history_) total_lines += count_lines(e);
            while (total_lines > max_history_lines && !conversation_history_.empty()) {
                total_lines -= count_lines(conversation_history_.front());
                conversation_history_.erase(conversation_history_.begin());
            }

            struct RenderItem {
                bool is_user;
                std::vector<std::string> lines;
                int text_height;
                int bubble_height;
            };

            std::vector<RenderItem> render_items;

            // 从最新向上收集，保证显示区域内展示最新消息（滚动到最新）
            int used_height = 0;
            for (auto it = conversation_history_.rbegin(); it != conversation_history_.rend(); ++it) {
                const auto& lines = get_lines(*it);
                const int line_count = std::max(1, (int)lines.size());
                const int text_height = line_count * line_height;
                const int avatar_h = it->is_user ? avatar_user_h : avatar_ai_h;
                const int content_h = std::max(text_height, avatar_h);
                const int bubble_height = bubble_padding_y * 2 + content_h;
                const int needed = (render_items.empty() ? 0 : bubble_gap_y) + bubble_height;
                if (used_height + needed > region_height - margin_y * 2) {
                    break; // 已无空间，停止收集更旧消息
                }
                RenderItem item{it->is_user, lines, text_height, bubble_height};
                render_items.push_back(std::move(item));
                used_height += needed;
            }

            // 反转回正序以正确绘制
            std::reverse(render_items.begin(), render_items.end());



            EpdRenderer::Clear();
            int cursor_y = margin_y;
            for (const auto& item : render_items) {
                const bool is_user = item.is_user;
                const int avatar_w = is_user ? avatar_user_w : avatar_ai_w;
                const int avatar_h = is_user ? avatar_user_h : avatar_ai_h;
                const int avatar_x = is_user ? (region_width - margin_x - avatar_w) : margin_x;
                const int avatar_y = cursor_y + bubble_padding_y;

                // Draw avatar if available
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

                // Text alignment and wrapping boundaries
                const int text_x_left = text_area_left;
                const int text_x_right = text_area_right;
                const int text_x_left_user = text_x_right - text_area_width; // 用户侧统一左对齐起点
                const int content_height = std::max(item.text_height, avatar_h);
                int text_y = avatar_y + std::max(0, (content_height - item.text_height) / 2);

                const bool single_line_user = is_user && item.lines.size() <= 1;

                for (const auto& ln : item.lines) {
                    int line_px = 0;
                    size_t idx = 0;
                    while (idx < ln.size()) {
                        const size_t start = idx;
                        const uint32_t cp = decode_utf8(ln, idx);
                        (void)start;
                        line_px += char_width_for(cp);
                    }
                    line_px = std::min(line_px, text_area_width);

                    int text_x;
                    if (is_user) {
                        if (single_line_user) {
                            // 单行用户：右对齐，过宽则落到左起点
                            text_x = std::max(text_x_left_user, text_x_right - line_px);
                        } else {
                            // 多行用户：左对齐
                            text_x = text_x_left_user;
                        }
                    } else {
                        text_x = text_x_left;
                    }

                    EpdRenderer::DrawText(ln, text_x, text_y, font_size);
                    text_y += line_height;
                }

                cursor_y += item.bubble_height + bubble_gap_y;
            }

            int window_height = std::min(region_height, cursor_y + margin_y);
            EpdRenderer::DisplayWindow(0, 0, region_width, window_height, true);
            ESP_LOGW(TAG, "DisplayWindow");
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
                    EpdRenderer::DrawText(line, 0, display->height() - 40, EpdRenderer::FontSize::k16);
                    EpdRenderer::Display(true);
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
