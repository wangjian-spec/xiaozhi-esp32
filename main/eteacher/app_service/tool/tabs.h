#pragma once

#include <vector>
#include <string>
#include <functional>

#include "eteacher/app_manager/app_base.h"

class CustomEpdDisplay;
class Adafruit_GFX;

namespace eteacher {

struct TabItem {
    std::string title;
    // A simple callback to render the property's content into the provided buffer/context.
    // The rendering implementation can be adapted to the project's EPD manager.
    std::function<void()> render_callback;
};

struct TabConfig {
    int x = 0; // top-left x
    int y = 0; // top-left y
    int width = 800; // total width
    int height = 600; // total height
    int rows = 1; // option grid rows
    int cols = 4; // option grid columns
    int option_area_height = 40; // height reserved for option area
    int property_area_height = 160; // height reserved for property area
};

class TabView {
public:
    TabView() = default;
    explicit TabView(const TabConfig& cfg);

    void SetConfig(const TabConfig& cfg);
    void SetItems(const std::vector<TabItem>& items);

    // Navigation: called when up/down/left/right pressed
    void MoveUp();
    void MoveDown();
    void MoveLeft();
    void MoveRight();

    // Confirm / Cancel (C / B keys behavior)
    void Confirm();
    void Back();

    // Draw into an existing graphics context (no scheduling)
    // Note: this class no longer schedules EPD updates itself; the
    // caller is responsible for scheduling or calling Draw as needed.
    void Draw(Adafruit_GFX& gfx);

    // Show/close on a specific EPD surface. The owning app will call
    // `HandleButton` to feed input events into this view.
    void Show(CustomEpdDisplay* epd, int x, int y, int w, int h);
    void Close();

    // Handle button events forwarded by the embedding app. Returns true if
    // the event is consumed. `consumed` may be nullptr.
    bool HandleButton(const ButtonEvent& event, bool* consumed = nullptr);

    // Query state
    bool IsShowingProperties() const { return showing_properties_; }
    int SelectedIndex() const { return selected_index_; }

private:
    TabConfig cfg_;
    std::vector<TabItem> items_;
    int selected_index_ = 0;
    bool showing_properties_ = false;
    CustomEpdDisplay* epd_ = nullptr;
    int x_ = 0;
    int y_ = 0;
    int w_ = 0;
    int h_ = 0;

    void RenderOptions();
    void RenderProperties();
    // Draw is public for app-layer composition
};

} // namespace eteacher
