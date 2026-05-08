#include "eteacher/apps/device_setting/device_setting.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include <ssid_manager.h>
#include <wifi_manager.h>

#include <cJSON.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_wifi.h>

#include "boards/common/board.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_service/app_service.h"
#include "eteacher/app_ui/debug.h"
#include "eteacher/app_ui/input.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/apps/device_setting/device_setting_ui.h"
#include "system_info.h"

namespace {
constexpr const char* kTag = "DeviceSettingApp";
constexpr const app_ui::desc::UiDesc* kUiDesc = &app_ui::generated::device_setting::kUi;

bool IsUserFieldPlaceholder(const std::string& text) {
    return text == "Phone" || text == "Password" || text == "code";
}

constexpr uint32_t kWidgetTabView = 0xA8EC2EDCu;
constexpr uint32_t kWidgetBottomBar = 0x80DAA8ADu;

constexpr uint32_t kWidgetScanButton = 0xF5EFF05Fu;
constexpr uint32_t kWidgetScanList = 0x3EE34E54u;
constexpr uint32_t kWidgetSavedList = 0xBAB5B4F8u;
constexpr uint32_t kWidgetPageAlert = 0x2C695914u;

constexpr uint32_t kWidgetSoftKeyboard = 0xF2B83403u;
constexpr uint32_t kWidgetDialogInputPassword = 0x847FB9EBu;
constexpr uint32_t kWidgetLabelSsid = 0x36D75C59u;
constexpr uint32_t kWidgetLabelPassword = 0xE39189D7u;
constexpr uint32_t kWidgetTextAreaPassword = 0x0F95CADDu;
constexpr uint32_t kWidgetLabelStatusInformation = 0xB55C8996u;

constexpr uint32_t kWidgetDialogHint = 0x2452ECFFu;
constexpr uint32_t kWidgetHintAlert = 0x7F1B0E11u;
constexpr uint32_t kWidgetButtonSubmit = 0xD17B7AB2u;
constexpr uint32_t kWidgetButtonCancel = 0x087A890Eu;

constexpr uint32_t kWidgetUserPhone = 0xEAB63B8Fu;
constexpr uint32_t kWidgetUserPassword = 0xA49D920Au;
constexpr uint32_t kWidgetUserCode = 0x5F18D61Cu;
constexpr uint32_t kWidgetUserSendCode = 0x57562AC1u;
constexpr uint32_t kWidgetUserLogin = 0xB563EFF5u;
constexpr uint32_t kWidgetUserTitle = 0x47A9CC25u;
constexpr uint32_t kWidgetUserPhoneCaption = 0xD5DD31D7u;
constexpr uint32_t kWidgetUserPasswordCaption = 0xACD863CEu;
constexpr uint32_t kWidgetUserCodeCaption = 0x9395F213u;
constexpr uint32_t kWidgetUserTopFrame = 0x54827056u;
constexpr uint32_t kWidgetUserBottomFrame = 0x9AEA792Fu;
constexpr uint32_t kWidgetUserStageSetting = 0xA2491E37u;
constexpr uint32_t kWidgetUserNoReadRadio = 0x30A954EFu;
constexpr uint32_t kWidgetUserHasReadRadio = 0x8A8DA96Bu;
constexpr uint32_t kWidgetUserMissionSetting = 0xA7B12F6Fu;
constexpr uint32_t kWidgetUserStatus = 0x4BA45650u;
constexpr uint32_t kWidgetUserName = 0x113D6751u;
constexpr uint32_t kWidgetUserPhoneLabel = 0x80F9AF09u;
constexpr uint32_t kWidgetUserMode = 0x511A6020u;

constexpr uint32_t kWidgetDeviceModel = 0x09683472u;
constexpr uint32_t kWidgetDeviceId = 0xA5FFEB50u;
constexpr uint32_t kWidgetDeviceResource = 0x708485AEu;
constexpr uint32_t kWidgetDeviceActivation = 0xB43FAADDu;
constexpr uint32_t kWidgetDeviceStatusButton = 0xBC786763u;
constexpr uint32_t kWidgetDeviceDownloadButton = 0xCD89E678u;

constexpr const char* kInputInitStatus = "请输入WIFI密码,按Start退出键盘";
constexpr const char* kUserJsonPath = "/sdcard/user/user.json";
constexpr int kDefaultDailyNewWordTarget = 10;
constexpr int kDefaultDailyReviewWordTarget = 5;
constexpr int kDefaultDailyTotalTarget = 15;
constexpr std::array<int, 12> kDefaultStageLevelupCount = {10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48};
constexpr std::array<std::pair<int, int>, 7> kMissionPresets = {
    std::pair<int, int>{15, 5},
    std::pair<int, int>{20, 5},
    std::pair<int, int>{25, 5},
    std::pair<int, int>{30, 5},
    std::pair<int, int>{35, 5},
    std::pair<int, int>{40, 5},
    std::pair<int, int>{50, 5},
};

int PositiveOrFallback(int value, int fallback) {
    return value > 0 ? value : fallback;
}

int JsonIntOrDefault(const cJSON* obj, const char* key, int fallback);
bool JsonBoolOrDefault(const cJSON* obj, const char* key, bool fallback);
std::string JsonStringOrDefault(const cJSON* obj, const char* key, const std::string& fallback);

int ReadLegacyDailyTotalTarget(const cJSON* object, int fallback) {
    if (!cJSON_IsObject(object)) {
        return fallback;
    }
    const int configured_count = JsonIntOrDefault(
        object,
        "daily_total_target",
        JsonIntOrDefault(object, "today_mission_count", JsonIntOrDefault(object, "today_practice_word", JsonIntOrDefault(object, "target_words", 0))));
    if (configured_count > 0) {
        return configured_count;
    }
    const int new_word_count = std::max(0, JsonIntOrDefault(object, "new_word_count", 0));
    const int review_word_count = std::max(0, JsonIntOrDefault(object, "review_word_count", 0));
    const int combined_count = new_word_count + review_word_count;
    return combined_count > 0 ? combined_count : fallback;
}

int ReadDailyNewWordTarget(const cJSON* object, int fallback) {
    if (!cJSON_IsObject(object)) {
        return fallback;
    }
    const int configured_count = JsonIntOrDefault(object, "daily_new_word_target", JsonIntOrDefault(object, "new_word_count", 0));
    if (configured_count > 0) {
        return configured_count;
    }
    return fallback;
}

int ReadDailyReviewWordTarget(const cJSON* object, int daily_new_word_target, int daily_total_target, int fallback) {
    if (!cJSON_IsObject(object)) {
        return fallback;
    }
    const int configured_count = JsonIntOrDefault(object, "daily_review_word_target", JsonIntOrDefault(object, "review_word_count", -1));
    if (configured_count >= 0) {
        return configured_count;
    }
    if (daily_total_target > 0 && daily_total_target >= daily_new_word_target) {
        return daily_total_target - daily_new_word_target;
    }
    return fallback;
}

void NormalizeTodayMissionTargets(int* daily_new_word_target, int* daily_review_word_target, int* daily_total_target, int* target_words) {
    if (daily_new_word_target == nullptr || daily_review_word_target == nullptr || daily_total_target == nullptr || target_words == nullptr) {
        return;
    }
    *daily_new_word_target = PositiveOrFallback(*daily_new_word_target, kDefaultDailyNewWordTarget);
    *daily_review_word_target = std::max(0, *daily_review_word_target);
    *daily_total_target = PositiveOrFallback(*daily_total_target, kDefaultDailyTotalTarget);
    if (*daily_total_target < *daily_new_word_target) {
        *daily_total_target = *daily_new_word_target;
    }
    if (*daily_review_word_target > 0) {
        *daily_total_target = std::max(*daily_total_target, *daily_new_word_target + *daily_review_word_target);
    }
    *target_words = *daily_total_target;
}

bool IsClickLike(const ButtonEvent& event) {
    return event.action == ButtonAction::Click || event.action == ButtonAction::LongPress;
}

std::string ReadFileToString(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return {};
    }
    FILE* fp = std::fopen(path, "rb");
    if (!fp) {
        return {};
    }
    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return {};
    }
    const long size = std::ftell(fp);
    if (size < 0) {
        std::fclose(fp);
        return {};
    }
    std::rewind(fp);
    std::string content(static_cast<size_t>(size), '\0');
    const size_t read_size = size > 0 ? std::fread(content.data(), 1, static_cast<size_t>(size), fp) : 0;
    std::fclose(fp);
    if (read_size != static_cast<size_t>(size)) {
        return {};
    }
    return content;
}

bool EnsureDirectoryExists(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    std::string partial;
    for (const char ch : std::string(path)) {
        partial.push_back(ch);
        if (ch == '/') {
            if (partial.size() <= 1) {
                continue;
            }
            struct stat info = {};
            if (stat(partial.c_str(), &info) != 0) {
                if (mkdir(partial.c_str(), 0777) != 0) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool WriteStringToFile(const char* path, const std::string& content) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    std::string parent(path);
    const size_t slash = parent.find_last_of('/');
    if (slash != std::string::npos) {
        parent.resize(slash + 1);
        if (!EnsureDirectoryExists(parent.c_str())) {
            return false;
        }
    }
    FILE* fp = std::fopen(path, "wb");
    if (!fp) {
        return false;
    }
    const size_t written = content.empty() ? 0 : std::fwrite(content.data(), 1, content.size(), fp);
    std::fclose(fp);
    return written == content.size();
}

int JsonIntOrDefault(const cJSON* obj, const char* key, int fallback) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(obj), key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

bool JsonBoolOrDefault(const cJSON* obj, const char* key, bool fallback) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(obj), key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

std::string JsonStringOrDefault(const cJSON* obj, const char* key, const std::string& fallback = {}) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(const_cast<cJSON*>(obj), key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

void LoadIntArrayFromJson(cJSON* root, const char* key, std::vector<int>* values, int fallback) {
    if (values == nullptr) {
        return;
    }
    const cJSON* array = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(array)) {
        return;
    }
    values->clear();
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, const_cast<cJSON*>(array)) {
        values->push_back(cJSON_IsNumber(item) ? item->valueint : fallback);
    }
}

void EnsureIntVectorSize(std::vector<int>* values, size_t size, int fallback) {
    if (values == nullptr) {
        return;
    }
    if (values->size() < size) {
        values->resize(size, fallback);
    }
    for (auto& value : *values) {
        if (value <= 0) {
            value = fallback;
        }
    }
}

int ParseStageIndex(const std::string& value) {
    std::string lower = value;
    while (!lower.empty() && std::isspace(static_cast<unsigned char>(lower.front())) != 0) {
        lower.erase(lower.begin());
    }
    while (!lower.empty() && std::isspace(static_cast<unsigned char>(lower.back())) != 0) {
        lower.pop_back();
    }
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lower.rfind("stage", 0) == 0) {
        const int parsed = std::atoi(lower.substr(5).c_str());
        return std::max(1, std::min(12, parsed));
    }
    const int parsed = std::atoi(lower.c_str());
    return std::max(1, std::min(12, parsed <= 0 ? 1 : parsed));
}

std::string StageKey(int stage_index) {
    return "stage" + std::to_string(std::max(1, std::min(12, stage_index)));
}

std::string BuildMissionLabel(int daily_new_word_target, int daily_review_word_target, int daily_total_target) {
    return "新" + std::to_string(daily_new_word_target) + " / 复" + std::to_string(daily_review_word_target) + " / 总" + std::to_string(daily_total_target);
}

std::string BuildJsonLogPreview(const std::string& content, size_t max_length = 160) {
    std::string preview;
    preview.reserve(std::min(max_length, content.size()));
    for (char ch : content) {
        if (preview.size() >= max_length) {
            break;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            preview.push_back(' ');
        } else {
            preview.push_back(ch);
        }
    }
    if (content.size() > preview.size()) {
        preview += "...";
    }
    return preview;
}

void AppendIfValid(const app_ui::Widget* widget, std::vector<uint32_t>& out) {
    if (!widget) {
        return;
    }
    const uint32_t id = widget->Id();
    if (id != 0) {
        out.push_back(id);
    }
}

bool MapButtonToKey(AppButton button, app_ui::KeyCode& out) {
    switch (button) {
        case AppButton::Up:
            out = app_ui::KeyCode::Up;
            return true;
        case AppButton::Down:
            out = app_ui::KeyCode::Down;
            return true;
        case AppButton::Left:
            out = app_ui::KeyCode::Left;
            return true;
        case AppButton::Right:
            out = app_ui::KeyCode::Right;
            return true;
        case AppButton::A:
            out = app_ui::KeyCode::A;
            return true;
        case AppButton::B:
            out = app_ui::KeyCode::B;
            return true;
        case AppButton::C:
            out = app_ui::KeyCode::C;
            return true;
        case AppButton::D:
            out = app_ui::KeyCode::D;
            return true;
        case AppButton::Select:
            out = app_ui::KeyCode::Select;
            return true;
        case AppButton::Start:
            out = app_ui::KeyCode::Start;
            return true;
        case AppButton::VolumeUp:
            out = app_ui::KeyCode::VolumeUp;
            return true;
        case AppButton::VolumeDown:
            out = app_ui::KeyCode::VolumeDown;
            return true;
        default:
            return false;
    }
}
}

