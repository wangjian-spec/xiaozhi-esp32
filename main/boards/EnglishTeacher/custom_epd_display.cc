#include "custom_epd_display.h"

// Keep this module Arduino-friendly; it is only used by EnglishTeacher board.

Epd::Epd(Pins pins)
	: gfx_(Panel(pins.cs, pins.dc, pins.rst, pins.busy)) {}

bool Epd::Begin(SPIClass& spi, uint32_t spi_hz, uint16_t reset_ms) {
	// Bind to shared SPI bus; SD and EPD will cooperate via CS lines and SPI transactions.
	gfx_.init(
		0, /* serial_diag_bitrate: 0 disables serial debug */
		true, /* initial */
		reset_ms,
		false, /* pulldown_rst_mode */
		spi,
		SPISettings(spi_hz, MSBFIRST, SPI_MODE0));

	// Portrait orientation: rotate to 'up' (0 = default, 1=90cw, 2=180, 3=270cw).
	gfx_.setRotation(0);

	begun_ = true;
	Clear(false);
	return true;
}

void Epd::Clear(bool partial) {
	if (!begun_) {
		return;
	}

	if (partial) {
		gfx_.setPartialWindow(0, 0, gfx_.width(), gfx_.height());
	} else {
		gfx_.setFullWindow();
	}

	gfx_.firstPage();
	do {
		gfx_.fillScreen(GxEPD_WHITE);
	} while (gfx_.nextPage());
}

void Epd::DrawText(int16_t x, int16_t y, const char* text, Color color, Color bg, uint8_t text_size) {
	if (!begun_ || text == nullptr) {
		return;
	}
	gfx_.setTextSize(text_size);
	gfx_.setTextColor(color, bg);
	gfx_.setCursor(x, y);
	gfx_.print(text);
}

void Epd::DrawBitmap(int16_t x, int16_t y, const uint8_t* bitmap, int16_t w, int16_t h, Color color) {
	if (!begun_ || bitmap == nullptr) {
		return;
	}
	gfx_.drawBitmap(x, y, bitmap, w, h, color);
}

bool Epd::FullRefresh(DrawCallback cb, void* ctx, Color bg) {
	if (!begun_) {
		return false;
	}
	gfx_.setFullWindow();
	gfx_.firstPage();
	do {
		gfx_.fillScreen(bg);
		if (cb) {
			cb(gfx_, ctx);
		}
	} while (gfx_.nextPage());
	return true;
}

bool Epd::PartialRefresh(int16_t x, int16_t y, int16_t w, int16_t h, DrawCallback cb, void* ctx, Color bg) {
	if (!begun_) {
		return false;
	}
	gfx_.setPartialWindow(x, y, w, h);
	gfx_.firstPage();
	do {
		gfx_.fillScreen(bg);
		if (cb) {
			cb(gfx_, ctx);
		}
	} while (gfx_.nextPage());
	return true;
}

void Epd::Sleep() {
	if (!begun_) {
		return;
	}
	// Put panel into lowest power state.
	gfx_.hibernate();
}

CustomEpdDisplay::CustomEpdDisplay(Epd::Pins pins)
	: Epd(pins) {
	mutex_ = xSemaphoreCreateMutex();
}

CustomEpdDisplay::~CustomEpdDisplay() {
	if (mutex_ != nullptr) {
		vSemaphoreDelete(mutex_);
		mutex_ = nullptr;
	}
}

bool CustomEpdDisplay::Lock(int timeout_ms) {
	if (mutex_ == nullptr) {
		return true;
	}
	TickType_t ticks = (timeout_ms <= 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
	return xSemaphoreTake(mutex_, ticks) == pdTRUE;
}

void CustomEpdDisplay::Unlock() {
	if (mutex_ == nullptr) {
		return;
	}
	(void)xSemaphoreGive(mutex_);
}

bool CustomEpdDisplay::Begin(SPIClass& spi, uint32_t spi_hz, uint16_t reset_ms) {
	const bool ok = Epd::Begin(spi, spi_hz, reset_ms);
	if (ok) {
		width_ = Driver().width();
		height_ = Driver().height();
	}
	return ok;
}
