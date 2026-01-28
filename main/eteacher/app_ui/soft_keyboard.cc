#include "eteacher/app_ui/soft_keyboard.h"

#include <Adafruit_GFX.h>
#include <algorithm>
#include <array>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_ui/nav_sequence.h"
#include "eteacher/app_ui/status_bar.h"

namespace eteacher::app_ui {
namespace {

constexpr int kRows = 4;
constexpr int kCols = 9;
constexpr int kPages = 3;
constexpr int kKeyCount = kRows * kCols;

const char* kPage1[kKeyCount] = {
	"a","b","c","d","e","f","g","h","i",
	"j","k","l","m","n","o","p","q","r",
	"s","t","u","v","w","x","y","z","0",
	"1","2","3","4","5","6","7","8","9"
};

const char* kPage2[kKeyCount] = {
	"A","B","C","D","E","F","G","H","I",
	"J","K","L","M","N","O","P","Q","R",
	"S","T","U","V","W","X","Y","Z","!",
	"\"","#","$","%","&","'","(",")","*"
};

const char* kPage3[kKeyCount] = {
	"+",",","-",".","/",":",";","<","=",
	">","?","@","[","\\","]","^","_","`",
	"{","|","}","~","空格","","","","",
	"","","","","","","","",""
};

const char* const* PageData(int page) {
	switch (page) {
		case 0: return kPage1;
		case 1: return kPage2;
		default: return kPage3;
	}
}

} // namespace

void KeyboardWidget::ShowKeyboard(CustomEpdDisplay* epd, int16_t x, int16_t y, int16_t w, int16_t h) {
	last_epd_ = epd;
	bounds_ = {x, y, w, h};
	visible_ = true;
	ComputeLayout();
}

void KeyboardWidget::ShowKeyboard(CustomEpdDisplay* epd) {
	if (!epd) return;
	ShowKeyboard(epd, 0, 0, static_cast<int16_t>(epd->width()), static_cast<int16_t>(epd->height()));
}

void KeyboardWidget::CloseKeyboard() {
	visible_ = false;
}

bool KeyboardWidget::HandleButton(const ButtonEvent& event, std::string& output, bool* consumed) {
	if (!visible_) {
		if (consumed) *consumed = false;
		return false;
	}

	if (event.action != ButtonAction::Click) {
		if (consumed) *consumed = false;
		return false;
	}

	switch (event.id) {
		case AppButton::Up:
			MoveSelection(-1, 0);
			if (consumed) *consumed = true;
			return false;
		case AppButton::Down:
			MoveSelection(1, 0);
			if (consumed) *consumed = true;
			return false;
		case AppButton::Left:
			MoveSelection(0, -1);
			if (consumed) *consumed = true;
			return false;
		case AppButton::Right:
			MoveSelection(0, 1);
			if (consumed) *consumed = true;
			return false;
		case AppButton::A:
			ApplyPageChange(1);
			if (consumed) *consumed = true;
			return false;
		case AppButton::D: {
			output = SelectedOutput();
			if (!output.empty()) {
				if (on_input_) {
					on_input_(output);
				}
				if (consumed) *consumed = true;
				return true;
			}
			if (consumed) *consumed = false;
			return false;
		}
		case AppButton::B:
			if (on_delete_) {
				on_delete_();
				if (consumed) *consumed = true;
				return false;
			}
			if (consumed) *consumed = false;
			return false;
		default:
			if (consumed) *consumed = false;
			return false;
	}
}

bool KeyboardWidget::HandleSeqEvent(const seq_event_t& evt, bool* consumed) {
	if (!visible_) {
		if (consumed) *consumed = false;
		return false;
	}
	auto seq = eteacher::app_ui::nav::ToAppButtonSequence(&evt);
	if (seq.empty()) {
		if (consumed) *consumed = false;
		return false;
	}
	eteacher::app_ui::nav::ApplyGridMoveSequence(row_, col_, kRows, kCols, seq);
	if (consumed) *consumed = true;
	return true;
}

void KeyboardWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
	if (!visible_) return;
	if (!epd) return;
	if (bounds_.IsEmpty()) return;
	if (grid_.IsEmpty() || epd != last_epd_) {
		last_epd_ = epd;
		ComputeLayout();
	}

