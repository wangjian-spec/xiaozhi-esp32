#include "custom_epd_display.h"

// Keep this module Arduino-friendly; it is only used by EnglishTeacher board.

CustomEpdDisplay::CustomEpdDisplay(Pins pins)
	: gfx_(Panel(pins.cs, pins.dc, pins.rst, pins.busy)) {
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
	// Bind to shared SPI bus; SD and EPD will cooperate via CS lines and SPI transactions.
	gfx_.init(
		0, /* serial_diag_bitrate: 0 disables serial debug */
		true, /* initial */
		reset_ms,
		false, /* pulldown_rst_mode */
		spi,
		SPISettings(spi_hz, MSBFIRST, SPI_MODE0));

	// Portrait orientation: rotate to 'up' (0 = default, 1=90cw, 2=180, 3=270cw)
	gfx_.setRotation(0);

	begun_ = true;
	width_ = gfx_.width();
	height_ = gfx_.height();
	Clear(false);
	return true;
}

void CustomEpdDisplay::Clear(bool partial) {
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

void CustomEpdDisplay::Sleep() {
	if (!begun_) {
		return;
	}
	// Put panel into lowest power state.
	gfx_.hibernate();
}
