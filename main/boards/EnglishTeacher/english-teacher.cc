// 将arduino.h放在最前面，避免INADDR_NONE冲突
#include <Arduino.h>

#include "english-teacher.h"

#include "assets/lang_config.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "eteacher/app_manager/app_manager.h"
#include "eteacher/epd_manager/epd_manager.h"
#include "lamp_controller.h"
#include "led/single_led.h"

#include <SPI.h>

#include <esp_log.h>

static constexpr const char* kTag = "EnglishTeacherBoard";

// iot_button 建议配置短按/长按阈值，0 可能导致部分事件不触发或表现不稳定。
static constexpr uint16_t kBtnLongPressMs = 2000;
static constexpr uint16_t kBtnShortPressMs = 100;

static constexpr uint32_t kSharedSpiHz = 20 * 1000 * 1000;

EnglishTeacherBoard::EnglishTeacherBoard()
	// 注意：active_high 默认为 false（按下=低电平）。若你硬件是“按下=高电平”，需要把这里的 false 改成 true。
	: display_(Epd::Pins{
		(int8_t)EPD_PIN_NUM_CS,
		(int8_t)EPD_PIN_NUM_DC,
		(int8_t)EPD_PIN_NUM_RST,
		(int8_t)EPD_PIN_NUM_BUSY,
	})
	, up_button_(BUTTON_UP_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
	, left_button_(BUTTON_LEFT_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
	, down_button_(BUTTON_DOWN_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
	, right_button_(BUTTON_RIGHT_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
	, a_button_(BUTTON_A_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
	, b_button_(BUTTON_B_GPIO, false, kBtnLongPressMs, kBtnShortPressMs)
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

Led* EnglishTeacherBoard::GetLed() {
	static SingleLed led(BUILTIN_LED_GPIO);
	return &led;
}

AudioCodec* EnglishTeacherBoard::GetAudioCodec() {
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

CustomEpdDisplay* EnglishTeacherBoard::GetDisplay() {
	return &display_;
}

CustomSdFat* EnglishTeacherBoard::GetSd() {
	return &sd_;
}

void EnglishTeacherBoard::InitializeArduinoAndSharedSpi() {
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
	ESP_LOGI(kTag, "Shared SPI ready: SCK=%d MISO=%d MOSI=%d", (int)SPI_PIN_NUM_CLK, (int)SPI_PIN_NUM_MISO,
			 (int)SPI_PIN_NUM_MOSI);
}

void EnglishTeacherBoard::InitializeSdCard() {
	SdSpiConfig cfg((int)SD_PIN_NUM_CS, SHARED_SPI, kSharedSpiHz, &SPI);
	if (!sd_.Begin(cfg)) {
		ESP_LOGE(kTag, "SD init failed (CS=%d)", (int)SD_PIN_NUM_CS);
	} else {
		ESP_LOGI(kTag, "SD init OK (CS=%d)", (int)SD_PIN_NUM_CS);
	}
}

void EnglishTeacherBoard::InitializeEpd() {
	bool ok = display_.Begin(SPI, kSharedSpiHz);
	ESP_LOGI(kTag, "EPD init %s", ok ? "OK" : "FAILED");
	if (ok) {
		EpdManager::GetInstance().Init(&display_);
	}
}

void EnglishTeacherBoard::InitializeButtons() {
	auto& app_mgr = AppManager::GetInstance();

	up_button_.OnClick([&]() {
		ESP_LOGW(kTag, "UP: Click");
		app_mgr.HandleButton(ButtonEvent{AppButton::Up});
	});
	left_button_.OnClick([&]() {
		ESP_LOGW(kTag, "LEFT: Click");
		app_mgr.HandleButton(ButtonEvent{AppButton::Left});
	});
	down_button_.OnClick([&]() {
		ESP_LOGW(kTag, "DOWN: Click");
		app_mgr.HandleButton(ButtonEvent{AppButton::Down});
	});
	right_button_.OnClick([&]() {
		ESP_LOGW(kTag, "RIGHT: Click");
		app_mgr.HandleButton(ButtonEvent{AppButton::Right});
	});


	a_button_.OnPressDown([&]() {
		ESP_LOGW(kTag, "BOOT(A): PressDown");
		app_mgr.HandleButton(ButtonEvent{AppButton::A, ButtonAction::PressDown});
	});
	a_button_.OnPressUp([&]() {
		ESP_LOGW(kTag, "BOOT(A): PressUp");
		app_mgr.HandleButton(ButtonEvent{AppButton::A, ButtonAction::PressUp});
	});
	a_button_.OnLongPress([&]() {
		ESP_LOGW(kTag, "BOOT(A): LongPress");
		app_mgr.HandleButton(ButtonEvent{AppButton::A, ButtonAction::LongPress});
	});
	a_button_.OnClick([&]() {
		ESP_LOGW(kTag, "BOOT(A): Click");
		app_mgr.HandleButton(ButtonEvent{AppButton::A, ButtonAction::Click});
	});
	a_button_.OnDoubleClick([&]() {
		ESP_LOGW(kTag, "BOOT(A): DoubleClick");
		app_mgr.HandleButton(ButtonEvent{AppButton::A, ButtonAction::DoubleClick, 2});
	});
	a_button_.OnMultipleClick([&]() {
		ESP_LOGW(kTag, "BOOT(A): MultipleClick");
		app_mgr.HandleButton(ButtonEvent{AppButton::A, ButtonAction::MultipleClick, 3});
	}, 3);

	// B: Click -> AppButton::B
	b_button_.OnClick([&]() {
		ESP_LOGW(kTag, "TOUCH(B): Click");
		AppManager::GetInstance().HandleButton(ButtonEvent{AppButton::B});
	});

	// C/D
	c_button_.OnClick([&]() {
		ESP_LOGW(kTag, "C: Click");
		AppManager::GetInstance().HandleButton(ButtonEvent{AppButton::C});
	});
	d_button_.OnClick([&]() {
		ESP_LOGW(kTag, "D: Click");
		AppManager::GetInstance().HandleButton(ButtonEvent{AppButton::D});
	});

	select_button_.OnClick([&]() {
		ESP_LOGW(kTag, "SELECT: Click");
		AppManager::GetInstance().HandleButton(ButtonEvent{AppButton::Select});
	});
	start_button_.OnClick([&]() {
		ESP_LOGW(kTag, "START: Click");
		AppManager::GetInstance().HandleButton(ButtonEvent{AppButton::Start});
	});
	start_button_.OnLongPress([&]() {
		ESP_LOGW(kTag, "START: LongPress");
		AppManager::GetInstance().HandleButton(ButtonEvent{AppButton::Start, ButtonAction::LongPress});
	});

	
	volume_up_button_.OnClick([&]() {
		ESP_LOGW(kTag, "VOLUME_UP: Click");
		auto *codec = GetAudioCodec();
		if (!codec) {
			return;
		}
		int volume = codec->output_volume() + 10;
		if (volume > 100) {
			volume = 100;
		}
		codec->SetOutputVolume(volume);
	});
	volume_up_button_.OnLongPress([&]() {
		ESP_LOGW(kTag, "VOLUME_UP: LongPress");
		auto *codec = GetAudioCodec();
		if (!codec) {
			return;
		}
		codec->SetOutputVolume(100);
	});

	volume_down_button_.OnClick([&]() {
		ESP_LOGW(kTag, "VOLUME_DOWN: Click");
		auto *codec = GetAudioCodec();
		if (!codec) {
			return;
		}
		int volume = codec->output_volume() - 10;
		if (volume < 0) {
			volume = 0;
		}
		codec->SetOutputVolume(volume);
	});
	volume_down_button_.OnLongPress([&]() {
		ESP_LOGW(kTag, "VOLUME_DOWN: LongPress");
		auto *codec = GetAudioCodec();
		if (!codec) {
			return;
		}
		codec->SetOutputVolume(0);
	});
}

void EnglishTeacherBoard::InitializeTools() {
	static LampController lamp(LAMP_GPIO);
}

DECLARE_BOARD(EnglishTeacherBoard);