	gfx.fillRect(bounds_.x, bounds_.y, bounds_.w, bounds_.h, GxEPD_WHITE);

	const char* const* page = PageData(page_);
	const int16_t font_h = GetFontHeight(font_);
	const int16_t ascent = GetFontAscent(font_);

	for (int r = 0; r < kRows; ++r) {
		for (int c = 0; c < kCols; ++c) {
			auto rect = CellRect(r, c);
			if (rect.IsEmpty()) continue;
			gfx.drawRect(rect.x, rect.y, rect.w, rect.h, GxEPD_BLACK);

			const int idx = r * kCols + c;
			const char* label = page[idx];
			if (label && label[0] != '\0') {
				const std::string_view text(label);
				const int16_t text_w = epd->MeasureUtf8Width(text, font_);
				const int16_t text_x = static_cast<int16_t>(rect.x + (rect.w - text_w) / 2);
				const int16_t text_y = static_cast<int16_t>(rect.y + (rect.h - font_h) / 2 + ascent);
				epd->DrawUtf8(text_x, text_y, text, font_, GxEPD_BLACK);
			}
		}
	}

	auto sel = CellRect(row_, col_);
	if (!sel.IsEmpty()) {
		constexpr int16_t radius = 4;
		const int16_t inset = 1;
		gfx.drawRoundRect(static_cast<int16_t>(sel.x + inset),
						  static_cast<int16_t>(sel.y + inset),
						  static_cast<int16_t>(sel.w - inset * 2),
						  static_cast<int16_t>(sel.h - inset * 2),
						  radius,
						  GxEPD_BLACK);
	}
}

std::string KeyboardWidget::GetSelectedLabel() const {
	const char* const* page = PageData(page_);
	const int idx = row_ * kCols + col_;
	const char* label = page[idx];
	return label ? std::string(label) : std::string();
}

void KeyboardWidget::GetBounds(int16_t& x, int16_t& y, int16_t& w, int16_t& h) const {
	x = bounds_.x;
	y = bounds_.y;
	w = bounds_.w;
	h = bounds_.h;
}

void KeyboardWidget::ComputeLayout() {
	if (bounds_.IsEmpty()) {
		grid_ = {};
		cell_size_ = 0;
		return;
	}
	const int16_t cell_w = static_cast<int16_t>(bounds_.w / kCols);
	const int16_t cell_h = static_cast<int16_t>(bounds_.h / kRows);
	cell_size_ = std::max<int16_t>(1, std::min(cell_w, cell_h));

	const int16_t grid_w = static_cast<int16_t>(cell_size_ * kCols);
	const int16_t grid_h = static_cast<int16_t>(cell_size_ * kRows);
	const int16_t grid_x = static_cast<int16_t>(bounds_.x + (bounds_.w - grid_w) / 2);
	const int16_t grid_y = static_cast<int16_t>(bounds_.y + (bounds_.h - grid_h) / 2);
	grid_ = {grid_x, grid_y, grid_w, grid_h};
}

void KeyboardWidget::MoveSelection(int drow, int dcol) {
	row_ = (row_ + drow + kRows) % kRows;
	col_ = (col_ + dcol + kCols) % kCols;
}

void KeyboardWidget::ApplyPageChange(int delta) {
	page_ = (page_ + delta + kPages) % kPages;
}

std::string KeyboardWidget::SelectedOutput() const {
	const char* const* page = PageData(page_);
	const int idx = row_ * kCols + col_;
	const char* label = page[idx];
	if (!label || label[0] == '\0') {
		return {};
	}
	if (std::string_view(label) == std::string_view("空格")) {
		return " ";
	}
	return std::string(label);
}

eteacher::app_ui::layout::Rect KeyboardWidget::CellRect(int row, int col) const {
	if (grid_.IsEmpty() || cell_size_ <= 0) return {};
	if (row < 0 || row >= kRows || col < 0 || col >= kCols) return {};
	const int16_t x = static_cast<int16_t>(grid_.x + col * cell_size_);
	const int16_t y = static_cast<int16_t>(grid_.y + row * cell_size_);
	return {x, y, cell_size_, cell_size_};
}

} // namespace eteacher::app_ui
