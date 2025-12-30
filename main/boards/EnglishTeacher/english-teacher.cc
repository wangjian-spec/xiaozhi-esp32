//将arduino.h放在最前面，避免INADDR_NONE冲突
#include <Arduino.h>
#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/oled_display.h"
#include "system_reset.h"
#include "application.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "assets/lang_config.h"

// EnglishTeacher 专用按钮， 使板级初始化逻辑集中在一个地方，便于维护与裁剪。
#include <driver/gpio.h>
#include "button.h"

// EnglishTeacher 板级：SD 与 EPD 共用同一条 SPI 总线
#include <SPI.h>
// 仅 EnglishTeacher 板级需要这些 Arduino 生态库的初始化实现
#include <SdFat.h>

// EPD：统一封装在 custom_epd_display.*，对外提供 GxEPD2 原生 API（Raw()）
#include "custom_epd_display.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>

#ifdef SH1106
#include <esp_lcd_panel_sh1106.h>
#endif

#define TAG "EnglishTeacherBoard"

// -----------------------------------------------------------------------------
// EnglishTeacher：将 SD/EPD 的初始化集中到板级文件：
// - 真实硬件是 SD 与 EPD 共享同一条 SPI 总线
// - SPI.begin(...) 的参数与调用时序应由板级统一维护
// - SdFat/GxEPD2 属于 Arduino 生态库，初始化路径也更贴近 board bring-up
// -----------------------------------------------------------------------------

namespace {
constexpr const char* kSdTag = "EnglishTeacherSd";
constexpr const char* kEpdTag = "EnglishTeacherEpd";
constexpr const char* kBtnTag = "EnglishTeacherButton";

// iot_button 建议配置短按/长按阈值，0 可能导致部分事件不触发或表现不稳定。
constexpr uint16_t kBtnLongPressMs = 2000;
constexpr uint16_t kBtnShortPressMs = 50;

bool InitSdCardOnSharedSpi(SPIClass& spi, int cs_pin, uint32_t max_sck_hz) {
    // 用函数内 static 避免全局 new：
    // - 只初始化一次
    // - 不引入堆碎片问题
    static SdFat sd;
    static bool ready = false;

    if (ready) {
        return true;
    }

    if (cs_pin < 0) {
        ESP_LOGW(kSdTag, "SD init skipped: invalid CS pin");
        return false;
    }

    // 关键点：SD 与 EPD 共用 SPI，总线初始化(引脚/host)由板级统一 SPI.begin() 负责。
    // 这里仅确保 SD 的 CS 处于非选中态，避免上电后总线被 SD 误占用。
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);

    const uint32_t max_sck = (max_sck_hz != 0) ? max_sck_hz : SD_SCK_MHZ(20);

    // SHARED_SPI：明确告诉 SdFat 该 SPI 总线会被其它外设共享。
    SdSpiConfig spi_cfg(cs_pin, SHARED_SPI, max_sck, &spi);

    ready = sd.begin(spi_cfg);
    if (!ready) {
        ESP_LOGW(kSdTag, "SdFat begin failed");
        return false;
    }

    ESP_LOGI(kSdTag, "SD init OK (SdFat, shared SPI)");
    return true;
}

bool InitEpdOnSharedSpi() {
    // 只保留一份 EPD 对象：由 CustomEpdDisplay 单例持有。
    // 这样后续其它模块若要画图，可以直接 english_teacher::CustomEpdDisplay::GetEpd().Raw() 拿到 GxEPD2 对象。
    auto& epd = english_teacher::CustomEpdDisplay::GetEpd();
    const bool ok = epd.Init(0, true, 2, false, false);
    if (ok) {
        ESP_LOGI(kEpdTag, "EPD init OK (GxEPD2, shared SPI)");
    }
    return ok;
}
} // namespace

class EnglishTeacherBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Display* display_ = nullptr;

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
        // 统一复用 CustomEpdDisplay 的 helper，避免同一逻辑在多处维护。
        // 注：SdFat 也依赖 Arduino core + SPI，因此这里在 SD/EPD 之前执行。
        english_teacher::CustomEpdDisplay::EnsureArduinoCore();
        english_teacher::CustomEpdDisplay::EnsureSharedSpi();
    }

    void InitializeSdCard() {
        // max_sck_hz=0 表示使用 SdFat 推荐默认值（当前为 20MHz）。
        // 需要更保守/更激进的频率时，可把 0 改为 SD_SCK_MHZ(x)。
        InitSdCardOnSharedSpi(SPI, (int)SD_PIN_NUM_CS, 0);
    }

    void InitializeEpd() {
        InitEpdOnSharedSpi();
    }

    void InitializeDisplayI2c() {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = DISPLAY_SDA_PIN,
            .scl_io_num = DISPLAY_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
    }

    void InitializeSsd1306Display() {
        // SSD1306 config
        esp_lcd_panel_io_i2c_config_t io_config = {
            .dev_addr = 0x3C,
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .control_phase_bytes = 1,
            .dc_bit_offset = 6,
            .lcd_cmd_bits = 8,
            .lcd_param_bits = 8,
            .flags = {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
            .scl_speed_hz = 400 * 1000,
        };

        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

        ESP_LOGI(TAG, "Install SSD1306 driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = -1;
        panel_config.bits_per_pixel = 1;

        esp_lcd_panel_ssd1306_config_t ssd1306_config = {
            .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
        };
        panel_config.vendor_config = &ssd1306_config;

#ifdef SH1106
        ESP_ERROR_CHECK(esp_lcd_new_panel_sh1106(panel_io_, &panel_config, &panel_));
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config, &panel_));
#endif
        ESP_LOGI(TAG, "SSD1306 driver installed");

        // Reset the display
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
        if (esp_lcd_panel_init(panel_) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize display");
            display_ = new NoDisplay();
            return;
        }
        ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));

        // Set the display to on
        ESP_LOGI(TAG, "Turning display on");
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

        display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
    }

    void InitializeButtons() {
        // 按键逻辑尽量直观：直接对成员 Button 绑定回调。

        // 确保按键日志可见：即使全局日志级别偏高，也至少让本 tag 输出。
        esp_log_level_set(kBtnTag, ESP_LOG_VERBOSE);

        ESP_LOGW(kBtnTag,
                 "Buttons init: UP=%d LEFT=%d DOWN=%d RIGHT=%d A=%d B=%d C=%d D=%d SEL=%d START=%d VUP=%d VDOWN=%d",
                 (int)BUTTON_UP_GPIO, (int)BUTTON_LEFT_GPIO, (int)BUTTON_DOWN_GPIO, (int)BUTTON_RIGHT_GPIO,
                 (int)BOOT_BUTTON_GPIO, (int)TOUCH_BUTTON_GPIO, (int)BUTTON_C_GPIO, (int)BUTTON_D_GPIO,
                 (int)BUTTON_SELECT_GPIO, (int)BUTTON_START_GPIO,
                 (int)VOLUME_UP_BUTTON_GPIO, (int)VOLUME_DOWN_BUTTON_GPIO);

        // 方向键：当前不绑定业务逻辑，仅打印事件
        up_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "UP: PressDown"); });
        up_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "UP: PressUp"); });
        up_button_.OnClick([]() { ESP_LOGW(kBtnTag, "UP: Click"); });
        up_button_.OnLongPress([]() { ESP_LOGW(kBtnTag, "UP: LongPress"); });

        left_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "LEFT: PressDown"); });
        left_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "LEFT: PressUp"); });
        left_button_.OnClick([]() { ESP_LOGW(kBtnTag, "LEFT: Click"); });
        left_button_.OnLongPress([]() { ESP_LOGW(kBtnTag, "LEFT: LongPress"); });

        down_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "DOWN: PressDown"); });
        down_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "DOWN: PressUp"); });
        down_button_.OnClick([]() { ESP_LOGW(kBtnTag, "DOWN: Click"); });
        down_button_.OnLongPress([]() { ESP_LOGW(kBtnTag, "DOWN: LongPress"); });

        right_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "RIGHT: PressDown"); });
        right_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "RIGHT: PressUp"); });
        right_button_.OnClick([]() { ESP_LOGW(kBtnTag, "RIGHT: Click"); });
        right_button_.OnLongPress([]() { ESP_LOGW(kBtnTag, "RIGHT: LongPress"); });

        // BOOT_BUTTON_GPIO 复用 A 键：单击切换聊天/配网
        boot_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "BOOT(A): PressDown"); });
        boot_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "BOOT(A): PressUp"); });
        boot_button_.OnClick([this]() {
            ESP_LOGW(kBtnTag, "BOOT(A): Click");
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });

        // 长按 BOOT：直接进入配网模式（不依赖其它模块的 WiFi 状态 API）
        boot_button_.OnLongPress([this]() {
            ESP_LOGW(kBtnTag, "BOOT(A): LongPress");
            EnterWifiConfigMode();
        });

        // TOUCH_BUTTON_GPIO 复用 B 键：按下开始说话，抬起结束
        touch_button_.OnPressDown([]() {
            ESP_LOGW(kBtnTag, "TOUCH(B): PressDown");
            Application::GetInstance().StartListening();
        });
        touch_button_.OnPressUp([]() {
            ESP_LOGW(kBtnTag, "TOUCH(B): PressUp");
            Application::GetInstance().StopListening();
        });

        // C/D/Start/Select：当前不绑定业务逻辑，仅打印事件
        c_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "C: PressDown"); });
        c_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "C: PressUp"); });
        c_button_.OnClick([]() { ESP_LOGW(kBtnTag, "C: Click"); });
        c_button_.OnLongPress([]() { ESP_LOGW(kBtnTag, "C: LongPress"); });

        d_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "D: PressDown"); });
        d_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "D: PressUp"); });
        d_button_.OnClick([]() { ESP_LOGW(kBtnTag, "D: Click"); });
        d_button_.OnLongPress([]() { ESP_LOGW(kBtnTag, "D: LongPress"); });

        select_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "SELECT: PressDown"); });
        select_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "SELECT: PressUp"); });
        select_button_.OnClick([]() { ESP_LOGW(kBtnTag, "SELECT: Click"); });
        select_button_.OnLongPress([]() { ESP_LOGW(kBtnTag, "SELECT: LongPress"); });

        start_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "START: PressDown"); });
        start_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "START: PressUp"); });
        start_button_.OnClick([]() { ESP_LOGW(kBtnTag, "START: Click"); });
        start_button_.OnLongPress([]() { ESP_LOGW(kBtnTag, "START: LongPress"); });

        // 音量：单击 +/-10，长按到极值
        volume_up_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "VOLUME_UP: PressDown"); });
        volume_up_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "VOLUME_UP: PressUp"); });
        volume_up_button_.OnClick([this]() {
            ESP_LOGW(kBtnTag, "VOLUME_UP: Click");
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() + 10;
            if (volume > 100) {
                volume = 100;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });
        volume_up_button_.OnLongPress([this]() {
            ESP_LOGW(kBtnTag, "VOLUME_UP: LongPress");
            GetAudioCodec()->SetOutputVolume(100);
            GetDisplay()->ShowNotification(Lang::Strings::MAX_VOLUME);
        });

        volume_down_button_.OnPressDown([]() { ESP_LOGW(kBtnTag, "VOLUME_DOWN: PressDown"); });
        volume_down_button_.OnPressUp([]() { ESP_LOGW(kBtnTag, "VOLUME_DOWN: PressUp"); });
        volume_down_button_.OnClick([this]() {
            ESP_LOGW(kBtnTag, "VOLUME_DOWN: Click");
            auto codec = GetAudioCodec();
            auto volume = codec->output_volume() - 10;
            if (volume < 0) {
                volume = 0;
            }
            codec->SetOutputVolume(volume);
            GetDisplay()->ShowNotification(Lang::Strings::VOLUME + std::to_string(volume));
        });
        volume_down_button_.OnLongPress([this]() {
            ESP_LOGW(kBtnTag, "VOLUME_DOWN: LongPress");
            GetAudioCodec()->SetOutputVolume(0);
            GetDisplay()->ShowNotification(Lang::Strings::MUTED);
        });

        // 注：目前仅打印日志；后续若绑定业务逻辑，保留日志即可更方便排查。
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
        // InitializeDisplayI2c();
        // InitializeSsd1306Display();
        display_ = new NoDisplay(); // Disable display for English Teacher Board
        InitializeButtons();
        InitializeTools();

        // SPI 外设初始化（SD + EPD）：对现有 OLED/I2C 逻辑零侵入
        InitializeArduinoAndSharedSpi();
        InitializeSdCard();
        InitializeEpd();
    }

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
        return display_;
    }
};

DECLARE_BOARD(EnglishTeacherBoard);
