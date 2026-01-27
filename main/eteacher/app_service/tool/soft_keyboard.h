#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_service/tool/ui_widget.h"

class CustomEpdDisplay;
class Adafruit_GFX;

// KeyboardWidget: 软键盘控件（4x9 网格，三页模式）。
// 适用场景：输入法/文字输入。
// 焦点行为：方向键移动，Confirm 输入字符，Delete 删除字符。
class KeyboardWidget : public eteacher::app_service::tool::WidgetBase {
public:
	static constexpr int kRows = 4;
	static constexpr int kCols = 9;

	using Grid = std::array<std::array<const char*, kCols>, kRows>;
	struct KeyboardLayout {
		Grid lower;
		Grid upper;
		Grid symbols;
	};

	enum class Mode {
		Lowercase = 0,
		Uppercase = 1,
		OtherSymbols = 2,
	};

	using InputCallback = std::function<void(std::string_view)>;
	using DeleteCallback = std::function<void()>;

	KeyboardWidget();
	void SetLayout(const KeyboardLayout& layout);
	void SetMode(Mode mode);
	Mode mode() const { return mode_; }

	void NextMode();

	void SetSelection(int row, int col);
	int row() const { return row_; }
	int col() const { return col_; }

	void SetOnInput(InputCallback cb) { on_input_ = std::move(cb); }
	void SetOnDelete(DeleteCallback cb) { on_delete_ = std::move(cb); }

	bool OnAction(eteacher::app_service::tool::Action action) override;
	void OnFocus(bool focused) override;
	void Draw(Adafruit_GFX& gfx, CustomEpdDisplay* epd) override;

	// 兼容旧接口（非布局引擎模式）。
	void ShowKeyboard(CustomEpdDisplay* epd, int16_t x, int16_t y, int16_t w, int16_t h);
	void ShowKeyboard(CustomEpdDisplay* epd);
	void CloseKeyboard();
	bool IsVisible() const { return visible_; }

	bool HasStateChanged() const { return state_changed_; }
	bool ConsumeStateChanged();

	void GetBounds(int16_t* x, int16_t* y, int16_t* w, int16_t* h) const;
	const char* GetSelectedLabel() const;
	bool HandleButton(const ButtonEvent& event, std::string& output, bool* consumed = nullptr);
	void Draw(Adafruit_GFX& gfx);

private:
	void MarkStateChanged();
	const char* LabelAt(int row, int col) const;
	void EnsureSelectionValid();
	bool FindNearestValidSelection(int start_row, int start_col, int* out_row, int* out_col) const;
	void UpdateLayoutFromRect();
	eteacher::layout::Rect CellRect(int row, int col) const;

	int16_t x_ = 0;
	int16_t y_ = 0;
	int16_t w_ = 0;
	int16_t h_ = 0;
	int16_t cell_ = 0;
	int row_ = 0;
	int col_ = 0;
	Mode mode_ = Mode::Lowercase;
	bool layout_valid_ = false;
	const KeyboardLayout* layout_ = nullptr;
	InputCallback on_input_{};
	DeleteCallback on_delete_{};

	// Compatibility state
	CustomEpdDisplay* epd_ = nullptr;
	bool visible_ = false;
	bool state_changed_ = false;
	eteacher::layout::Region compat_region_{};
};

// Backward compatibility alias.
using SoftKeyboard = KeyboardWidget;
