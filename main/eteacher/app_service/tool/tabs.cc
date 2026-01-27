#include "tabs.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"

#include <Adafruit_GFX.h>
#include <GxEPD2_BW.h>

#include <algorithm>
#include <cstdio>

namespace eteacher {

TabView::TabView(const TabConfig& cfg) : cfg_(cfg) {}

void TabView::SetConfig(const TabConfig& cfg) {
    cfg_ = cfg;
}

void TabView::SetItems(const std::vector<TabItem>& items) {
    items_ = items;
    if (!items_.empty()) selected_index_ = std::min(selected_index_, (int)items_.size() - 1);
}

static int clamp_index(int idx, int n) {
    if (n <= 0) return 0;
    if (idx < 0) return 0;
    if (idx >= n) return n - 1;
    return idx;
}

void TabView::MoveUp() {
    if (showing_properties_) return; // navigation disabled when showing properties
    if (items_.empty()) return;
    int cols = std::max(1, cfg_.cols);
    int rows = std::max(1, cfg_.rows);
    int r = selected_index_ / cols;
    int c = selected_index_ % cols;
    r = (r - 1 + rows) % rows; // wrap around vertically
    int new_idx = r * cols + c;
    if (new_idx >= (int)items_.size()) {
        // if outside range, clamp to last valid in that column
        while (new_idx >= (int)items_.size() && r >= 0) {
            r = (r - 1 + rows) % rows; // step back
            new_idx = r * cols + c;
            if (r == (rows - 1)) break; // prevent infinite loop
        }
        new_idx = clamp_index(new_idx, items_.size());
    }
    selected_index_ = new_idx;
}

void TabView::MoveDown() {
    if (showing_properties_) return;
    if (items_.empty()) return;
    int cols = std::max(1, cfg_.cols);
    int rows = std::max(1, cfg_.rows);
    int r = selected_index_ / cols;
    int c = selected_index_ % cols;
    r = (r + 1) % rows; // wrap around
    int new_idx = r * cols + c;
    if (new_idx >= (int)items_.size()) {
        // clamp back
        new_idx = clamp_index(new_idx, items_.size());
    }
    selected_index_ = new_idx;
}

void TabView::MoveLeft() {
    if (showing_properties_) return;
    if (items_.empty()) return;
    int cols = std::max(1, cfg_.cols);
    int r = selected_index_ / cols;
    int c = selected_index_ % cols;
    c = (c - 1 + cols) % cols; // wrap
    int new_idx = r * cols + c;
    if (new_idx >= (int)items_.size()) {
        new_idx = clamp_index(new_idx, items_.size());
    }
    selected_index_ = new_idx;
}

void TabView::MoveRight() {
    if (showing_properties_) return;
    if (items_.empty()) return;
    int cols = std::max(1, cfg_.cols);
    int r = selected_index_ / cols;
    int c = selected_index_ % cols;
    c = (c + 1) % cols; // wrap
    int new_idx = r * cols + c;
    if (new_idx >= (int)items_.size()) {
        new_idx = clamp_index(new_idx, items_.size());
    }
    selected_index_ = new_idx;
}

void TabView::Confirm() {
    if (items_.empty()) return;
    // Enter property view for the selected item
    showing_properties_ = true;
    // If there is a render callback, call it now (placeholder)
    if (items_[selected_index_].render_callback) {
        items_[selected_index_].render_callback();
    }
}

void TabView::Back() {
    // Exit property view
    showing_properties_ = false;
}

void TabView::RenderOptions() {
    // no-op here; drawing happens in Draw()
}

void TabView::RenderProperties() {
    // no-op here; drawing happens in Draw()
}

void TabView::Show(CustomEpdDisplay* epd, int x, int y, int w, int h) {
    if (!epd) return;
    epd_ = epd;
    x_ = x; y_ = y; w_ = w; h_ = h;
}

void TabView::Close() {
    if (!epd_) return;
    // force clear area
    auto cb = [](Adafruit_GFX& /*gfx*/, void* /*ctx*/) {};
    EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial, cb, nullptr, nullptr,
                                       EpdManager::Rect(x_, y_, w_, h_));
    epd_ = nullptr;
}

