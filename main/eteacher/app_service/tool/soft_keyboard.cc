#include "eteacher/app_service/tool/soft_keyboard.h"

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/app_manager/menu.h"

namespace {
constexpr const char* kFont = "wenquanyi_11pt";
constexpr int kTextBaselineOffset = 5;

const KeyboardWidget::KeyboardLayout kDefaultLayout = {
	{{
		{{"a", "b", "c", "d", "e", "f", "g", "h", "i"}},
		{{"j", "k", "l", "m", "n", "o", "p", "q", "r"}},
		{{"s", "t", "u", "v", "w", "x", "y", "z", "0"}},
		{{"1", "2", "3", "4", "5", "6", "7", "8", "9"}},
	}},
	{{
		{{"A", "B", "C", "D", "E", "F", "G", "H", "I"}},
		{{"J", "K", "L", "M", "N", "O", "P", "Q", "R"}},
		{{"S", "T", "U", "V", "W", "X", "Y", "Z", "!"}},
		{{"\"", "#", "$", "%", "&", "'", "(", ")", "*"}},
	}},
	{{
		{{"+", ",", "-", ".", "/", ":", ";", "<", "="}},
		{{">", "?", "@", "[", "\\", "]", "^", "_", "`"}},
		{{"{", "|", "}", "~", "空格", "", "", "", ""}},
		{{"", "", "", "", "", "", "", "", ""}},
	}},
};

void DrawSelectionRect(Adafruit_GFX& gfx, int16_t x, int16_t y, int16_t w, int16_t h, int16_t border) {
	if (w <= 0 || h <= 0 || border <= 0) {
		return;
	}
	int16_t radius = static_cast<int16_t>(std::min<int16_t>(8, std::min<int16_t>(w, h) / 4));
	for (int i = 0; i < border; ++i) {
		gfx.drawRoundRect(x - i, y - i, w + i * 2, h + i * 2, radius + i, GxEPD_BLACK);
	}
}

eteacher::layout::Rect UnionRect(const eteacher::layout::Rect& a, const eteacher::layout::Rect& b) {
	if (a.IsEmpty()) return b;
	if (b.IsEmpty()) return a;
	const int16_t x1 = std::min<int16_t>(a.x, b.x);
	const int16_t y1 = std::min<int16_t>(a.y, b.y);
	const int16_t x2 = std::max<int16_t>(a.right(), b.right());
	const int16_t y2 = std::max<int16_t>(a.bottom(), b.bottom());
	return {x1, y1, static_cast<int16_t>(x2 - x1), static_cast<int16_t>(y2 - y1)};
}
} // namespace

KeyboardWidget::KeyboardWidget() {
	layout_ = &kDefaultLayout;
}

void KeyboardWidget::SetLayout(const KeyboardLayout& layout) {
	layout_ = &layout;
	EnsureSelectionValid();
	MarkDirty(rect());
	MarkStateChanged();
}

void KeyboardWidget::SetMode(Mode mode) {
	if (mode_ == mode) {
		return;
	}
	mode_ = mode;
	EnsureSelectionValid();
	MarkDirty(rect());
	MarkStateChanged();
}

void KeyboardWidget::NextMode() {
	Mode next = Mode::Lowercase;
	if (mode_ == Mode::Lowercase) {
		next = Mode::Uppercase;
	} else if (mode_ == Mode::Uppercase) {
		next = Mode::OtherSymbols;
	} else {
		next = Mode::Lowercase;
	}
	SetMode(next);
}

void KeyboardWidget::SetSelection(int row, int col) {
	row_ = (row % kRows + kRows) % kRows;
	col_ = (col % kCols + kCols) % kCols;
	EnsureSelectionValid();
	MarkDirty(rect());
	MarkStateChanged();
}

bool KeyboardWidget::OnAction(eteacher::app_service::tool::Action action) {
	UpdateLayoutFromRect();
	const int prev_row = row_;
	const int prev_col = col_;

	if (action == eteacher::app_service::tool::Action::Up) {
		row_ = (row_ - 1 + kRows) % kRows;
	} else if (action == eteacher::app_service::tool::Action::Down) {
		row_ = (row_ + 1) % kRows;
	} else if (action == eteacher::app_service::tool::Action::Left) {
		col_ = (col_ - 1 + kCols) % kCols;
	} else if (action == eteacher::app_service::tool::Action::Right) {
		col_ = (col_ + 1) % kCols;
	} else if (action == eteacher::app_service::tool::Action::Confirm) {
		const char* label = LabelAt(row_, col_);
		if (label && label[0] != '\0' && on_input_) {
			if (std::string(label) == "空格") {
				on_input_(" ");
			} else {
				on_input_(label);
			}
		}
		MarkStateChanged();
		return true;
	} else if (action == eteacher::app_service::tool::Action::Delete) {
		if (on_delete_) {
			on_delete_();
		}
		MarkStateChanged();
		return true;
	} else if (action == eteacher::app_service::tool::Action::Next) {
		NextMode();
		return true;
	} else {
		return false;
	}

	EnsureSelectionValid();
	const auto a = CellRect(prev_row, prev_col);
	const auto b = CellRect(row_, col_);
	MarkDirty(UnionRect(a, b));
	MarkStateChanged();
	return true;
}

