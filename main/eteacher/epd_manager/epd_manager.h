#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class EpdManager {
public:
    static EpdManager& GetInstance();
    static constexpr int kButtonCount = 6;
    static constexpr int kMaxConversationHistory = 20;
    static constexpr int kMaxLinesPerMessage = 40;
    static constexpr int kMaxLineLen = 64;
    void Init();
    void ShowMainMenu();
    void ShowMainMenu(const std::vector<std::string>& items, int selected_index);
    void ShowWordCard(const std::string& card_html);
    void UpdateConversation(bool is_user, const std::string& text);
    void SetActiveScreen(int screen_id);
    void SetRefreshIntervalMs(int interval_ms);
    // Set button hints (6 entries) to be shown on screen
    void SetButtonHints(const std::array<std::string, 6>& hints);
    void DrawButtonHints();

private:
    void LogSystemStatus() const;
    struct Command;
    struct ConversationEntry {
        bool is_user = false;
        uint8_t line_count = 0;
        uint16_t line_width_px[kMaxLinesPerMessage] = {0};
        uint8_t line_length[kMaxLinesPerMessage] = {0};
        char lines[kMaxLinesPerMessage][kMaxLineLen] = {{0}};
    };

    struct RenderItem {
        size_t index = 0;
        int text_height = 0;
        int bubble_height = 0;
    };

    struct AvatarData {
        std::vector<uint8_t> buffer;
        uint8_t* data = nullptr;
        size_t size = 0;
        bool ok = false;
    };

    void EnsureTaskCreated();
    static void TaskEntry(void* arg);
    void TaskLoop();
    void DispatchCommand(Command* cmd);
    void ProcessCommand(Command& cmd);
    bool LoadAvatarFromSd(const std::string& relative_path, AvatarData& dst, const char* label);
    void EnsureAvatarsLoaded();
    bool IsRefreshAllowed(int64_t now_us) const;
    void UpdateLastRefreshTime(int64_t now_us);
    void AppendLineToEntry(ConversationEntry& entry, const char* data, size_t len, int width);
    size_t HistoryIndex(size_t relative_index) const;
    void RemoveOldestEntry();

    EpdManager() = default;

    QueueHandle_t command_queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    bool initialized_ = false;

    int active_screen_ = 0;
    std::array<std::string, kButtonCount> button_hints_ = {"", "", "", "", "", ""};
    ConversationEntry conversation_history_[kMaxConversationHistory] = {};
    size_t conversation_start_ = 0;
    size_t conversation_count_ = 0;
    int total_history_lines_ = 0;
    RenderItem render_items_[kMaxConversationHistory] = {};

    AvatarData user_avatar_;
    AvatarData ai_avatar_;
    bool warned_user_avatar_ = false;
    bool warned_ai_avatar_ = false;
    int refresh_interval_ms_ = 500;
    int64_t last_refresh_time_us_ = 0;
};
