#pragma once

// EnglishTeacher: EPD (e-paper) display wrapper.
//
// Goals:
// - Keep board code simple (one Begin() call).
// - Keep drawing usage familiar (Adafruit_GFX style).
// - Avoid leaking too much GxEPD2 internals while still being practical.

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <SPI.h>

#include "display/display.h"

// GxEPD2 core
#include <GxEPD2_BW.h>

// Panel: Good Display 4.2" b/w 400x300, SSD1683
#include <gdey/GxEPD2_420_GDEY042T81.h>

class CustomEpdDisplay : public Display {
public:
	struct Pins {
		int8_t cs;
		int8_t dc;
		int8_t rst;
		int8_t busy;
	};

	explicit CustomEpdDisplay(Pins pins);
	~CustomEpdDisplay() override;

	// Initialize panel and bind it to an SPI bus.
	// NOTE: This does NOT call spi.begin(); the board should do SPI begin once,
	//       because SD and EPD share the same SPI bus.
	bool Begin(SPIClass& spi, uint32_t spi_hz = 20 * 1000 * 1000, uint16_t reset_ms = 10);

	// Direct access to the underlying GxEPD2 driver (full/partial refresh APIs).
	// Example (full refresh):
	//   auto& d = display.Epd();
	//   d.setFullWindow(); d.firstPage(); do { d.fillScreen(GxEPD_WHITE); } while (d.nextPage());
	auto& Epd() { return gfx_; }
	const auto& Epd() const { return gfx_; }

	// Clear screen; when partial=true use partial update mode.
	void Clear(bool partial = false);
	void Sleep();

private:
	bool Lock(int timeout_ms = 0) override;
	void Unlock() override;

	// Keep RAM usage predictable: smaller page height reduces peak buffer usage.
	// 16 works well for large 4.2" panels while keeping updates reasonably fast.
	static constexpr uint16_t kPageHeight = 16;

	bool begun_ = false;
	SemaphoreHandle_t mutex_ = nullptr;

	using Panel = GxEPD2_420_GDEY042T81;
	GxEPD2_BW<Panel, kPageHeight> gfx_;
};