void KeyboardWidget::OnFocus(bool focused) {
	WidgetBase::OnFocus(focused);
	MarkDirty(rect());
	MarkStateChanged();
}

const char* KeyboardWidget::LabelAt(int row, int col) const {
	if (row < 0 || row >= kRows || col < 0 || col >= kCols) {
		return "";
	}
	const KeyboardLayout* layout = layout_ ? layout_ : &kDefaultLayout;
	if (mode_ == Mode::Lowercase) {
		return layout->lower[row][col];
	}
	if (mode_ == Mode::Uppercase) {
		return layout->upper[row][col];
	}
	return layout->symbols[row][col];
}

void KeyboardWidget::EnsureSelectionValid() {
	row_ = (row_ % kRows + kRows) % kRows;
	col_ = (col_ % kCols + kCols) % kCols;
	if (LabelAt(row_, col_)[0] != '\0') {
		return;
	}
	int nr = 0;
	int nc = 0;
	if (FindNearestValidSelection(row_, col_, &nr, &nc)) {
		row_ = nr;
		col_ = nc;
		return;
	}
	row_ = 0;
	col_ = 0;
}

bool KeyboardWidget::FindNearestValidSelection(int start_row, int start_col, int* out_row, int* out_col) const {
	if (!out_row || !out_col) {
		return false;
	}
	int best_r = -1;
	int best_c = -1;
	int best_dist = std::numeric_limits<int>::max();
	for (int r = 0; r < kRows; ++r) {
		for (int c = 0; c < kCols; ++c) {
			if (LabelAt(r, c)[0] == '\0') {
				continue;
			}
			int dist = std::abs(r - start_row) + std::abs(c - start_col);
			if (dist < best_dist) {
				best_dist = dist;
				best_r = r;
				best_c = c;
			}
		}
	}
	if (best_r >= 0 && best_c >= 0) {
		*out_row = best_r;
		*out_col = best_c;
		return true;
	}
	return false;
}

void KeyboardWidget::UpdateLayoutFromRect() {
	const auto r = rect();
	if (r.IsEmpty()) {
		layout_valid_ = false;
		return;
	}
	const int16_t cell = static_cast<int16_t>(std::max<int16_t>(1, std::min<int16_t>(r.w / kCols, r.h / kRows)));
	cell_ = cell;
	const int16_t actual_w = static_cast<int16_t>(cell_ * kCols);
	const int16_t actual_h = static_cast<int16_t>(cell_ * kRows);
	int16_t nx = static_cast<int16_t>(r.x + (r.w - actual_w) / 2);
	int16_t ny = static_cast<int16_t>(r.y + (r.h - actual_h) / 2);
	if (nx < r.x) nx = r.x;
	if (ny < r.y) ny = r.y;
	x_ = nx;
	y_ = ny;
	w_ = actual_w;
	h_ = actual_h;
	layout_valid_ = true;
}

eteacher::layout::Rect KeyboardWidget::CellRect(int row, int col) const {
	if (!layout_valid_ || cell_ <= 0) return {};
	const int16_t cell_w = static_cast<int16_t>(w_ / kCols);
	const int16_t cell_h = static_cast<int16_t>(h_ / kRows);
	const int16_t x = static_cast<int16_t>(x_ + col * cell_w);
	const int16_t y = static_cast<int16_t>(y_ + row * cell_h);
	const int16_t w = (col == kCols - 1) ? static_cast<int16_t>(x_ + w_ - x) : cell_w;
	const int16_t h = (row == kRows - 1) ? static_cast<int16_t>(y_ + h_ - y) : cell_h;
	return {x, y, w, h};
}

void KeyboardWidget::Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) {
	const auto r = rect();
	if (r.IsEmpty() || !epd) {
		return;
	}

	UpdateLayoutFromRect();
	if (!layout_valid_ || w_ <= 0 || h_ <= 0) {
		return;
	}

	gfx.fillRect(r.x, r.y, r.w, r.h, GxEPD_WHITE);

	const int16_t cell_w = static_cast<int16_t>(w_ / kCols);
	const int16_t cell_h = static_cast<int16_t>(h_ / kRows);

	for (int rr = 0; rr < kRows; ++rr) {
		for (int cc = 0; cc < kCols; ++cc) {
			const int16_t x = static_cast<int16_t>(x_ + cc * cell_w);
			const int16_t y = static_cast<int16_t>(y_ + rr * cell_h);
			const int16_t w = (cc == kCols - 1) ? static_cast<int16_t>(x_ + w_ - x) : cell_w;
			const int16_t h = (rr == kRows - 1) ? static_cast<int16_t>(y_ + h_ - y) : cell_h;

			gfx.drawRect(x, y, w, h, GxEPD_BLACK);

			const char* label = LabelAt(rr, cc);
			if (label && label[0] != '\0') {
				int16_t text_w = epd->MeasureUtf8Width(label, kFont);
				int16_t tx = static_cast<int16_t>(x + (w - text_w) / 2);
				if (tx < x + 2) {
					tx = x + 2;
				}
				int16_t ty = static_cast<int16_t>(y + (h / 2) + kTextBaselineOffset);
				epd->DrawUtf8(tx, ty, label, kFont, GxEPD_BLACK);
			}

			if (rr == row_ && cc == col_) {
				DrawSelectionRect(gfx, x + 2, y + 2, w - 4, h - 4, 2);
			}
		}
	}

	ClearDirty();
}