MenuMeta DeviceSettingApp::GetMenuMeta() const {
    return MenuMeta{"device_setting", "系统设置", "Left/Right to switch"};
}

bool DeviceSettingApp::ShouldInterceptSelectExit() const {
    return confirm_dialog_visible_ || keyboard_visible_ || IsInputDialogVisible() || IsHintDialogVisible();
}

void DeviceSettingApp::OnEnter(AppContext &ctx) {
    ui_ready_ = false;
    keyboard_visible_ = false;
    keyboard_mode_ = KeyboardMode::None;
    keyboard_ignore_activation_once_ = false;
    confirm_dialog_visible_ = false;
    confirm_delete_action_ = false;
    scanning_in_progress_ = false;
    has_scanned_once_ = false;
    last_scan_failed_ = false;
    initial_focus_pending_ = false;
    submit_connect_in_progress_ = false;
    confirm_connect_in_progress_ = false;
    submit_cooldown_until_us_ = 0;
    confirm_cooldown_until_us_ = 0;
    hint_alert_text_.clear();
    keyboard_field_title_.clear();
    last_alert_text_.clear();
    last_connected_ssid_.clear();
    active_user_input_ = nullptr;
    user_json_ = {};
    router_.Reset();
    scene_load_id_ = 0;
    epd_ = dynamic_cast<CustomEpdDisplay*>(ctx.board.GetDisplay());
    LoadUserJson();

    if (!LoadUi(ctx)) {
        return;
    }
    InitUiEngine();
    router_.SetActivateFn([this](AppContext& context, size_t /*index*/, const std::string& scene_id) {
        return LoadScene(context, scene_id, ++scene_load_id_);
    });
    if (router_.Activate(ctx, 0)) {
        Render(ctx);
    }
}

void DeviceSettingApp::OnExit(AppContext &ctx) {
    (void)ctx;
    StopAutoCloseDialog();

    if (connect_disconnected_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, connect_disconnected_handler_);
        connect_disconnected_handler_ = nullptr;
    }
    if (connect_got_ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, connect_got_ip_handler_);
        connect_got_ip_handler_ = nullptr;
    }

    ui_ready_ = false;
    keyboard_visible_ = false;
    keyboard_mode_ = KeyboardMode::None;
    keyboard_ignore_activation_once_ = false;
    confirm_dialog_visible_ = false;
    submit_connect_in_progress_ = false;
    confirm_connect_in_progress_ = false;
    submit_cooldown_until_us_ = 0;
    confirm_cooldown_until_us_ = 0;
    hint_alert_text_.clear();
    keyboard_field_title_.clear();
    active_user_input_ = nullptr;
    epd_ = nullptr;
}

void DeviceSettingApp::OnTick(AppContext &ctx) {
    if (initial_focus_pending_) {
        if (tabview_) {
            ESP_LOGI(kTag, "Forcing initial tab focus on post-activation tick id=0x%08lx",
                     static_cast<unsigned long>(tabview_->Id()));
            ui_engine_.RequestFocus(tabview_->Id());
        } else {
            EnsureManagedFocus();
        }
        initial_focus_pending_ = false;
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }

    if (!ui_ready_ || !is_wifi_scene_) {
        return;
    }

    const std::string connected_ssid = CurrentConnectedSsid();
    if (connected_ssid == last_connected_ssid_) {
        return;
    }

    last_connected_ssid_ = connected_ssid;
    RefreshNetworkListDisplay();
    UpdateAlertStatusByNetwork();
    Render(ctx);
}

void DeviceSettingApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
    if (!ui_ready_ || !IsClickLike(event)) {
        return;
    }

    if (HandleConfirmDialogButtons(event)) {
        EnsureManagedFocus();
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }

    if (!keyboard_visible_ && !confirm_dialog_visible_ && HandleFocusCycle(event)) {
        EnsureManagedFocus();
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }

    if (HandleKeyboardButtons(event)) {
        EnsureManagedFocus();
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }
    if (HandleTabViewNav(ctx, event)) {
        EnsureManagedFocus();
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }
    if (HandleFocusedButtons(event)) {
        EnsureManagedFocus();
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }
    if (HandleListViewActivate(event)) {
        EnsureManagedFocus();
        UpdateBottomBarHintByFocus();
        Render(ctx);
        return;
    }

    SendInputToUi(event);
    EnsureManagedFocus();
    UpdateBottomBarHintByFocus();
    Render(ctx);
}

bool DeviceSettingApp::LoadUi(AppContext &ctx) {
    auto scene_ids = app_ui::CollectSceneIds(*kUiDesc);
    if (scene_ids.empty()) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: no scenes");
        return false;
    }
    router_.SetScenes(std::move(scene_ids));
    ui_ready_ = true;
    return true;
}

bool DeviceSettingApp::LoadScene(AppContext &ctx, const std::string& scene_id, uint16_t scene_index) {
    if (!scene_runtime_.LoadFromDesc(*kUiDesc, scene_id.c_str(), scene_index)) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to load scene");
        return false;
    }
    auto new_root = scene_runtime_.TakeRoot();
    if (!new_root) {
        ctx.board.GetDisplay()->SetChatMessage("system", "Device Setting UI: failed to take scene root");
        return false;
    }

    root_ = new_root.get();
    BindWidgets(root_);
    last_alert_text_.clear();
    UpdateTabSelection();
    BuildFocusCycle(scene_id);
    SyncFocusCycleIndex();
    initial_focus_pending_ = true;

    uint32_t preferred_focus_id = 0;
    if (tabview_) {
        ESP_LOGI(kTag, "Scene %s requesting initial tab focus id=0x%08lx focusable=%d",
                 scene_id.c_str(),
                 static_cast<unsigned long>(tabview_->Id()),
                 tabview_->Focusable() ? 1 : 0);
        preferred_focus_id = tabview_->Id();
    } else if (scene_id == "page_1e8a" && user_stage_setting_button_) {
        preferred_focus_id = user_stage_setting_button_->Id();
    } else if (scene_id == "page_9b37" && device_status_button_) {
        preferred_focus_id = device_status_button_->Id();
    }

    if (preferred_focus_id != 0) {
        ui_engine_.RequestFocus(preferred_focus_id);
    }

    ui_engine_.SetRoot(std::move(new_root));
    ESP_LOGI(kTag, "Load scene: %s", scene_id.c_str());
    HideKeyboard();
    HideConfirmDialog();
    UpdateAlertStatusByNetwork();
    UpdateBottomBarHintByFocus();
    return true;
}

void DeviceSettingApp::InitUiEngine() {
    ui_engine_.Reset();
    app_ui::debug::SetUiDebugLoggingEnabled(true);
    ui_engine_.SetEpd(epd_);
}

void DeviceSettingApp::Render(AppContext &ctx) {
    if (!ui_ready_) {
        return;
    }
    if (confirm_dialog_visible_) {
        RefreshHintAlertLabel();
    }
    ui_engine_.RequestRender();
    if (!epd_) {
        std::string msg = "Device Setting UI\n";
        msg += "Scene: #";
        msg += std::to_string(scene_runtime_.SceneId());
        ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
    }
}

void DeviceSettingApp::PrevScene(AppContext &ctx) {
    if (!router_.HasScenes()) {
        return;
    }
    if (router_.Prev(ctx)) {
        UpdateTabSelection();
    }
}

void DeviceSettingApp::NextScene(AppContext &ctx) {
    if (!router_.HasScenes()) {
        return;
    }
    if (router_.Next(ctx)) {
        UpdateTabSelection();
    }
}

