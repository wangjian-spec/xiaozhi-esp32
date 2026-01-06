//将arduino.h放在最前面，避免INADDR_NONE冲突
#include <Arduino.h>
#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "application.h"
#include "config.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

#include "button.h"
#include "eteacher/app_manager/app_manager.h"

// EnglishTeacher 板级：SD 与 EPD 共用同一条 SPI 总线
#include <SPI.h>
// SD：封装在 custom_sd_fat.h / .cc 中（基于 SdFat）
#include "custom_sd_fat.h"

// EPD：统一封装在 custom_epd_display.h / .cc 中
#include "custom_epd_display.h"

#include <esp_log.h>

#define TAG "EnglishTeacherBoard"
// -----------------------------------------------------------------

// iot_button 建议配置短按/长按阈值，0 可能导致部分事件不触发或表现不稳定。
constexpr uint16_t kBtnLongPressMs = 2000;
constexpr uint16_t kBtnShortPressMs = 50;

constexpr uint32_t kSharedSpiHz = 20 * 1000 * 1000;

namespace {

void BindLogOnlyButton(Button& button, const char* name) {
    button.OnPressDown([name]() { ESP_LOGW(TAG, "%s: PressDown", name); });
    button.OnPressUp([name]() { ESP_LOGW(TAG, "%s: PressUp", name); });
    button.OnClick([name]() { ESP_LOGW(TAG, "%s: Click", name); });
    button.OnLongPress([name]() { ESP_LOGW(TAG, "%s: LongPress", name); });
}
} // namespace

class EnglishTeacherBoard : public WifiBoard {
private:
    CustomEpdDisplay display_ = CustomEpdDisplay({
        (int8_t)EPD_PIN_NUM_CS,
        (int8_t)EPD_PIN_NUM_DC,
        (int8_t)EPD_PIN_NUM_RST,
        (int8_t)EPD_PIN_NUM_BUSY,
    });
    CustomSdFat sd_;

    // EnglishTeacher 的全部物理按键直接作为成员对象持有：
    // - 初始化更直观（无需 ButtonManager 中转）
    // - 回调绑定集中在 InitializeButtons()，便于维护
    Button up_button_;
    Button left_button_;
    Button down_button_;
    Button right_button_;
    Button boot_button_;        // 复用 A
    Button touch_button_;       // 复用 B
    Button c_button_;
    Button d_button_;
    Button select_button_;
    Button start_button_;
    Button volume_up_button_;
    Button volume_down_button_;

    void InitializeArduinoAndSharedSpi() {
        // Arduino framework is required by SdFat / GxEPD2 (vendored as IDF components).
        // Ensure it's initialized exactly once.
        static bool arduino_inited = false;
        if (!arduino_inited) {
            initArduino();
            arduino_inited = true;
        }

        // Configure shared-SPI devices' control pins early.
        // GxEPD2 may call digitalWrite() during init; on ESP32-Arduino the pin must
        // be configured with pinMode() first, otherwise __digitalWrite() logs errors.
        pinMode((int)SD_PIN_NUM_CS, OUTPUT);
        digitalWrite((int)SD_PIN_NUM_CS, HIGH);

        pinMode((int)EPD_PIN_NUM_CS, OUTPUT);
        pinMode((int)EPD_PIN_NUM_DC, OUTPUT);
        pinMode((int)EPD_PIN_NUM_RST, OUTPUT);
        pinMode((int)EPD_PIN_NUM_BUSY, INPUT);

        digitalWrite((int)EPD_PIN_NUM_CS, HIGH);
        digitalWrite((int)EPD_PIN_NUM_DC, HIGH);
        digitalWrite((int)EPD_PIN_NUM_RST, HIGH);

        // ESP32 Arduino SPI.begin(sck, miso, mosi) signature.
        SPI.begin((int)SPI_PIN_NUM_CLK, (int)SPI_PIN_NUM_MISO, (int)SPI_PIN_NUM_MOSI);
        ESP_LOGI(TAG, "Shared SPI ready: SCK=%d MISO=%d MOSI=%d", (int)SPI_PIN_NUM_CLK, (int)SPI_PIN_NUM_MISO, (int)SPI_PIN_NUM_MOSI);
    }

    void InitializeSdCard() {
        SdSpiConfig cfg((int)SD_PIN_NUM_CS, SHARED_SPI, kSharedSpiHz, &SPI);
        if (!sd_.Begin(cfg)) {
            ESP_LOGE(TAG, "SD init failed (CS=%d)", (int)SD_PIN_NUM_CS);
        } else {
            ESP_LOGI(TAG, "SD init OK (CS=%d)", (int)SD_PIN_NUM_CS);
        }
    }