void KeyboardWidget::ShowKeyboard(CustomEpdDisplay* epd, int16_t x, int16_t y, int16_t w, int16_t h) {
	if (!epd) {
		return;
	}
	epd_ = epd;
	visible_ = true;
	compat_region_.rect = {x, y, w, h};
	AttachRegion(&compat_region_);
	UpdateLayoutFromRect();
	MarkStateChanged();
}

void KeyboardWidget::ShowKeyboard(CustomEpdDisplay* epd) {
	if (!epd) {
		return;
	}
	const int16_t screen_w = static_cast<int16_t>(epd->width());
	const int16_t screen_h = static_cast<int16_t>(epd->height());
	const auto style = eteacher::app_menu::MenuStyle{};
	const int16_t content_h = static_cast<int16_t>(screen_h - style.top_height - style.bottom_height);
	int16_t desired_h = static_cast<int16_t>(content_h / 3);
	if (desired_h <= 0) {
		desired_h = static_cast<int16_t>(screen_h / 4);
	}
	int16_t cell = static_cast<int16_t>(desired_h / kRows);
	if (cell <= 0) {
		cell = 1;
	}
	int16_t desired_w = static_cast<int16_t>(cell * kCols);
	if (desired_w > screen_w) {
		cell = static_cast<int16_t>(std::max<int16_t>(1, screen_w / kCols));
		desired_w = static_cast<int16_t>(cell * kCols);
		desired_h = static_cast<int16_t>(cell * kRows);
	}
	const int16_t x = static_cast<int16_t>((screen_w - desired_w) / 2);
	const int16_t y = static_cast<int16_t>(screen_h - style.bottom_height - desired_h);
	ShowKeyboard(epd, x, y, desired_w, desired_h);
}

void KeyboardWidget::CloseKeyboard() {
	if (!visible_) {
		return;
	}
	visible_ = false;
	epd_ = nullptr;
	MarkStateChanged();
}

bool KeyboardWidget::ConsumeStateChanged() {
	const bool changed = state_changed_;
	state_changed_ = false;
	return changed;
}

void KeyboardWidget::GetBounds(int16_t* x, int16_t* y, int16_t* w, int16_t* h) const {
	const auto r = rect();
	if (x) *x = r.x;
	if (y) *y = r.y;
	if (w) *w = r.w;
	if (h) *h = r.h;
}

const char* KeyboardWidget::GetSelectedLabel() const {
	return LabelAt(row_, col_);
}

bool KeyboardWidget::HandleButton(const ButtonEvent& event, std::string& output, bool* consumed) {
	if (consumed) {
		*consumed = false;
	}
	if (!visible_) {
		return false;
	}
	if (event.action != ButtonAction::Click) {
		return false;
	}

	bool moved = false;
	if (event.id == AppButton::Up) {
		OnAction(eteacher::app_service::tool::Action::Up);
		moved = true;
	} else if (event.id == AppButton::Down) {
		OnAction(eteacher::app_service::tool::Action::Down);
		moved = true;
	} else if (event.id == AppButton::Left) {
		OnAction(eteacher::app_service::tool::Action::Left);
		moved = true;
	} else if (event.id == AppButton::Right) {
		OnAction(eteacher::app_service::tool::Action::Right);
		moved = true;
	}

	if (moved) {
		if (consumed) *consumed = true;
		return false;
	}

	if (event.id == AppButton::A) {
		NextMode();
		if (consumed) *consumed = true;
		return false;
	}

	if (event.id == AppButton::D) {
		const char* label = GetSelectedLabel();
		if (!label || label[0] == '\0') {
			if (consumed) *consumed = true;
			return false;
		}
		if (std::string(label) == "空格") {
			output = " ";
		} else {
			output.assign(label);
		}
		if (consumed) *consumed = true;
		MarkStateChanged();
		return true;
	}

	return false;
}

void KeyboardWidget::Draw(Adafruit_GFX& gfx) {
	if (!visible_ || !epd_) {
		return;
	}
	Draw(gfx, epd_);
}

void KeyboardWidget::MarkStateChanged() {
	state_changed_ = true;
}
