#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_ui/layout_engine.h"

class Adafruit_GFX;
class CustomEpdDisplay;

#ifdef __cplusplus
extern "C" {
#endif
#include "eteacher/app_ui/seq_button.h"
#ifdef __cplusplus
}
#endif

namespace eteacher::app_ui {

// Soft keyboard widget (4x9, 3 pages). Uses row/col selection with wrap.
class KeyboardWidget {
public:
	using InputCallback = std::function<void(std::string_view)>;
	using DeleteCallback = std::function<void()>;

	KeyboardWidget() = default;

	void ShowKeyboard(CustomEpdDisplay* epd, int16_t x, int16_t y, int16_t w, int16_t h);
	void ShowKeyboard(CustomEpdDisplay* epd);
	void CloseKeyboard();

	bool IsVisible() const { return visible_; }

	void SetOnInput(InputCallback cb) { on_input_ = std::move(cb); }
	void SetOnDelete(DeleteCallback cb) { on_delete_ = std::move(cb); }

	// Returns true if a character is confirmed (D key). Output is filled when true.
	bool HandleButton(const ButtonEvent& event, std::string& output, bool* consumed = nullptr);

	// Handle sequence event from seq_button. Moves selection once per sequence.
	bool HandleSeqEvent(const seq_event_t& evt, bool* consumed = nullptr);

	// Draw keyboard into given display.
	void Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd);

	// Query helpers
	std::string GetSelectedLabel() const;
	void GetBounds(int16_t& x, int16_t& y, int16_t& w, int16_t& h) const;

private:
	void ComputeLayout();
	void MoveSelection(int drow, int dcol);
	void ApplyPageChange(int delta);
	std::string SelectedOutput() const;
	eteacher::app_ui::layout::Rect CellRect(int row, int col) const;

	bool visible_ = false;
	CustomEpdDisplay* last_epd_ = nullptr;

	eteacher::app_ui::layout::Rect bounds_{};
	eteacher::app_ui::layout::Rect grid_{};
	int16_t cell_size_ = 0;

	int page_ = 0;
	int row_ = 0;
	int col_ = 0;

	std::string_view font_ = "wenquanyi_9pt";
	InputCallback on_input_{};
	DeleteCallback on_delete_{};
};

} // namespace eteacher::app_ui