void DeviceSettingApp::BindWidgets(app_ui::Widget* root) {
    if (!root) {
        tabview_ = nullptr;
        scan_button_ = nullptr;
        scan_list_ = nullptr;
        saved_list_ = nullptr;
        bottom_bar_ = nullptr;
        keyboard_ = nullptr;
        input_dialog_ = nullptr;
        hint_dialog_ = nullptr;
        button_submit_ = nullptr;
        button_cancel_ = nullptr;
        ssid_label_ = nullptr;
        password_label_ = nullptr;
        status_info_label_ = nullptr;
        page_alert_label_ = nullptr;
        hint_alert_label_ = nullptr;
        password_area_ = nullptr;
        user_phone_area_ = nullptr;
        user_password_area_ = nullptr;
        user_code_area_ = nullptr;
        user_title_label_ = nullptr;
        user_phone_caption_label_ = nullptr;
        user_password_caption_label_ = nullptr;
        user_code_caption_label_ = nullptr;
        user_send_code_button_ = nullptr;
        user_login_button_ = nullptr;
        user_stage_setting_button_ = nullptr;
        user_mission_setting_button_ = nullptr;
        user_status_label_ = nullptr;
        user_name_label_ = nullptr;
        user_phone_label_ = nullptr;
        user_mode_label_ = nullptr;
        user_has_read_radio_ = nullptr;
        user_no_read_radio_ = nullptr;
        user_top_frame_ = nullptr;
        user_bottom_frame_ = nullptr;
        device_status_button_ = nullptr;
        device_download_button_ = nullptr;
        device_model_label_ = nullptr;
        device_id_label_ = nullptr;
        device_resource_label_ = nullptr;
        device_activation_label_ = nullptr;
        return;
    }

    tabview_ = dynamic_cast<app_ui::TabViewWidget*>(root->FindById(kWidgetTabView));
    bottom_bar_ = dynamic_cast<app_ui::BottomBarWidget*>(root->FindById(kWidgetBottomBar));

    scan_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetScanButton));
    scan_list_ = dynamic_cast<app_ui::ListViewWidget*>(root->FindById(kWidgetScanList));
    saved_list_ = dynamic_cast<app_ui::ListViewWidget*>(root->FindById(kWidgetSavedList));
    page_alert_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetPageAlert));

    keyboard_ = dynamic_cast<app_ui::SoftKeyboardWidget*>(root->FindById(kWidgetSoftKeyboard));
    input_dialog_ = dynamic_cast<app_ui::DialogWidget*>(root->FindById(kWidgetDialogInputPassword));
    hint_dialog_ = dynamic_cast<app_ui::DialogWidget*>(root->FindById(kWidgetDialogHint));
    button_submit_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetButtonSubmit));
    button_cancel_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetButtonCancel));

    ssid_label_ = input_dialog_ ? dynamic_cast<app_ui::LabelWidget*>(input_dialog_->FindById(kWidgetLabelSsid)) : nullptr;
    password_label_ = input_dialog_ ? dynamic_cast<app_ui::LabelWidget*>(input_dialog_->FindById(kWidgetLabelPassword)) : nullptr;
    status_info_label_ = input_dialog_ ? dynamic_cast<app_ui::LabelWidget*>(input_dialog_->FindById(kWidgetLabelStatusInformation)) : nullptr;
    password_area_ = input_dialog_ ? dynamic_cast<app_ui::TextAreaWidget*>(input_dialog_->FindById(kWidgetTextAreaPassword)) : nullptr;
    hint_alert_label_ = hint_dialog_ ? dynamic_cast<app_ui::LabelWidget*>(hint_dialog_->FindById(kWidgetHintAlert)) : nullptr;

    user_title_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetUserTitle));
    user_phone_caption_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetUserPhoneCaption));
    user_password_caption_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetUserPasswordCaption));
    user_code_caption_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetUserCodeCaption));
    user_top_frame_ = dynamic_cast<app_ui::FrameWidget*>(root->FindById(kWidgetUserTopFrame));
    user_bottom_frame_ = dynamic_cast<app_ui::FrameWidget*>(root->FindById(kWidgetUserBottomFrame));
    user_phone_area_ = dynamic_cast<app_ui::TextAreaWidget*>(root->FindById(kWidgetUserPhone));
    user_password_area_ = dynamic_cast<app_ui::TextAreaWidget*>(root->FindById(kWidgetUserPassword));
    user_code_area_ = dynamic_cast<app_ui::TextAreaWidget*>(root->FindById(kWidgetUserCode));
    user_send_code_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetUserSendCode));
    user_login_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetUserLogin));
    user_stage_setting_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetUserStageSetting));
    user_mission_setting_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetUserMissionSetting));
    user_status_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetUserStatus));
    user_name_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetUserName));
    user_phone_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetUserPhoneLabel));
    user_mode_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetUserMode));
    user_has_read_radio_ = dynamic_cast<app_ui::RadioWidget*>(root->FindById(kWidgetUserHasReadRadio));
    user_no_read_radio_ = dynamic_cast<app_ui::RadioWidget*>(root->FindById(kWidgetUserNoReadRadio));

    device_status_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetDeviceStatusButton));
    device_download_button_ = dynamic_cast<app_ui::ButtonWidget*>(root->FindById(kWidgetDeviceDownloadButton));
    device_model_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetDeviceModel));
    device_id_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetDeviceId));
    device_resource_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetDeviceResource));
    device_activation_label_ = dynamic_cast<app_ui::LabelWidget*>(root->FindById(kWidgetDeviceActivation));

    if (user_phone_area_ && IsUserFieldPlaceholder(user_phone_area_->Text())) {
        user_phone_area_->SetText("");
    }
    if (user_password_area_ && IsUserFieldPlaceholder(user_password_area_->Text())) {
        user_password_area_->SetText("");
    }
    if (user_code_area_ && IsUserFieldPlaceholder(user_code_area_->Text())) {
        user_code_area_->SetText("");
    }

    if (!hint_alert_label_ && hint_dialog_) {
        hint_alert_label_ = dynamic_cast<app_ui::LabelWidget*>(hint_dialog_->FindById(kWidgetPageAlert));
    }
    if (hint_alert_label_) {
        hint_alert_label_->SetZOrder(30);
    }

    if (keyboard_) {
        keyboard_->SetOnKey(&DeviceSettingApp::OnKeyboardKey, this);
    }
    if (tabview_) {
        tabview_->SetFocusable(true);
    }

    scan_ssids_raw_.clear();
    scan_rssi_raw_.clear();
    scan_results_.clear();
    has_scanned_once_ = false;
    last_scan_failed_ = false;
    RefreshNetworkListDisplay();
    if (scan_list_) {
        auto profile = scan_list_->Profile();
        profile.rows = 10;
        profile.cols = 1;
        profile.selection_enabled = true;
        profile.focus_highlight_enabled = true;
        scan_list_->SetProfile(profile);
        scan_list_->SetItems(scan_results_);
    }

    PopulateSavedNetworks();
    if (saved_list_) {
        auto profile = saved_list_->Profile();
        profile.rows = 10;
        profile.cols = 1;
        profile.selection_enabled = true;
        profile.focus_highlight_enabled = true;
        saved_list_->SetProfile(profile);
    }
    HideKeyboard();
    HideConfirmDialog();
    SetInputStatus(kInputInitStatus);
    SetBottomBarHint("");
    RefreshClientUiState();
}

void DeviceSettingApp::UpdateTabSelection() {
    if (!tabview_) {
        return;
    }
    tabview_->SetSelectedIndex(static_cast<int>(router_.Index()));
}

void DeviceSettingApp::PopulateScanResults() {
    scanning_in_progress_ = true;
    SetAlertLabel("网络扫描中");

    scan_ssids_raw_.clear();
    scan_rssi_raw_.clear();
    scan_results_.clear();
    bool scan_failed = false;

    auto& wifi = WifiManager::GetInstance();
    if (!wifi.Initialize()) {
        ESP_LOGW(kTag, "WiFi manager initialize failed");
        scan_failed = true;
    } else {
        auto& app_service = AppService::GetInstance();

        ESP_LOGI(kTag, "Manual WiFi scan requested");
        app_service.BeginWifiScanNoAutoConnect();
        wifi.SetStationScanConsumeEnabled(false);

        wifi_scan_config_t scan_cfg = {};
        scan_cfg.show_hidden = false;

        esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
        if (err == ESP_ERR_WIFI_STATE) {
            ESP_LOGW(kTag, "Scan rejected by WIFI_STATE, restarting station and retrying once");
            wifi.StopStation();
            wifi.StartStation();
            vTaskDelay(pdMS_TO_TICKS(200));
            err = esp_wifi_scan_start(&scan_cfg, true);
        }
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "Manual scan start failed: %s", esp_err_to_name(err));
            scan_failed = true;
        } else {
            std::vector<std::pair<std::string, int>> scan_results;
            uint16_t ap_num = 0;
            err = esp_wifi_scan_get_ap_num(&ap_num);
            if (err != ESP_OK) {
                ESP_LOGW(kTag, "Manual scan get_ap_num failed: %s", esp_err_to_name(err));
                scan_failed = true;
            } else if (ap_num > 0) {
                std::vector<wifi_ap_record_t> records(ap_num);
                uint16_t record_count = ap_num;
                err = esp_wifi_scan_get_ap_records(&record_count, records.data());
                if (err != ESP_OK) {
                    ESP_LOGW(kTag, "Manual scan get_ap_records failed: %s", esp_err_to_name(err));
                    scan_failed = true;
                } else {
                    records.resize(record_count);
                    std::sort(records.begin(), records.end(), [](const wifi_ap_record_t& lhs, const wifi_ap_record_t& rhs) {
                        return lhs.rssi > rhs.rssi;
                    });

                    scan_results.reserve(records.size());
                    for (const auto& record : records) {
                        const char* ssid = reinterpret_cast<const char*>(record.ssid);
                        if (ssid == nullptr || ssid[0] == '\0') {
                            continue;
                        }
                        auto duplicate = std::find_if(scan_results.begin(), scan_results.end(), [ssid](const auto& item) {
                            return item.first == ssid;
                        });
                        if (duplicate != scan_results.end()) {
                            continue;
                        }
                        scan_results.emplace_back(ssid, static_cast<int>(record.rssi));
                    }
                }
            }

            if (scan_results.empty()) {
                constexpr int kCacheRetryStepMs = 20;
                constexpr int kCacheRetryTimeoutMs = 160;
                int waited_ms = 0;
                while (waited_ms <= kCacheRetryTimeoutMs) {
                    scan_results = app_service.ConsumeWifiScanResults();
                    if (!scan_results.empty()) {
                        ESP_LOGI(kTag, "Manual scan recovered %u APs from scan guard cache after %d ms",
                                 static_cast<unsigned>(scan_results.size()), waited_ms);
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(kCacheRetryStepMs));
                    waited_ms += kCacheRetryStepMs;
                }
            } else {
                // Drain any guard cache populated by the scan callback so the next scan starts cleanly.
                (void)app_service.ConsumeWifiScanResults();
            }

            ESP_LOGI(kTag, "Manual scan captured %u APs directly from driver",
                     static_cast<unsigned>(scan_results.size()));

            for (const auto& item : scan_results) {
                if (item.first.empty()) {
                    continue;
                }
                scan_ssids_raw_.push_back(item.first);
                scan_rssi_raw_.push_back(item.second);
            }
        }
        wifi.SetStationScanConsumeEnabled(true);

        app_service.EndWifiScanNoAutoConnect();
    }

    has_scanned_once_ = true;
    last_scan_failed_ = scan_failed;
    RefreshNetworkListDisplay();

    scanning_in_progress_ = false;
    UpdateAlertStatusByNetwork();
}

void DeviceSettingApp::PopulateSavedNetworks() {
    saved_networks_.clear();
    const auto& ssid_list = SsidManager::GetInstance().GetSsidList();
    for (const auto& item : ssid_list) {
        saved_networks_.push_back(item.ssid);
    }
    if (saved_networks_.empty()) {
        saved_networks_.push_back("(empty)");
    }

    RefreshNetworkListDisplay();
}

void DeviceSettingApp::RefreshNetworkListDisplay() {
    const std::string connected_ssid = CurrentConnectedSsid();

    scan_results_.clear();
    if (last_scan_failed_) {
        scan_results_.push_back("(scan failed)");
    } else if (!scan_ssids_raw_.empty()) {
        for (size_t i = 0; i < scan_ssids_raw_.size(); ++i) {
            const auto& ssid = scan_ssids_raw_[i];
            std::string item = ssid;
            if (!connected_ssid.empty() && ssid == connected_ssid) {
                item += "\t已连接";
            } else if (i < scan_rssi_raw_.size()) {
                item += "\t" + std::to_string(scan_rssi_raw_[i]) + " dBm";
            }
            scan_results_.push_back(std::move(item));
        }
    } else if (has_scanned_once_) {
        scan_results_.push_back("(none)");
    }

    if (scan_list_) {
        scan_list_->SetItems(scan_results_);
    }

    saved_display_items_.clear();
    saved_display_items_.reserve(saved_networks_.size());
    for (const auto& ssid : saved_networks_) {
        if (ssid.empty() || IsPlaceholderItem(ssid)) {
            saved_display_items_.push_back(ssid);
            continue;
        }
        if (!connected_ssid.empty() && ssid == connected_ssid) {
            saved_display_items_.push_back(ssid + "\t已连接");
        } else {
            saved_display_items_.push_back(ssid);
        }
    }
    if (saved_list_) {
        saved_list_->SetItems(saved_display_items_);
    }

    UpdateListFocusability();
}

