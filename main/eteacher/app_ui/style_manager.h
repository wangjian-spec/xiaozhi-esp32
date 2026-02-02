#pragma once

namespace app_ui {

class StyleManager {
public:
    void MarkDirty(bool layout); // layout=true for layout dirty
    bool ConsumeLayoutDirty();
    bool ConsumeRenderDirty();

private:
    bool layout_dirty_ = false;
    bool render_dirty_ = false;
};

} // namespace app_ui
