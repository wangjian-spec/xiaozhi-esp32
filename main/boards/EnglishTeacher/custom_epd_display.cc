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
	// Clear full screen once.
	// NOTE: In GxEPD2, `display(false)` means full update waveform.
	gfx_.setFullWindow();
	gfx_.fillScreen(GxEPD_WHITE);
	gfx_.display(false);
	return true;
}

int16_t Epd::DrawUtf8(int16_t x,
					 int16_t baseline_y,
					 std::string_view utf8,
					 std::string_view font_name,
					 Color color) {
	if (!begun_) {
		return x;
	}
	const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
	if (!font || !font->Ready()) {
		return x;
	}
	return font->DrawUtf8(gfx_, x, baseline_y, utf8, color);
}

int16_t Epd::MeasureUtf8Width(std::string_view utf8, std::string_view font_name) const {
	const auto* font = eteacher::font_manager::GetBuiltinFont(font_name);
	if (!font || !font->Ready()) {
		return 0;
	}
	return font->MeasureUtf8Width(utf8);
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

void CustomEpdDisplay::SetChatMessageListener(ChatMessageListener* listener) {
	chat_listener_ = listener;
}

void CustomEpdDisplay::SetChatMessage(const char* role, const char* content) {
	if (chat_listener_ != nullptr) {
		chat_listener_->OnChatMessage(role, content);
	}
	Display::SetChatMessage(role, content);
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