bool DeviceSettingApp::HasFocusableListItems(const std::vector<std::string>& items) const {
    for (const auto& item : items) {
        if (!item.empty() && !IsPlaceholderItem(item)) {
            return true;
        }
    }
    return false;
}

void DeviceSettingApp::UpdateListFocusability() {
    if (scan_list_) {
        scan_list_->SetFocusable(HasFocusableListItems(scan_ssids_raw_));
    }
    if (saved_list_) {
        saved_list_->SetFocusable(HasFocusableListItems(saved_networks_));
    }
}

void DeviceSettingApp::UpdateAlertStatusByNetwork() {
    const std::string ssid = CurrentConnectedSsid();
    if (!ssid.empty()) {
        SetAlertLabel("网络已连接");
        return;
    }
    if (scanning_in_progress_) {
        SetAlertLabel("网络扫描中");
        return;
    }
    if (has_scanned_once_ && !last_scan_failed_) {
        SetAlertLabel("扫描成功");
        return;
    }
    SetAlertLabel("网络未连接");
}

void DeviceSettingApp::SetAlertLabel(const std::string& text) {
    if (last_alert_text_ == text && page_alert_label_ && page_alert_label_->Text() == text) {
        return;
    }
    last_alert_text_ = text;
    if (page_alert_label_) {
        page_alert_label_->SetText(text);
        page_alert_label_->SetVisible(true);
    }
}

void DeviceSettingApp::SetInputStatus(const std::string& text) {
    if (status_info_label_) {
        status_info_label_->SetText(text);
    }
}

void DeviceSettingApp::SetBottomBarHint(const std::string& text) {
    bottom_bar_hint_ = text;
    if (bottom_bar_) {
        bottom_bar_->SetText(bottom_bar_hint_);
    }
}

void DeviceSettingApp::PushBottomBarHint(const std::string& text) {
    SetBottomBarHint(text);
}

void DeviceSettingApp::UpdateBottomBarHintByFocus() {
    if (IsInputDialogVisible() || keyboard_visible_) {
        SetBottomBarHint("软键盘：方向键移动 C输入 B退格 D翻页 Start退出");
        return;
    }
    if (IsHintDialogVisible()) {
        SetBottomBarHint("上下切换 Start/C确认 B取消");
        return;
    }
    if (scan_button_ && scan_button_->Focused()) {
        SetBottomBarHint("按Start/C键开始扫描");
        return;
    }
    if (scan_list_ && scan_list_->Focused()) {
        SetBottomBarHint("C连接");
        return;
    }
    if (saved_list_ && saved_list_->Focused()) {
        SetBottomBarHint("C连接 D删除");
        return;
    }
    if ((user_phone_area_ && user_phone_area_->Focused()) || (user_password_area_ && user_password_area_->Focused()) ||
        (user_code_area_ && user_code_area_->Focused())) {
        SetBottomBarHint("按Start/C键编辑");
        return;
    }
    if (user_stage_setting_button_ && user_stage_setting_button_->Focused()) {
        SetBottomBarHint("按Start/C切换学习阶段");
        return;
    }
    if ((user_has_read_radio_ && user_has_read_radio_->Focused()) || (user_no_read_radio_ && user_no_read_radio_->Focused())) {
        SetBottomBarHint("按Start/C切换口语题开关");
        return;
    }
    if (user_mission_setting_button_ && user_mission_setting_button_->Focused()) {
        SetBottomBarHint("按Start/C切换今日任务");
        return;
    }
    if (user_send_code_button_ && user_send_code_button_->Focused()) {
        SetBottomBarHint("按Start/C发送验证码");
        return;
    }
    if (user_login_button_ && user_login_button_->Focused()) {
        SetBottomBarHint("按Start/C注册或登录");
        return;
    }
    if (device_status_button_ && device_status_button_->Focused()) {
        SetBottomBarHint("按Start/C检查设备状态");
        return;
    }
    if (device_download_button_ && device_download_button_->Focused()) {
        SetBottomBarHint("按Start/C下载资源");
        return;
    }
    SetBottomBarHint("");
}

void DeviceSettingApp::BuildFocusCycle(const std::string& scene_id) {
    is_wifi_scene_ = (scene_id == "page_main");
    focus_cycle_ids_.clear();
    focus_cycle_index_ = 0;
    AppendIfValid(tabview_, focus_cycle_ids_);
    if (scene_id == "page_main") {
        AppendIfValid(scan_button_, focus_cycle_ids_);
        AppendIfValid(scan_list_, focus_cycle_ids_);
        AppendIfValid(saved_list_, focus_cycle_ids_);
        return;
    }
    if (scene_id == "page_1e8a") {
        AppendIfValid(user_stage_setting_button_, focus_cycle_ids_);
        AppendIfValid(user_has_read_radio_, focus_cycle_ids_);
        AppendIfValid(user_no_read_radio_, focus_cycle_ids_);
        AppendIfValid(user_mission_setting_button_, focus_cycle_ids_);
        return;
    }
    if (scene_id == "page_9b37") {
        AppendIfValid(device_status_button_, focus_cycle_ids_);
        AppendIfValid(device_download_button_, focus_cycle_ids_);
    }
}

void DeviceSettingApp::SyncFocusCycleIndex() {
    if (focus_cycle_ids_.empty()) {
        focus_cycle_index_ = 0;
        return;
    }
    for (size_t i = 0; i < focus_cycle_ids_.size(); ++i) {
        const uint32_t id = focus_cycle_ids_[i];
        app_ui::Widget* widget = root_ ? root_->FindById(id) : nullptr;
        if (widget && widget->Focused()) {
            focus_cycle_index_ = static_cast<int>(i);
            return;
        }
    }
    focus_cycle_index_ = 0;
}

