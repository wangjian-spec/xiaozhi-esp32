#pragma once

#include <cstdint>

#include "input.h"
#include "renderer.h"
#include "scene.h"

namespace app_ui {

class StyleManager {
public:
    void MarkDirty(bool layout) { // layout=true for layout dirty
        if (layout) {
            layout_dirty_ = true;
        }
        render_dirty_ = true;
    }
    bool ConsumeLayoutDirty() {
        const bool dirty = layout_dirty_;
        layout_dirty_ = false;
        return dirty;
    }
    bool ConsumeRenderDirty() {
        const bool dirty = render_dirty_;
        render_dirty_ = false;
        return dirty;
    }

private:
    bool layout_dirty_ = false;
    bool render_dirty_ = false;
};

class AnimationEngine {
public:
    bool Tick(uint32_t) {
        if (dirty_) {
            dirty_ = false;
            return true;
        }
        return false;
    }
    void StopAll() { dirty_ = false; }

private:
    bool dirty_ = false;
};

class Painter;
class Widget;

enum class UIPhase {
    Idle,
    Layout,
    Render
};

class UIEngine {
public:
    // Design note:
    // UIEngine is an orchestrator only. It MUST NOT:
    // - create widgets
    // - own or persist widget pointers outside of the active root
    // - parse JSON or build widget trees
    // - implement concrete rendering logic
    // Keep JSON/Widget creation in Scene/SceneManager/WidgetBuilder and
    // drawing logic inside Painter/Widget implementations.
    void OnInput(const InputEvent& e);
    void RequestLayout();
    void RequestRender();

    void Tick(uint32_t delta_ms);

    void SetPainter(Painter* painter);
    void SetViewport(const Rect& rect);

    SceneManager& Scenes();
    InputQueue& Input();

private:
    Widget* ResolveRoot();

    UIPhase phase_ = UIPhase::Idle;
    SceneManager scenes_;
    LayoutEngine layout_;
    RenderList render_list_;
    DirtyTracker dirty_;
    Renderer renderer_;
    StyleManager style_;
    AnimationEngine animation_;
    InputQueue input_queue_;
    InputDispatcher dispatcher_;
    FocusManager focus_;

    Widget* cached_root_ = nullptr;
    Painter* painter_ = nullptr;
    Rect viewport_{0, 0, 0, 0};
    bool need_layout_ = true;
    bool need_render_ = true;
};

} // namespace app_ui
