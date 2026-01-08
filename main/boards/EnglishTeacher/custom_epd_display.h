#pragma once

#include <stdint.h>

#include <string_view>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <SPI.h>

#include "display/display.h"

#include <Adafruit_GFX.h>

#include "eteacher/font_manager/font_manager.h"

// GxEPD2 core
#include <GxEPD2_BW.h>

// Panel: Good Display 4.2" b/w 400x300, SSD1683
#include <gdey/GxEPD2_420_GDEY042T81.h>

// E-paper display (EPD) implementation.
// Standalone class that does NOT inherit any base class.
// It encapsulates EPD driver state and provides EPD-specific APIs.
class Epd {
public:
	using Color = uint16_t;
	using DrawCallback = void (*)(Adafruit_GFX& gfx, void* ctx);
	using Panel = GxEPD2_420_GDEY042T81;

	struct Pins {
		int8_t cs;
		int8_t dc;
		int8_t rst;
		int8_t busy;
	};

	explicit Epd(Pins pins);
	~Epd() = default;

	// Initialize panel and bind it to an SPI bus.
	// NOTE: This does NOT call spi.begin(); the board should do SPI begin once,
	//       because SD and EPD share the same SPI bus.
	bool Begin(SPIClass& spi, uint32_t spi_hz = 20 * 1000 * 1000, uint16_t reset_ms = 10);

	// Direct access to the underlying GxEPD2 driver (full/partial refresh APIs).
	// Example (full refresh):
	//   auto& d = display.Driver();
	//   d.setFullWindow(); d.firstPage(); do { d.fillScreen(GxEPD_WHITE); } while (d.nextPage());
	auto& Driver() { return gfx_; }
	const auto& Driver() const { return gfx_; }

	// Draw UTF-8 text (Chinese/English) using eteacher/font_manager built-in fonts.
	// - font_name: "wenquanyi_9pt" / "wenquanyi_11pt" (see font_manager)
	// - baseline_y: baseline Y coordinate
	// Returns: next x after drawing.
	int16_t DrawUtf8(int16_t x,
					 int16_t baseline_y,
					 std::string_view utf8,
					 std::string_view font_name = "wenquanyi_11pt",
					 Color color = GxEPD_BLACK);

	int16_t MeasureUtf8Width(std::string_view utf8, std::string_view font_name = "wenquanyi_11pt") const;

private:
	// Use full framebuffer height.
	//
	// Rationale:
	// - EpdManager uses GxEPD2's `display(false)` (full) and `displayWindow(...)` (partial)
	//   APIs. These APIs assume the framebuffer contains the whole screen content.
	// - Keeping the full framebuffer makes refresh logic simpler, easier to maintain,
	//   and avoids page-loop boilerplate scattered across business code.
	static constexpr uint16_t kPageHeight = Panel::HEIGHT;

	bool begun_ = false;

	GxEPD2_BW<Panel, kPageHeight> gfx_;
};

class CustomEpdDisplay : public Display, public Epd {
public:
	explicit CustomEpdDisplay(Epd::Pins pins);
	~CustomEpdDisplay() override;

	// Initialize panel and sync dimensions into Display.
	bool Begin(SPIClass& spi, uint32_t spi_hz = 20 * 1000 * 1000, uint16_t reset_ms = 10);

private:
	bool Lock(int timeout_ms = 0) override;
	void Unlock() override;

	SemaphoreHandle_t mutex_ = nullptr;
};