int DeviceSettingApp::FocusCycleIndexOf(uint32_t id) const {
    for (size_t i = 0; i < focus_cycle_ids_.size(); ++i) {
        if (focus_cycle_ids_[i] == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

app_ui::Widget* DeviceSettingApp::FindFocusableCycleWidget(int start_index, int step, bool allow_tabview) const {
    if (root_ == nullptr || focus_cycle_ids_.empty() || step == 0) {
        return nullptr;
    }

    for (int index = start_index; index >= 0 && index < static_cast<int>(focus_cycle_ids_.size()); index += step) {
        app_ui::Widget* widget = root_->FindById(focus_cycle_ids_[index]);
        if (!widget || !widget->Focusable()) {
            continue;
        }
        if (!allow_tabview && widget == tabview_) {
            continue;
        }
        return widget;
    }
    return nullptr;
}

app_ui::Widget* DeviceSettingApp::FirstSceneFocus() const {
    return FindFocusableCycleWidget(1, 1, false);
}

app_ui::Widget* DeviceSettingApp::LastSceneFocus() const {
    return FindFocusableCycleWidget(static_cast<int>(focus_cycle_ids_.size()) - 1, -1, false);
}

app_ui::Widget* DeviceSettingApp::CurrentManagedFocus() const {
    if (root_ == nullptr) {
        return nullptr;
    }
    for (uint32_t id : focus_cycle_ids_) {
        app_ui::Widget* widget = root_->FindById(id);
        if (widget && widget->Focused()) {
            return widget;
        }
    }
    return nullptr;
}

void DeviceSettingApp::EnsureManagedFocus() {
    if (keyboard_visible_ || confirm_dialog_visible_ || IsInputDialogVisible() || IsHintDialogVisible()) {
        return;
    }
    if (app_ui::Widget* current_focus = CurrentManagedFocus()) {
        ESP_LOGI(kTag, "Managed focus present id=0x%08lx", static_cast<unsigned long>(current_focus->Id()));
        return;
    }
    if (tabview_ && tabview_->Focusable()) {
        ESP_LOGW(kTag, "Managed focus missing, restoring tabview focus id=0x%08lx",
                 static_cast<unsigned long>(tabview_->Id()));
        ui_engine_.RequestFocus(tabview_->Id());
        return;
    }
    if (app_ui::Widget* first_focus = FirstSceneFocus()) {
        ESP_LOGW(kTag, "Managed focus missing, restoring first scene focus id=0x%08lx",
                 static_cast<unsigned long>(first_focus->Id()));
        ui_engine_.RequestFocus(first_focus->Id());
    }
}

bool DeviceSettingApp::HandleFocusCycle(const ButtonEvent &event) {
    if (focus_cycle_ids_.empty() || event.action != ButtonAction::Click) {
        return false;
    }

    app_ui::Widget* first_focus = FirstSceneFocus();
    app_ui::Widget* last_focus = LastSceneFocus();

    if (tabview_ && tabview_->Focused()) {
        if (event.id == AppButton::Down && first_focus) {
            ui_engine_.RequestFocus(first_focus->Id());
            ESP_LOGI(kTag, "Focus moved from tabview to widget=0x%08lx",
                     static_cast<unsigned long>(first_focus->Id()));
            return true;
        }
        if (event.id == AppButton::Up && last_focus) {
            ui_engine_.RequestFocus(last_focus->Id());
            ESP_LOGI(kTag, "Focus moved from tabview to widget=0x%08lx",
                     static_cast<unsigned long>(last_focus->Id()));
            return true;
        }
        return false;
    }

    if (first_focus && first_focus->Focused() && event.id == AppButton::Up && tabview_) {
        ui_engine_.RequestFocus(tabview_->Id());
        ESP_LOGI(kTag, "Focus moved to tabview from widget=0x%08lx",
                 static_cast<unsigned long>(first_focus->Id()));
        return true;
    }

    if (!is_wifi_scene_) {
        return false;
    }

    app_ui::Widget* current_focus = CurrentManagedFocus();
    if (current_focus == nullptr) {
        return false;
    }

    const int current_index = FocusCycleIndexOf(current_focus->Id());
    if (current_index < 0) {
        return false;
    }

    if (scan_list_ && scan_list_->Focused()) {
        const int selected_index = scan_list_->SelectedIndex();
        const int item_count = scan_list_->ItemCount();
        if ((event.id == AppButton::Up && selected_index <= 0) ||
            (event.id == AppButton::Down && selected_index >= item_count - 1)) {
            return true;
        }
    }

    if (saved_list_ && saved_list_->Focused()) {
        const int selected_index = saved_list_->SelectedIndex();
        const int item_count = saved_list_->ItemCount();
        if ((event.id == AppButton::Up && selected_index <= 0) ||
            (event.id == AppButton::Down && selected_index >= item_count - 1)) {
            return true;
        }
    }

    app_ui::Widget* target_focus = nullptr;
    if (event.id == AppButton::Left) {
        target_focus = FindFocusableCycleWidget(current_index - 1, -1, true);
    } else if (event.id == AppButton::Right) {
        target_focus = FindFocusableCycleWidget(current_index + 1, 1, false);
    } else if (event.id == AppButton::Down && current_focus == scan_button_) {
        target_focus = FindFocusableCycleWidget(current_index + 1, 1, false);
    }

    if (target_focus && target_focus != current_focus) {
        ui_engine_.RequestFocus(target_focus->Id());
        ESP_LOGI(kTag, "Focus moved to widget=0x%08lx", static_cast<unsigned long>(target_focus->Id()));
        return true;
    }

    return false;
}

bool DeviceSettingApp::HandleTabViewNav(AppContext &ctx, const ButtonEvent &event) {
    if (!tabview_ || !tabview_->Focused()) {
        return false;
    }
    if (event.id == AppButton::Left) {
        PrevScene(ctx);
        return true;
    }
    if (event.id == AppButton::Right) {
        NextScene(ctx);
        return true;
    }
    return false;
}

bool DeviceSettingApp::HandleKeyboardButtons(const ButtonEvent &event) {
    if (!keyboard_visible_) {
        return false;
    }

    if (event.id == AppButton::B) {
        DeletePasswordChar();
        return true;
    }
    if (event.id == AppButton::Select) {
        HideKeyboard();
        return true;
    }
    if (event.id == AppButton::Start) {
        if (event.action == ButtonAction::LongPress) {
            return true;
        }
        ConfirmPassword();
        return true;
    }

    app_ui::KeyCode key{};
    if (!MapButtonToKey(event.id, key)) {
        return true;
    }
    if (!keyboard_) {
        return true;
    }

    app_ui::InputEvent e;
    e.type = (event.action == ButtonAction::LongPress) ? app_ui::InputType::KeyRepeat : app_ui::InputType::KeyDown;
    e.key = static_cast<int>(key);
    e.timestamp = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    keyboard_->OnInput(e, app_ui::InputPhase::Target);
    return true;
}

bool DeviceSettingApp::HandleConfirmDialogButtons(const ButtonEvent &event) {
    if (!confirm_dialog_visible_) {
        return false;
    }

    if (event.id == AppButton::B || event.id == AppButton::Select) {
        HideConfirmDialog();
        return true;
    }

    if (event.id == AppButton::Up || event.id == AppButton::Down || event.id == AppButton::Left ||
        event.id == AppButton::Right) {
        if (button_submit_ && button_cancel_) {
            const bool submit_focused = button_submit_->Focused();
            ui_engine_.RequestFocus(submit_focused ? button_cancel_->Id() : button_submit_->Id());
        }
        RefreshHintAlertLabel();
        return true;
    }

    if ((event.id == AppButton::C || event.id == AppButton::Start) && event.action == ButtonAction::Click) {
        if (button_cancel_ && button_cancel_->Focused()) {
            HideConfirmDialog();
            return true;
        }

        const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
        if (confirm_connect_in_progress_ || now_us < confirm_cooldown_until_us_) {
            SetHintAlertLabel("正在连接中，请勿重复提交");
            return true;
        }

        ConfirmPendingDialogAction();
        return true;
    }

    return true;
}

bool DeviceSettingApp::HandleFocusedButtons(const ButtonEvent &event) {
    const bool action = (event.action == ButtonAction::Click) &&
                        (event.id == AppButton::Start || event.id == AppButton::C);
    if (!action) {
        return false;
    }
    if (user_stage_setting_button_ && user_stage_setting_button_->Focused()) {
        CycleStageSetting();
        return true;
    }
    if (user_has_read_radio_ && user_has_read_radio_->Focused()) {
        SetReadQuestionEnabled(true);
        return true;
    }
    if (user_no_read_radio_ && user_no_read_radio_->Focused()) {
        SetReadQuestionEnabled(false);
        return true;
    }
    if (user_mission_setting_button_ && user_mission_setting_button_->Focused()) {
        CycleMissionSetting();
        return true;
    }
    if (user_phone_area_ && user_phone_area_->Focused()) {
        ShowKeyboardForUserField(user_phone_area_);
        return true;
    }
    if (user_password_area_ && user_password_area_->Focused()) {
        ShowKeyboardForUserField(user_password_area_);
        return true;
    }
    if (user_code_area_ && user_code_area_->Focused()) {
        ShowKeyboardForUserField(user_code_area_);
        return true;
    }
    if (user_send_code_button_ && user_send_code_button_->Focused()) {
        TriggerSendVerificationCode();
        return true;
    }
    if (user_login_button_ && user_login_button_->Focused()) {
        TriggerCompleteLogin();
        return true;
    }
    if (device_status_button_ && device_status_button_->Focused()) {
        TriggerStatusCheck();
        return true;
    }
    if (device_download_button_ && device_download_button_->Focused()) {
        TriggerResourceDownload();
        return true;
    }
    if (scan_button_ && scan_button_->Focused()) {
        PopulateScanResults();
        return true;
    }
    return false;
}

bool DeviceSettingApp::HandleListViewActivate(const ButtonEvent &event) {
    const bool action = (event.action == ButtonAction::Click) &&
                        (event.id == AppButton::Start || event.id == AppButton::C || event.id == AppButton::D);
    if (!action) {
        return false;
    }

    if (scan_list_ && scan_list_->Focused()) {
        if (event.id == AppButton::D) {
            return true;
        }
        const int index = scan_list_->SelectedIndex();
        if (index < 0 || index >= static_cast<int>(scan_ssids_raw_.size())) {
            return true;
        }
        const auto& ssid = scan_ssids_raw_[index];
        if (ssid.empty() || IsPlaceholderItem(ssid)) {
            return true;
        }
        ShowKeyboardForSsid(ssid);
        return true;
    }

    if (saved_list_ && saved_list_->Focused()) {
        const int index = saved_list_->SelectedIndex();
        if (index < 0 || index >= static_cast<int>(saved_networks_.size())) {
            return true;
        }
        const auto& ssid = saved_networks_[index];
        if (!ssid.empty() && !IsPlaceholderItem(ssid)) {
            if (event.id == AppButton::D) {
                ShowConfirmDialog("确认删除该网络吗？", "", ssid, true);
            } else {
                ShowConfirmDialog("确认连接到该网络吗？", "", ssid, false);
            }
        }
        return true;
    }
    return false;
}

void DeviceSettingApp::SendInputToUi(const ButtonEvent &event) {
    app_ui::KeyCode key{};
    if (!MapButtonToKey(event.id, key)) {
        return;
    }

    app_ui::InputEvent e;
    e.type = (event.action == ButtonAction::LongPress) ? app_ui::InputType::KeyRepeat : app_ui::InputType::KeyDown;
    e.key = static_cast<int>(key);
    ui_engine_.OnInput(e);
}

void DeviceSettingApp::ShowKeyboardForSsid(const std::string& ssid) {
    StopAutoCloseDialog();
    HideConfirmDialog();

    keyboard_visible_ = true;
    keyboard_mode_ = KeyboardMode::WifiPassword;
    keyboard_ignore_activation_once_ = false;
    active_ssid_ = ssid;
    active_user_input_ = nullptr;
    keyboard_field_title_.clear();
    password_input_.clear();

    if (keyboard_) {
        auto profile = keyboard_->Profile();
        profile.page = 0;
        keyboard_->SetProfile(profile);
        keyboard_->SetSelectedIndex(0);
        keyboard_->SetVisible(true);
    }
    if (input_dialog_) {
        input_dialog_->SetVisible(true);
    }
    if (ssid_label_) {
        ssid_label_->SetText(std::string("SSID名称：") + ssid);
        ssid_label_->SetVisible(true);
    }
    if (password_label_) {
        password_label_->SetText("输入密码：");
        password_label_->SetVisible(true);
    }
    if (password_area_) {
        password_area_->SetText("");
        password_area_->SetVisible(true);
    }
    SetInputStatus(kInputInitStatus);
    if (keyboard_) {
        ui_engine_.RequestFocus(keyboard_->Id());
    }
}

void DeviceSettingApp::ShowKeyboardForUserField(app_ui::TextAreaWidget* field) {
    if (field == nullptr) {
        return;
    }

    ESP_LOGI(kTag, "Open keyboard for user field: %s", UserFieldTitle(field).c_str());

    StopAutoCloseDialog();
    HideConfirmDialog();

    keyboard_visible_ = true;
    keyboard_mode_ = KeyboardMode::UserField;
    keyboard_ignore_activation_once_ = false;
    active_ssid_.clear();
    active_user_input_ = field;
    keyboard_field_title_ = UserFieldTitle(field);
    password_input_ = TextAreaText(field);

    if (keyboard_) {
        auto profile = keyboard_->Profile();
        profile.page = 0;
        keyboard_->SetProfile(profile);
        keyboard_->SetSelectedIndex(0);
        keyboard_->SetVisible(true);
    }
    if (input_dialog_) {
        input_dialog_->SetVisible(true);
    }
    if (ssid_label_) {
        ssid_label_->SetText(keyboard_field_title_);
        ssid_label_->SetVisible(true);
    }
    if (password_label_) {
        password_label_->SetText("输入内容：");
        password_label_->SetVisible(true);
    }
    if (password_area_) {
        password_area_->SetText(password_input_);
        password_area_->SetVisible(true);
    }
    SetInputStatus("方向键移动，按C输入字符，按Start退出");
    if (keyboard_) {
        ui_engine_.RequestFocus(keyboard_->Id());
    }
}

void DeviceSettingApp::ShowConfirmDialog(const std::string& title, const std::string& detail, const std::string& ssid,
                                         bool delete_action) {
    StopAutoCloseDialog();
    HideKeyboard();

    confirm_dialog_visible_ = true;
    confirm_delete_action_ = delete_action;
    pending_confirm_ssid_ = ssid;

    if (hint_dialog_) {
        hint_dialog_->SetVisible(true);
    }
    SetHintAlertLabel(detail.empty() ? title : (title + detail));

    if (button_submit_ && button_cancel_) {
        ui_engine_.RequestFocus(button_submit_->Id());
    }
}

void DeviceSettingApp::HideConfirmDialog() {
    confirm_dialog_visible_ = false;
    confirm_delete_action_ = false;
    pending_confirm_ssid_.clear();
    hint_alert_text_.clear();

    if (hint_dialog_) {
        hint_dialog_->SetVisible(false);
    }
    if (hint_alert_label_) {
        hint_alert_label_->SetVisible(false);
    }
}

void DeviceSettingApp::SetHintAlertLabel(const std::string& text) {
    hint_alert_text_ = text;
    RefreshHintAlertLabel();
}

void DeviceSettingApp::RefreshHintAlertLabel() {
    if (!hint_alert_label_ && hint_dialog_) {
        hint_alert_label_ = dynamic_cast<app_ui::LabelWidget*>(hint_dialog_->FindById(kWidgetHintAlert));
        if (!hint_alert_label_) {
            hint_alert_label_ = dynamic_cast<app_ui::LabelWidget*>(hint_dialog_->FindById(kWidgetPageAlert));
        }
        if (!hint_alert_label_) {
            for (const auto& child : hint_dialog_->Children()) {
                auto* label = dynamic_cast<app_ui::LabelWidget*>(child.get());
                if (label != nullptr) {
                    hint_alert_label_ = label;
                    break;
                }
            }
        }
    }
    if (!hint_alert_label_) {
        return;
    }
    if (hint_dialog_) {
        hint_dialog_->SetZOrder(25);
        hint_dialog_->SetVisible(confirm_dialog_visible_);
        hint_dialog_->MarkDirty();
    }
    hint_alert_label_->SetText(hint_alert_text_);
    hint_alert_label_->SetZOrder(30);
    hint_alert_label_->SetVisible(confirm_dialog_visible_);
    hint_alert_label_->MarkDirty();
}

void DeviceSettingApp::ConfirmPendingDialogAction() {
    if (!confirm_dialog_visible_ || pending_confirm_ssid_.empty()) {
        HideConfirmDialog();
        return;
    }

    if (confirm_delete_action_) {
        DeleteSavedSsid(pending_confirm_ssid_);
        HideConfirmDialog();
        return;
    }

    SetHintAlertLabel("网络连接中 60秒");
    confirm_connect_in_progress_ = true;
    confirm_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1200ULL * 1000ULL;

    const std::string password = GetSavedPasswordBySsid(pending_confirm_ssid_);
    if (password.empty()) {
        confirm_connect_in_progress_ = false;
        confirm_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1000ULL * 1000ULL;
        SetHintAlertLabel("连接失败: 未找到密码");
        return;
    }

    std::string error;
    if (!ConnectToSsidWithPassword(pending_confirm_ssid_, password, false, error, 60000, true, true)) {
        confirm_connect_in_progress_ = false;
        confirm_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1500ULL * 1000ULL;
        SetHintAlertLabel(std::string("连接失败: ") + error);
        return;
    }
    confirm_connect_in_progress_ = false;
    confirm_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1000ULL * 1000ULL;

    SetHintAlertLabel("连接成功");
    UpdateAlertStatusByNetwork();
    StartAutoCloseDialog(1000);
}

void DeviceSettingApp::HideKeyboard() {
    keyboard_visible_ = false;
    keyboard_mode_ = KeyboardMode::None;
    keyboard_ignore_activation_once_ = false;
    active_ssid_.clear();
    active_user_input_ = nullptr;
    keyboard_field_title_.clear();
    password_input_.clear();

    if (keyboard_) {
        keyboard_->SetVisible(false);
    }
    if (input_dialog_) {
        input_dialog_->SetVisible(false);
    }
    if (ssid_label_) {
        ssid_label_->SetVisible(false);
    }
    if (password_label_) {
        password_label_->SetVisible(false);
    }
    if (password_area_) {
        password_area_->SetVisible(false);
    }
}

void DeviceSettingApp::AppendPassword(const char* value) {
    if (!keyboard_visible_ || !value || !value[0]) {
        return;
    }
    password_input_ += value;
    ESP_LOGI(kTag, "Keyboard append: value=%s current_len=%u", value, static_cast<unsigned>(password_input_.size()));
    UpdatePasswordText();
}

void DeviceSettingApp::DeletePasswordChar() {
    if (!keyboard_visible_) {
        return;
    }
    if (password_input_.empty()) {
        HideKeyboard();
        return;
    }
    password_input_.pop_back();
    ESP_LOGI(kTag, "Keyboard delete: current_len=%u", static_cast<unsigned>(password_input_.size()));
    UpdatePasswordText();
}

void DeviceSettingApp::ConfirmPassword() {
    if (!keyboard_visible_) {
        return;
    }
    if (keyboard_mode_ == KeyboardMode::UserField) {
        if (active_user_input_ == nullptr) {
            return;
        }
        ESP_LOGI(kTag, "Close user keyboard and commit text len=%u to %s",
                 static_cast<unsigned>(password_input_.size()), UserFieldTitle(active_user_input_).c_str());
        SetTextAreaText(active_user_input_, password_input_);
        HideKeyboard();
        RefreshUserSettingsPage();
        return;
    }

    if (active_ssid_.empty()) {
        return;
    }

    const uint64_t now_us = static_cast<uint64_t>(esp_timer_get_time());
    if (now_us < submit_cooldown_until_us_) {
        SetInputStatus("正在连接中，请勿重复提交");
        return;
    }

    if (submit_connect_in_progress_) {
        SetInputStatus("正在连接中，请勿重复提交");
        return;
    }
    if (password_input_.empty()) {
        SetInputStatus("请输入密码后再提交");
        return;
    }

    submit_connect_in_progress_ = true;
    submit_cooldown_until_us_ = now_us + 1200ULL * 1000ULL;
    SetInputStatus("网络正在连接中 60秒");
    std::string error;
    if (!ConnectToSsidWithPassword(active_ssid_, password_input_, true, error, 60000, true)) {
        submit_connect_in_progress_ = false;
        submit_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1500ULL * 1000ULL;
        SetInputStatus(error);
        return;
    }
    submit_connect_in_progress_ = false;
    submit_cooldown_until_us_ = static_cast<uint64_t>(esp_timer_get_time()) + 1000ULL * 1000ULL;

    SetInputStatus("网络连接成功");
    UpdateAlertStatusByNetwork();
    StartAutoCloseDialog(1000);
}

void DeviceSettingApp::UpdatePasswordText() {
    if (password_area_) {
        password_area_->SetVisible(true);
        password_area_->SetText(password_input_);
        password_area_->MarkDirty();
    }
    if (keyboard_mode_ == KeyboardMode::UserField && active_user_input_ != nullptr) {
        active_user_input_->SetText(password_input_);
    }
    if (input_dialog_) {
        input_dialog_->MarkDirty();
    }
}

bool DeviceSettingApp::IsUserSettingsScene() const {
    return user_stage_setting_button_ != nullptr || user_mission_setting_button_ != nullptr;
}

bool DeviceSettingApp::IsDeviceInfoScene() const {
    return device_status_button_ != nullptr || device_download_button_ != nullptr;
}

std::string DeviceSettingApp::TextAreaText(app_ui::TextAreaWidget* widget) const {
    if (widget == nullptr) {
        return {};
    }
    std::string text = widget->Text();
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    if (IsUserFieldPlaceholder(text)) {
        return {};
    }
    return text;
}

void DeviceSettingApp::SetTextAreaText(app_ui::TextAreaWidget* widget, const std::string& text) {
    if (widget != nullptr) {
        widget->SetText(text);
        ESP_LOGI(kTag, "TextArea updated widget=0x%08lx text=%s",
                 static_cast<unsigned long>(widget->Id()), text.c_str());
    }
}

std::string DeviceSettingApp::UserFieldTitle(app_ui::TextAreaWidget* widget) const {
    if (widget == user_phone_area_) {
        return "手机号";
    }
    if (widget == user_password_area_) {
        return "密码";
    }
    if (widget == user_code_area_) {
        return "验证码";
    }
    return "输入内容";
}

void DeviceSettingApp::RefreshUserSettingsPage() {
    if (!IsUserSettingsScene()) {
        return;
    }

    if (user_title_label_) {
        user_title_label_->SetText("学习设置");
    }
    if (user_top_frame_) {
        user_top_frame_->SetText("");
    }
    if (user_bottom_frame_) {
        user_bottom_frame_->SetText("");
    }
    if (user_phone_caption_label_) {
        user_phone_caption_label_->SetVisible(false);
    }
    if (user_password_caption_label_) {
        user_password_caption_label_->SetVisible(false);
    }
    if (user_code_caption_label_) {
        user_code_caption_label_->SetVisible(false);
    }
    if (user_phone_area_) {
        user_phone_area_->SetVisible(false);
    }
    if (user_password_area_) {
        user_password_area_->SetVisible(false);
    }
    if (user_code_area_) {
        user_code_area_->SetVisible(false);
    }
    if (user_send_code_button_) {
        user_send_code_button_->SetVisible(false);
    }
    if (user_login_button_) {
        user_login_button_->SetVisible(false);
    }
    if (user_status_label_) {
        user_status_label_->SetText("当前阶段: " + StageKey(CurrentStageIndex()));
    }
    if (user_name_label_) {
        user_name_label_->SetText(std::string("口语题: ") + (user_json_.enable_read_questions ? "开启" : "关闭"));
    }
    if (user_phone_label_) {
        user_phone_label_->SetText("今日任务: " + BuildMissionLabel(user_json_.today_mission.daily_new_word_target, user_json_.today_mission.daily_review_word_target, user_json_.today_mission.daily_total_target));
    }
    if (user_mode_label_) {
        user_mode_label_->SetText("按 C 切换当前焦点项");
    }
    if (user_stage_setting_button_) {
        user_stage_setting_button_->SetVisible(true);
        user_stage_setting_button_->SetText("阶段 " + StageKey(CurrentStageIndex()));
        auto profile = user_stage_setting_button_->Profile();
        profile.focus_invert = true;
        user_stage_setting_button_->SetProfile(profile);
    }
    if (user_mission_setting_button_) {
        user_mission_setting_button_->SetVisible(true);
        user_mission_setting_button_->SetText(BuildMissionLabel(user_json_.today_mission.daily_new_word_target, user_json_.today_mission.daily_review_word_target, user_json_.today_mission.daily_total_target));
        auto profile = user_mission_setting_button_->Profile();
        profile.focus_invert = true;
        user_mission_setting_button_->SetProfile(profile);
    }
    if (user_has_read_radio_) {
        user_has_read_radio_->SetVisible(true);
        user_has_read_radio_->SetText("口语开");
        auto profile = user_has_read_radio_->Profile();
        profile.checked = user_json_.enable_read_questions;
        profile.focus_invert = true;
        user_has_read_radio_->SetProfile(profile);
    }
    if (user_no_read_radio_) {
        user_no_read_radio_->SetVisible(true);
        user_no_read_radio_->SetText("口语关");
        auto profile = user_no_read_radio_->Profile();
        profile.checked = !user_json_.enable_read_questions;
        profile.focus_invert = true;
        user_no_read_radio_->SetProfile(profile);
    }
}

void DeviceSettingApp::RefreshDeviceInfoPage() {
    if (!IsDeviceInfoScene()) {
        return;
    }

    if (device_model_label_) {
        device_model_label_->SetText("型号: " + Board::GetInstance().GetBoardType());
    }
    if (device_id_label_) {
        const std::string device_id = et_server_client_.DeviceId();
        device_id_label_->SetText(std::string("编号: ") + (device_id.empty() ? SystemInfo::GetMacAddress() : device_id));
    }
    if (device_resource_label_) {
        std::string resource = et_server_client_.InstalledResourceName();
        if (!et_server_client_.InstalledResourceVersion().empty()) {
            if (!resource.empty()) {
                resource += "@";
            }
            resource += et_server_client_.InstalledResourceVersion();
        }
        if (resource.empty()) {
            resource = et_server_client_.ResourceName();
            if (!et_server_client_.ResourceVersion().empty()) {
                if (!resource.empty()) {
                    resource += "@";
                }
                resource += et_server_client_.ResourceVersion();
            }
        }
        device_resource_label_->SetText(std::string("资源: ") + (resource.empty() ? "-" : resource));
    }
    if (device_activation_label_) {
        std::string activation = et_server_client_.ActivationStatus();
        if (activation.empty()) {
            activation = "未检查";
        }
        device_activation_label_->SetText(std::string("激活: ") + activation);
    }
}

void DeviceSettingApp::RefreshClientUiState() {
    RefreshUserSettingsPage();
    RefreshDeviceInfoPage();
}

bool DeviceSettingApp::LoadUserJson() {
    user_json_ = {};
    user_json_.stage_levelup_count.assign(kDefaultStageLevelupCount.begin(), kDefaultStageLevelupCount.end());
    user_json_.stage_words_quantity.assign(12, 0);
    user_json_.stage_new_word_cursor.assign(12, 0);
    user_json_.today_mission.daily_new_word_target = kDefaultDailyNewWordTarget;
    user_json_.today_mission.daily_review_word_target = kDefaultDailyReviewWordTarget;
    user_json_.today_mission.daily_total_target = kDefaultDailyTotalTarget;
    user_json_.today_mission.target_words = kDefaultDailyTotalTarget;
    const std::string content = ReadFileToString(kUserJsonPath);
    if (content.empty()) {
        return SaveUserJson();
    }

    cJSON* root = cJSON_Parse(content.c_str());
    if (!root) {
        return SaveUserJson();
    }

    cJSON* users = cJSON_GetObjectItemCaseSensitive(root, "users");
    if (cJSON_IsObject(users)) {
        user_json_.name = JsonStringOrDefault(users, "name", user_json_.name);
        user_json_.current_stage = JsonStringOrDefault(users, "current_stage", user_json_.current_stage);
        user_json_.level = std::max(0, JsonIntOrDefault(users, "level", user_json_.level));
        cJSON* today_mission = cJSON_GetObjectItemCaseSensitive(users, "today_mission");
        user_json_.today_mission.daily_new_word_target = ReadDailyNewWordTarget(today_mission, user_json_.today_mission.daily_new_word_target);
        user_json_.today_mission.daily_total_target = ReadLegacyDailyTotalTarget(today_mission, user_json_.today_mission.daily_total_target);
        user_json_.today_mission.daily_review_word_target = ReadDailyReviewWordTarget(
            today_mission,
            user_json_.today_mission.daily_new_word_target,
            user_json_.today_mission.daily_total_target,
            user_json_.today_mission.daily_review_word_target);
    }

    cJSON* learning_preferences = cJSON_GetObjectItemCaseSensitive(root, "learning_preferences");
    if (cJSON_IsObject(learning_preferences)) {
        user_json_.enable_read_questions = JsonBoolOrDefault(
            learning_preferences,
            "enable_read_questions",
            user_json_.enable_read_questions);
        user_json_.today_mission.daily_new_word_target = ReadDailyNewWordTarget(learning_preferences, user_json_.today_mission.daily_new_word_target);
        user_json_.today_mission.daily_total_target = ReadLegacyDailyTotalTarget(learning_preferences, user_json_.today_mission.daily_total_target);
        user_json_.today_mission.daily_review_word_target = ReadDailyReviewWordTarget(
            learning_preferences,
            user_json_.today_mission.daily_new_word_target,
            user_json_.today_mission.daily_total_target,
            user_json_.today_mission.daily_review_word_target);

        cJSON* preference_mission = cJSON_GetObjectItemCaseSensitive(learning_preferences, "today_mission");
        user_json_.today_mission.daily_new_word_target = ReadDailyNewWordTarget(preference_mission, user_json_.today_mission.daily_new_word_target);
        user_json_.today_mission.daily_total_target = ReadLegacyDailyTotalTarget(preference_mission, user_json_.today_mission.daily_total_target);
        user_json_.today_mission.daily_review_word_target = ReadDailyReviewWordTarget(
            preference_mission,
            user_json_.today_mission.daily_new_word_target,
            user_json_.today_mission.daily_total_target,
            user_json_.today_mission.daily_review_word_target);
    }

    cJSON* settings = cJSON_GetObjectItemCaseSensitive(root, "settings");
    if (cJSON_IsObject(settings)) {
        user_json_.enable_read_questions = JsonBoolOrDefault(settings, "enable_read_questions", user_json_.enable_read_questions);
    }

    cJSON* practice_stats = cJSON_GetObjectItemCaseSensitive(root, "practice_stats");
    if (cJSON_IsObject(practice_stats)) {
        user_json_.today_progress_percent = std::max(0, JsonIntOrDefault(practice_stats, "today_progress_percent", user_json_.today_progress_percent));
        user_json_.mastered_words = std::max(0, JsonIntOrDefault(practice_stats, "mastered_words", user_json_.mastered_words));
        user_json_.practice_stats.continuous_days = std::max(1, JsonIntOrDefault(practice_stats, "continuous_days", user_json_.practice_stats.continuous_days));
        user_json_.practice_stats.last_practice_date = JsonStringOrDefault(practice_stats, "last_practice_date", user_json_.practice_stats.last_practice_date);
    }

    LoadIntArrayFromJson(root, "stage_levelup_count", &user_json_.stage_levelup_count, 20);
    LoadIntArrayFromJson(root, "stage_words_quantity", &user_json_.stage_words_quantity, 0);
    LoadIntArrayFromJson(root, "stage_new_word_cursor", &user_json_.stage_new_word_cursor, 0);
    EnsureIntVectorSize(&user_json_.stage_levelup_count, 12, 20);
    EnsureIntVectorSize(&user_json_.stage_words_quantity, 12, 0);
    EnsureIntVectorSize(&user_json_.stage_new_word_cursor, 12, 0);
    NormalizeTodayMissionTargets(
        &user_json_.today_mission.daily_new_word_target,
        &user_json_.today_mission.daily_review_word_target,
        &user_json_.today_mission.daily_total_target,
        &user_json_.today_mission.target_words);
    ESP_LOGI(kTag,
             "device_setting user.json loaded path=%s stage=%s new_target=%d review_target=%d completed=%d total_target=%d speak=%d preview=%s",
             kUserJsonPath,
             user_json_.current_stage.c_str(),
             user_json_.today_mission.daily_new_word_target,
             user_json_.today_mission.daily_review_word_target,
             user_json_.today_mission.completed_words,
             user_json_.today_mission.target_words,
             user_json_.enable_read_questions ? 1 : 0,
             BuildJsonLogPreview(content).c_str());
    cJSON_Delete(root);
    return true;
}

bool DeviceSettingApp::SaveUserJson() const {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON* users = cJSON_CreateObject();
    cJSON_AddStringToObject(users, "name", user_json_.name.c_str());
    cJSON_AddStringToObject(users, "current_stage", user_json_.current_stage.c_str());
    cJSON_AddNumberToObject(users, "level", user_json_.level);
    cJSON_AddItemToObject(root, "users", users);

    cJSON* settings = cJSON_CreateObject();
    cJSON_AddBoolToObject(settings, "enable_read_questions", user_json_.enable_read_questions);
    cJSON_AddItemToObject(root, "settings", settings);

    cJSON* learning_preferences = cJSON_CreateObject();
    cJSON_AddBoolToObject(learning_preferences, "enable_read_questions", user_json_.enable_read_questions);
    cJSON_AddNumberToObject(learning_preferences, "daily_new_word_target", std::max(1, user_json_.today_mission.daily_new_word_target));
    cJSON_AddNumberToObject(learning_preferences, "daily_review_word_target", std::max(0, user_json_.today_mission.daily_review_word_target));
    cJSON_AddNumberToObject(learning_preferences, "daily_total_target", std::max(1, user_json_.today_mission.daily_total_target));
    cJSON_AddItemToObject(root, "learning_preferences", learning_preferences);

    cJSON* devices = cJSON_CreateObject();
    cJSON_AddStringToObject(devices, "device_id", user_json_.device.device_id.c_str());
    cJSON_AddStringToObject(devices, "firmware", user_json_.device.firmware.c_str());
    cJSON_AddItemToObject(root, "devices", devices);

    cJSON* practice_stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(practice_stats, "today_progress_percent", user_json_.today_progress_percent);
    cJSON_AddNumberToObject(practice_stats, "mastered_words", user_json_.mastered_words);
    cJSON_AddNumberToObject(practice_stats, "continuous_days", user_json_.practice_stats.continuous_days);
    cJSON_AddStringToObject(practice_stats, "last_practice_date", user_json_.practice_stats.last_practice_date.c_str());
    cJSON_AddItemToObject(root, "practice_stats", practice_stats);

    cJSON* levelup_array = cJSON_CreateArray();
    for (int value : user_json_.stage_levelup_count) {
        cJSON_AddItemToArray(levelup_array, cJSON_CreateNumber(value));
    }
    cJSON_AddItemToObject(root, "stage_levelup_count", levelup_array);

    cJSON* quantity_array = cJSON_CreateArray();
    for (int value : user_json_.stage_words_quantity) {
        cJSON_AddItemToArray(quantity_array, cJSON_CreateNumber(value));
    }
    cJSON_AddItemToObject(root, "stage_words_quantity", quantity_array);

    cJSON* cursor_array = cJSON_CreateArray();
    for (int value : user_json_.stage_new_word_cursor) {
        cJSON_AddItemToArray(cursor_array, cJSON_CreateNumber(value));
    }
    cJSON_AddItemToObject(root, "stage_new_word_cursor", cursor_array);

    char* printed = cJSON_Print(root);
    const std::string output = printed ? printed : "{}";
    if (printed) {
        cJSON_free(printed);
    }
    cJSON_Delete(root);
    const bool ok = WriteStringToFile(kUserJsonPath, output);
    ESP_LOGI(kTag,
             "device_setting user.json save %s path=%s stage=%s new_target=%d review_target=%d completed=%d total_target=%d speak=%d preview=%s",
             ok ? "ok" : "failed",
             kUserJsonPath,
             user_json_.current_stage.c_str(),
             user_json_.today_mission.daily_new_word_target,
             user_json_.today_mission.daily_review_word_target,
             user_json_.today_mission.completed_words,
             user_json_.today_mission.target_words,
             user_json_.enable_read_questions ? 1 : 0,
             BuildJsonLogPreview(output).c_str());
    return ok;
}

int DeviceSettingApp::CurrentStageIndex() const {
    return ParseStageIndex(user_json_.current_stage);
}

void DeviceSettingApp::CycleStageSetting() {
    user_json_.current_stage = StageKey(CurrentStageIndex() % 12 + 1);
    (void)SaveUserJson();
    RefreshUserSettingsPage();
}

void DeviceSettingApp::CycleMissionSetting() {
    int next_index = 0;
    for (size_t i = 0; i < kMissionPresets.size(); ++i) {
        if (user_json_.today_mission.daily_total_target == kMissionPresets[i].first &&
            user_json_.today_mission.daily_review_word_target == kMissionPresets[i].second) {
            next_index = static_cast<int>((i + 1) % kMissionPresets.size());
            break;
        }
    }
    user_json_.today_mission.daily_total_target = kMissionPresets[static_cast<size_t>(next_index)].first;
    user_json_.today_mission.daily_review_word_target = kMissionPresets[static_cast<size_t>(next_index)].second;
    user_json_.today_mission.daily_new_word_target = std::max(1, user_json_.today_mission.daily_total_target - user_json_.today_mission.daily_review_word_target);
    user_json_.today_mission.target_words = user_json_.today_mission.daily_total_target;
    (void)SaveUserJson();
    RefreshUserSettingsPage();
}

void DeviceSettingApp::SetReadQuestionEnabled(bool enabled) {
    user_json_.enable_read_questions = enabled;
    (void)SaveUserJson();
    RefreshUserSettingsPage();
}

void DeviceSettingApp::TriggerStatusCheck() {
    ESP_LOGI(kTag, "Trigger status check");
    SetBottomBarHint("状态检查中...");
    std::string message;
    std::string error;
    if (!et_server_client_.RefreshStatus(message, error)) {
        SetBottomBarHint(error);
    } else {
        SetBottomBarHint(message.empty() ? "状态检查完成" : message);
    }
    RefreshClientUiState();
}

void DeviceSettingApp::TriggerSendVerificationCode() {
    const std::string phone = TextAreaText(user_phone_area_);
    ESP_LOGI(kTag, "Trigger send verification code, phone=%s", phone.c_str());
    if (phone.empty()) {
        ESP_LOGW(kTag, "Send verification code rejected: empty phone input");
        SetBottomBarHint("请先输入手机号");
        return;
    }

    SetBottomBarHint("验证码发送中...");
    std::string message;
    std::string error;
    if (!et_server_client_.SendVerificationCode(phone, message, error)) {
        SetBottomBarHint(error);
    } else {
        SetBottomBarHint(message.empty() ? "验证码已发送" : message);
    }
    RefreshUserSettingsPage();
}

void DeviceSettingApp::TriggerCompleteLogin() {
    const std::string phone = TextAreaText(user_phone_area_);
    const std::string password = TextAreaText(user_password_area_);
    const std::string code = TextAreaText(user_code_area_);
    ESP_LOGI(kTag, "Trigger login/register, phone=%s code_len=%u password_len=%u",
             phone.c_str(), static_cast<unsigned>(code.size()), static_cast<unsigned>(password.size()));
    if (phone.empty() || password.empty() || code.empty()) {
        ESP_LOGW(kTag, "Login/register rejected: phone_empty=%d password_empty=%d code_empty=%d",
                 phone.empty(), password.empty(), code.empty());
        SetBottomBarHint("请先填写手机号、密码和验证码");
        return;
    }

    SetBottomBarHint("提交中...");
    EtServerLoginActionResult result = EtServerLoginActionResult::LoggedIn;
    std::string message;
    std::string error;
    if (!et_server_client_.CompleteLoginOrRegister(phone, password, code, result, message, error)) {
        SetBottomBarHint(error);
    } else if (result == EtServerLoginActionResult::RegisteredNeedLoginCode) {
        SetTextAreaText(user_code_area_, "");
        SetBottomBarHint(message.empty() ? "注册成功，请输入新的登录验证码" : message);
    } else {
        SetTextAreaText(user_code_area_, "");
        SetBottomBarHint(message.empty() ? "登录成功" : message);
    }
    RefreshClientUiState();
}

void DeviceSettingApp::TriggerResourceDownload() {
    ESP_LOGI(kTag, "Trigger resource download");
    SetBottomBarHint("资源下载中...");
    std::string message;
    std::string error;
    if (!et_server_client_.DownloadCurrentResource(message, error)) {
        SetBottomBarHint(error);
    } else {
        SetBottomBarHint(message.empty() ? "资源下载完成" : message);
    }
    RefreshDeviceInfoPage();
}

bool DeviceSettingApp::ConnectToSsidWithPassword(const std::string& ssid,
                                                  const std::string& password,
                                                  bool save_on_success,
                                                  std::string& error,
                                                  int timeout_ms,
                                                  bool show_countdown,
                                                  bool countdown_on_hint_dialog) {
    error.clear();
    if (ssid.empty()) {
        error = "SSID为空";
        return false;
    }

    auto& ssid_manager = SsidManager::GetInstance();
    const auto original_list = ssid_manager.GetSsidList();

    auto restore_ssids = [&ssid_manager, &original_list]() {
        ssid_manager.Clear();
        for (auto it = original_list.rbegin(); it != original_list.rend(); ++it) {
            ssid_manager.AddSsid(it->ssid, it->password);
        }
    };

    ssid_manager.Clear();
    ssid_manager.AddSsid(ssid, password);

    auto& wifi = WifiManager::GetInstance();
    if (!wifi.Initialize()) {
        restore_ssids();
        error = "WiFi初始化失败";
        return false;
    }

    connect_wait_connected_ = false;
    connect_wait_failed_ = false;
    connect_wait_reason_ = static_cast<uint8_t>(WIFI_REASON_UNSPECIFIED);

    if (connect_disconnected_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, connect_disconnected_handler_);
        connect_disconnected_handler_ = nullptr;
    }
    if (connect_got_ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, connect_got_ip_handler_);
        connect_got_ip_handler_ = nullptr;
    }

    esp_event_handler_instance_register(
        WIFI_EVENT,
        WIFI_EVENT_STA_DISCONNECTED,
        &DeviceSettingApp::OnConnectWifiEvent,
        this,
        &connect_disconnected_handler_);
    esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &DeviceSettingApp::OnConnectIpEvent,
        this,
        &connect_got_ip_handler_);

    if (wifi.IsConnected()) {
        const std::string current_ssid = wifi.GetSsid();
        ESP_LOGI(kTag, "Stop current station connection (%s) before credential verification for %s",
                 current_ssid.c_str(), ssid.c_str());
        wifi.StopStation();
    }

    
    wifi.StartStation();

    const int effective_timeout_ms = (timeout_ms > 0) ? timeout_ms : 12000;
    const int step_ms = 100;
    int elapsed_ms = 0;
    int last_remain_sec = -1;
    bool connected = false;
    bool connected_to_other_ssid = false;
    std::string other_ssid;
    while (elapsed_ms < effective_timeout_ms) {
        if (show_countdown) {
            const int remain_sec = std::max(0, (effective_timeout_ms - elapsed_ms + 999) / 1000);
            if (remain_sec != last_remain_sec) {
                const std::string msg = std::string("网络正在连接中 ") + std::to_string(remain_sec) + "秒";
                if (countdown_on_hint_dialog) {
                    SetHintAlertLabel(msg);
                } else {
                    SetInputStatus(msg);
                }
                last_remain_sec = remain_sec;
            }
        }
        if (connect_wait_connected_ || wifi.IsConnected()) {
            const std::string current_ssid = wifi.GetSsid();
            if (!current_ssid.empty() && current_ssid == ssid) {
                connected = true;
                break;
            }
            if (!current_ssid.empty()) {
                connected_to_other_ssid = true;
                other_ssid = current_ssid;
            }
        }
        if (connect_wait_failed_) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(step_ms));
        elapsed_ms += step_ms;
    }

    const bool keep_station_running = connected;
    if (!keep_station_running) {
        wifi.StopStation();
    }

    if (connect_disconnected_handler_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, connect_disconnected_handler_);
        connect_disconnected_handler_ = nullptr;
    }
    if (connect_got_ip_handler_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, connect_got_ip_handler_);
        connect_got_ip_handler_ = nullptr;
    }

    restore_ssids();

    if (connected) {
        if (save_on_success) {
            ssid_manager.AddSsid(ssid, password);
        }

        const auto& list = ssid_manager.GetSsidList();
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].ssid == ssid) {
                if (i != 0) {
                    ssid_manager.SetDefaultSsid(static_cast<int>(i));
                }
                break;
            }
        }
        last_connected_ssid_.clear();
        PopulateSavedNetworks();
        RefreshNetworkListDisplay();
        UpdateAlertStatusByNetwork();
        return true;
    }

    wifi.StartStation();
    if (connect_wait_failed_) {
        error = ConnectErrorToText(connect_wait_reason_);
    } else if (connected_to_other_ssid && !other_ssid.empty()) {
        error = std::string("连接到了其他网络: ") + other_ssid;
    } else {
        error = "连接超时";
    }
    return false;
}

