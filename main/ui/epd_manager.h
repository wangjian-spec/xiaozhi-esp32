#pragma once
#include <array>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class EpdManager {
public:
    static EpdManager& GetInstance();
    static constexpr int kButtonCount = 6;
    static constexpr int kMaxConversationHistory = 12;
    void Init();
    void ShowMainMenu();
    void ShowMainMenu(const std::vector<std::string>& items, int selected_index);
    void ShowWordCard(const std::string& card_html);
    void UpdateConversation(bool is_user, const std::string& text);
    void SetActiveScreen(int screen_id);
    // Set button hints (6 entries) to be shown on screen
    void SetButtonHints(const std::array<std::string, 6>& hints);
    void DrawButtonHints();

private:
    struct Command;
    struct ConversationEntry {
        bool is_user = false;
        std::string text;
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

    EpdManager() = default;

    QueueHandle_t command_queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    bool initialized_ = false;

    int active_screen_ = 0;
    std::array<std::string, kButtonCount> button_hints_ = {"", "", "", "", "", ""};
    std::vector<ConversationEntry> conversation_history_;

    AvatarData user_avatar_;
    AvatarData ai_avatar_;
    bool warned_user_avatar_ = false;
    bool warned_ai_avatar_ = false;
};
