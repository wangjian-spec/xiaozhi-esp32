#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../widget.h"

namespace app_ui::widget_internal {

struct GridLayoutMetrics {
    int rows = 1;
    int cols = 1;
    int cell_w = 1;
    int cell_h = 1;
};

int ResolveKeyboardRows(const SoftKeyboardProfile& profile);
int ResolveKeyboardCols(const SoftKeyboardProfile& profile);
int ResolveKeyboardPages(const SoftKeyboardProfile& profile);
int ResolveKeyboardPageSize(const SoftKeyboardProfile& profile);
bool HasCustomKeyboardLayout(const SoftKeyboardProfile& profile);
const char* DefaultKeyboardLabel(int page, int index);

bool IsKeyDownOrRepeat(const InputEvent& e);
bool IsActivationKey(KeyCode key);
int ClampIndexByCount(int index, int count);

void DrawCenteredTextInCell(Painter& p, int x, int y, int w, int h, const char* label);
int ResolveGridCols(int item_count, int rows_hint, int cols_hint);
GridLayoutMetrics MakeGridLayoutMetrics(const Rect& rect, int rows_hint, int cols_hint);
void DrawGridOutline(Painter& p, const Rect& rect, const GridLayoutMetrics& metrics);
void DrawGenericBar(Painter& p, const Rect& rect, const std::string& text, const BarProfile& profile);

uint8_t ClampProgressValue(uint8_t value, uint8_t max_value);
std::vector<std::string> ParseItemsFromText(const std::string& text);
std::vector<std::string> WrapTextByWidth(Painter& p, const std::string& text, int max_width, int max_lines);

std::unique_ptr<ItemModel> MakeStaticVectorModel(std::vector<std::string> items);
std::unique_ptr<ItemModel> MakeTextParsedModel(const std::string& text);

ListViewBehavior& DefaultListViewBehaviorInstance();
DialogBehavior& DefaultDialogBehaviorInstance();

} // namespace app_ui::widget_internal