void DeviceSettingApp::ConnectToSavedSsid(const std::string& ssid) {
    const std::string password = GetSavedPasswordBySsid(ssid);
    if (password.empty()) {
        return;
    }
    std::string error;
    (void)ConnectToSsidWithPassword(ssid, password, false, error);
}

void DeviceSettingApp::DeleteSavedSsid(const std::string& ssid) {
    if (ssid.empty() || IsPlaceholderItem(ssid)) {
        return;
    }

    auto& ssid_manager = SsidManager::GetInstance();
    const auto& list = ssid_manager.GetSsidList();
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].ssid == ssid) {
            ssid_manager.RemoveSsid(static_cast<int>(i));
            break;
        }
    }
    PopulateSavedNetworks();
}

void DeviceSettingApp::StartAutoCloseDialog(int delay_ms) {
    if (delay_ms <= 0) {
        RunAutoCloseDialogNow();
        return;
    }

    if (dialog_auto_close_timer_ == nullptr) {
        esp_timer_create_args_t args = {
            .callback = &DeviceSettingApp::OnAutoCloseTimer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "dev_setting_dialog_close",
            .skip_unhandled_events = true
        };
        if (esp_timer_create(&args, &dialog_auto_close_timer_) != ESP_OK) {
            dialog_auto_close_timer_ = nullptr;
            return;
        }
    }

    esp_timer_stop(dialog_auto_close_timer_);
    esp_timer_start_once(dialog_auto_close_timer_, static_cast<uint64_t>(delay_ms) * 1000ULL);
}