bool TabView::HandleButton(const ButtonEvent& event, bool* consumed) {
    if (consumed) *consumed = false;
    if (!epd_) return false;
    if (event.action != ButtonAction::Click) return false;

    if (showing_properties_) {
        if (event.id == AppButton::B) {
            Back();
            if (consumed) *consumed = true;
            return true;
        }
        return false;
    }

    switch (event.id) {
        case AppButton::Up:
            MoveUp();
            if (consumed) *consumed = true;
            return true;
        case AppButton::Down:
            MoveDown();
            if (consumed) *consumed = true;
            return true;
        case AppButton::Left:
            MoveLeft();
            if (consumed) *consumed = true;
            return true;
        case AppButton::Right:
            MoveRight();
            if (consumed) *consumed = true;
            return true;
        case AppButton::C:
            Confirm();
            if (consumed) *consumed = true;
            return true;
        default:
            return false;
    }
}

// Note: scheduling of EPD updates was removed. Callers should
// schedule EpdManager updates and call `Draw(Adafruit_GFX&)` when
// a redraw is required.

void TabView::Draw(Adafruit_GFX& gfx) {
    if (!epd_ || w_ <= 0 || h_ <= 0) return;

    // background
    gfx.fillRect(x_, y_, w_, h_, GxEPD_WHITE);

    // Option area top
    int opt_h = cfg_.option_area_height > 0 ? cfg_.option_area_height : (h_ / 4);
    int prop_h = h_ - opt_h;

    int cols = std::max(1, cfg_.cols);
    int rows = std::max(1, cfg_.rows);
    int cell_w = w_ / cols;
    int cell_h = opt_h / rows;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            int tx = x_ + c * cell_w;
            int ty = y_ + r * cell_h;
            int tw = (c == cols - 1) ? (x_ + w_ - tx) : cell_w;
            int th = (r == rows - 1) ? (y_ + opt_h - ty) : cell_h;
            if (idx >= (int)items_.size()) {
                // draw empty
                gfx.drawRect(tx, ty, tw, th, GxEPD_BLACK);
                continue;
            }
            // Selected item: black background, white text; else white background, black text
            if (idx == selected_index_) {
                gfx.fillRect(tx, ty, tw, th, GxEPD_BLACK);
                int16_t text_w = epd_->MeasureUtf8Width(items_[idx].title.c_str(), "wenquanyi_11pt");
                int16_t txpos = tx + (tw - text_w) / 2;
                int16_t typos = ty + (th / 2) + 5;
                epd_->DrawUtf8(txpos, typos, items_[idx].title.c_str(), "wenquanyi_11pt", GxEPD_WHITE);
            } else {
                gfx.drawRect(tx, ty, tw, th, GxEPD_BLACK);
                int16_t text_w = epd_->MeasureUtf8Width(items_[idx].title.c_str(), "wenquanyi_11pt");
                int16_t txpos = tx + (tw - text_w) / 2;
                int16_t typos = ty + (th / 2) + 5;
                epd_->DrawUtf8(txpos, typos, items_[idx].title.c_str(), "wenquanyi_11pt", GxEPD_BLACK);
            }
        }
    }

    // property area
    int prop_x = x_;
    int prop_y = y_ + opt_h;
    gfx.drawRect(prop_x, prop_y, w_, prop_h, GxEPD_BLACK);
    if (showing_properties_ && selected_index_ < (int)items_.size()) {
        const std::string& t = items_[selected_index_].title;
        int16_t txpos = prop_x + 4;
        int16_t typos = prop_y + 20;
        epd_->DrawUtf8(txpos, typos, t.c_str(), "wenquanyi_11pt", GxEPD_BLACK);
        // call render callback to draw property details if present
        if (items_[selected_index_].render_callback) {
            items_[selected_index_].render_callback();
        }
    }
}

} // namespace eteacher