    void InitializeEpd() {
        bool ok = display_.Begin(SPI, kSharedSpiHz);
        ESP_LOGI(TAG, "EPD init %s", ok ? "OK" : "FAILED");
    }
    void InitializeButtons() {
        auto &app_mgr = AppManager::GetInstance();

        up_button_.OnClick([&]() {
            ESP_LOGW(TAG, "UP: Click");
            app_mgr.HandleButton(AppButton::Up);
        });
        left_button_.OnClick([&]() {
            ESP_LOGW(TAG, "LEFT: Click");
            app_mgr.HandleButton(AppButton::Back);
        });
        down_button_.OnClick([&]() {
            ESP_LOGW(TAG, "DOWN: Click");
            app_mgr.HandleButton(AppButton::Down);
        });
        right_button_.OnClick([&]() {
            ESP_LOGW(TAG, "RIGHT: Click");
            app_mgr.HandleButton(AppButton::Select);
        });

        // BOOT_BUTTON_GPIO 复用 A 键：单击切换聊天/配网
        boot_button_.OnPressDown([]() { ESP_LOGW(TAG, "BOOT(A): PressDown"); });
        boot_button_.OnPressUp([]() { ESP_LOGW(TAG, "BOOT(A): PressUp"); });
        boot_button_.OnClick([this]() {
            ESP_LOGW(TAG, "BOOT(A): Click");
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // 长按 BOOT：直接进入配网模式（不依赖其它模块的 WiFi 状态 API）
        boot_button_.OnLongPress([this]() {
            ESP_LOGW(TAG, "BOOT(A): LongPress");
            EnterWifiConfigMode();
        });

        // TOUCH_BUTTON_GPIO 复用 B 键：按下开始说话，抬起结束
        touch_button_.OnPressDown([&]() {
            ESP_LOGW(TAG, "TOUCH(B): PressDown");
            AppManager::GetInstance().HandleButton(AppButton::Ptt);
        });
        touch_button_.OnPressUp([&]() {
            ESP_LOGW(TAG, "TOUCH(B): PressUp");
            AppManager::GetInstance().HandleButton(AppButton::Ptt);
        });

        // C/D/Start/Select：当前不绑定业务逻辑，仅打印事件
        BindLogOnlyButton(c_button_, "C");
        BindLogOnlyButton(d_button_, "D");
        select_button_.OnClick([&]() {
            ESP_LOGW(TAG, "SELECT: Click");
            AppManager::GetInstance().HandleButton(AppButton::Select);
        });
        start_button_.OnClick([&]() {
            ESP_LOGW(TAG, "START: Click");
            AppManager::GetInstance().HandleButton(AppButton::Back);
        });

        // 音量：单击 +/-10，长按到极值
        volume_up_button_.OnClick([&]() {
            ESP_LOGW(TAG, "VOLUME_UP: Click");
            AppManager::GetInstance().HandleButton(AppButton::Up);
        });
        volume_up_button_.OnLongPress([&]() {
            ESP_LOGW(TAG, "VOLUME_UP: LongPress");
            AppManager::GetInstance().HandleButton(AppButton::Up, true);
        });

        volume_down_button_.OnClick([&]() {
            ESP_LOGW(TAG, "VOLUME_DOWN: Click");
            AppManager::GetInstance().HandleButton(AppButton::Down);
        });
        volume_down_button_.OnLongPress([&]() {
            ESP_LOGW(TAG, "VOLUME_DOWN: LongPress");
            AppManager::GetInstance().HandleButton(AppButton::Down, true);
        });

    }

    // 物联网初始化，逐步迁移到 MCP 协议
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

public:
    EnglishTeacherBoard()
        // 注意：active_high 默认为 false（按下=低电平）。若你硬件是“按下=高电平”，需要把这里的 false 改成 true。
        : up_button_(BUTTON_UP_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , left_button_(BUTTON_LEFT_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , down_button_(BUTTON_DOWN_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , right_button_(BUTTON_RIGHT_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , boot_button_(BOOT_BUTTON_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , touch_button_(TOUCH_BUTTON_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , c_button_(BUTTON_C_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , d_button_(BUTTON_D_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , select_button_(BUTTON_SELECT_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , start_button_(BUTTON_START_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , volume_up_button_(VOLUME_UP_BUTTON_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
        , volume_down_button_(VOLUME_DOWN_BUTTON_GPIO, false, kBtnLongPressMs, kBtnShortPressMs) {

        InitializeButtons();
        InitializeTools();
        InitializeArduinoAndSharedSpi();
        InitializeSdCard();
        InitializeEpd();
    }

    // Expose EPD and SD to other modules.
    CustomEpdDisplay& GetEpdDisplay() { return display_; }
    CustomSdFat& GetSd() { return sd_; }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(
            AUDIO_INPUT_SAMPLE_RATE,    // mic 采样率
            AUDIO_OUTPUT_SAMPLE_RATE,   // speaker 采样率
            AUDIO_I2S_SPK_GPIO_BCLK,    // speaker BCLK
            AUDIO_I2S_SPK_GPIO_LRCK,    // speaker WS/LRCK
            AUDIO_I2S_SPK_GPIO_DOUT,    // speaker DATA OUT
            I2S_STD_SLOT_RIGHT,         // speaker channel
            AUDIO_I2S_MIC_GPIO_SCK,     // mic BCLK
            AUDIO_I2S_MIC_GPIO_WS,      // mic WS/LRCK
            AUDIO_I2S_MIC_GPIO_DIN,     // mic DATA IN
            I2S_STD_SLOT_LEFT           // mic channel
        );
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return &display_;
    }
};

DECLARE_BOARD(EnglishTeacherBoard);