void DeviceSettingApp::StopAutoCloseDialog() {
    if (dialog_auto_close_timer_ != nullptr) {
        esp_timer_stop(dialog_auto_close_timer_);
        esp_timer_delete(dialog_auto_close_timer_);
        dialog_auto_close_timer_ = nullptr;
    }
}

void DeviceSettingApp::RunAutoCloseDialogNow() {
    HideKeyboard();
    HideConfirmDialog();
    UpdateBottomBarHintByFocus();
    ui_engine_.RequestRender();
}

bool DeviceSettingApp::IsScanningInProgress() const {
    return scanning_in_progress_;
}

bool DeviceSettingApp::IsInputDialogVisible() const {
    return input_dialog_ && input_dialog_->Visible();
}

bool DeviceSettingApp::IsHintDialogVisible() const {
    return hint_dialog_ && hint_dialog_->Visible();
}

std::string DeviceSettingApp::GetSavedPasswordBySsid(const std::string& ssid) const {
    const auto& list = SsidManager::GetInstance().GetSsidList();
    for (const auto& item : list) {
        if (item.ssid == ssid) {
            return item.password;
        }
    }
    return {};
}

std::string DeviceSettingApp::ConnectErrorToText(uint8_t reason) const {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "密码输入错误";
        case WIFI_REASON_NO_AP_FOUND:
            return "未找到该网络";
        case WIFI_REASON_ASSOC_FAIL:
            return "连接失败(关联失败)";
        default:
            return "连接失败";
    }
}

