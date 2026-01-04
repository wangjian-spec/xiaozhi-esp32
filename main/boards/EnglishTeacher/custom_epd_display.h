#pragma once

#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <SPI.h>

#include "display/display.h"

#include <Adafruit_GFX.h>

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

	// Clear screen; when partial=true use partial update mode.
	void Clear(bool partial = false);

	// Full refresh interface: set full update window.
	void SetFullRefresh() { gfx_.setFullWindow(); }

	// Partial refresh interface: set partial update window.
	void SetPartialRefresh(int16_t x, int16_t y, int16_t w, int16_t h) { gfx_.setPartialWindow(x, y, w, h); }

	// Common drawing helpers (drawn into current page buffer).
	void DrawText(int16_t x, int16_t y, const char* text, Color color = GxEPD_BLACK, Color bg = GxEPD_WHITE, uint8_t text_size = 1);
	void DrawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, Color color = GxEPD_BLACK);

	// Common refresh helpers (wrap firstPage()/nextPage() boilerplate).
	// The callback should draw everything it needs for a single page.
	bool FullRefresh(DrawCallback cb, void* ctx = nullptr, Color bg = GxEPD_WHITE);
	bool PartialRefresh(int16_t x, int16_t y, int16_t w, int16_t h, DrawCallback cb, void* ctx = nullptr, Color bg = GxEPD_WHITE);

	// Put panel into lowest power state.
	void Sleep();

private:
	// Keep RAM usage predictable: smaller page height reduces peak buffer usage.
	// 16 works well for large 4.2" panels while keeping updates reasonably fast.
	static constexpr uint16_t kPageHeight = 16;

	bool begun_ = false;

	using Panel = GxEPD2_420_GDEY042T81;
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