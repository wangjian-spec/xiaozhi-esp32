#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
//配置墨水屏和SD卡SPI通讯引脚
#define SPI_PIN_NUM_MOSI GPIO_NUM_14     //EPS32S3 DEMO
#define SPI_PIN_NUM_CLK  GPIO_NUM_17     //EPS32S3 DEMO
#define EPD_PIN_NUM_CS   GPIO_NUM_45     //EPS32S3 DEMO
#define EPD_PIN_NUM_DC   GPIO_NUM_46     //EPS32S3 DEMO
#define EPD_PIN_NUM_RST  GPIO_NUM_47     //EPS32S3 DEMO
#define EPD_PIN_NUM_BUSY GPIO_NUM_48     //EPS32S3 DEMO
#define SPI_PIN_NUM_MISO   GPIO_NUM_13     //ESP32S3 DEMO

//SD卡片选引脚
#define SD_PIN_NUM_CS GPIO_NUM_10       //ESP32S3 DEMO

// -------------------- EPD 面板型号选择（GxEPD2 必需） --------------------
// GxEPD2 必须在编译期选择具体屏幕驱动型号，否则无法实例化 display 对象。
// 如你的硬件不是 4.2" GDEY042T81，请把下面宏改为对应型号，并同步修改
// EPD 面板选择与驱动 include / using 在 boards/EnglishTeacher/english-teacher.cc 中。
#define ENGLISH_TEACHER_EPD_PANEL_GDEY042T81 1

#define AUDIO_INPUT_SAMPLE_RATE 16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#else

#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif

#define BUILTIN_LED_GPIO GPIO_NUM_43

// Direction buttons
#define BUTTON_UP_GPIO GPIO_NUM_0
#define BUTTON_LEFT_GPIO GPIO_NUM_1
#define BUTTON_DOWN_GPIO GPIO_NUM_2
#define BUTTON_RIGHT_GPIO GPIO_NUM_11

// Action buttons
#define BUTTON_A_GPIO GPIO_NUM_12
#define BUTTON_B_GPIO GPIO_NUM_18
#define BUTTON_C_GPIO GPIO_NUM_21
#define BUTTON_D_GPIO GPIO_NUM_38

// Menu buttons
#define BUTTON_SELECT_GPIO GPIO_NUM_39
#define BUTTON_START_GPIO GPIO_NUM_40

// Reuse legacy names to minimize code changes
#define BOOT_BUTTON_GPIO BUTTON_A_GPIO
#define TOUCH_BUTTON_GPIO BUTTON_B_GPIO

// Volume buttons
#define VOLUME_UP_BUTTON_GPIO GPIO_NUM_41
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_42

#define DISPLAY_SDA_PIN GPIO_NUM_NC
#define DISPLAY_SCL_PIN GPIO_NUM_NC
#define DISPLAY_WIDTH 128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT 32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT 64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT 64
#define SH1106
#else
#error "未选择 OLED 屏幕类型"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_44

#endif // _BOARD_CONFIG_H_