std::string DeviceSettingApp::CurrentConnectedSsid() const {
    auto& wifi = WifiManager::GetInstance();
    if (!wifi.IsConnected()) {
        return {};
    }
    return wifi.GetSsid();
}

bool DeviceSettingApp::IsPlaceholderItem(const std::string& item) const {
    return item == "(none)" || item == "(empty)" || item == "(scan failed)";
}

void DeviceSettingApp::OnConnectWifiEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)event_base;
    if (event_id != WIFI_EVENT_STA_DISCONNECTED || !arg || !event_data) {
        return;
    }

    auto* app = static_cast<DeviceSettingApp*>(arg);
    auto* disconnected = static_cast<wifi_event_sta_disconnected_t*>(event_data);
    app->connect_wait_reason_ = disconnected->reason;

    switch (disconnected->reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_ASSOC_FAIL:
            app->connect_wait_failed_ = true;
            break;
        default:
            break;
    }
}

void DeviceSettingApp::OnConnectIpEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)event_base;
    (void)event_data;
    if (event_id != IP_EVENT_STA_GOT_IP || !arg) {
        return;
    }
    auto* app = static_cast<DeviceSettingApp*>(arg);
    app->connect_wait_connected_ = true;
}

void DeviceSettingApp::OnAutoCloseTimer(void* arg) {
    auto* app = static_cast<DeviceSettingApp*>(arg);
    if (!app) {
        return;
    }
    AppService::GetInstance().Schedule([app]() {
        app->RunAutoCloseDialogNow();
    });
}

void DeviceSettingApp::OnKeyboardKey(app_ui::SoftKeyboardWidget* widget, const char* value, void* ctx) {
    (void)widget;
    auto* app = static_cast<DeviceSettingApp*>(ctx);
    if (!app) {
        return;
    }
    ESP_LOGI(kTag, "Keyboard key callback value=%s", value ? value : "");
    app->AppendPassword(value);
}

std::unique_ptr<AppBase> MakeDeviceSettingApp() {
    return std::make_unique<DeviceSettingApp>();
}
