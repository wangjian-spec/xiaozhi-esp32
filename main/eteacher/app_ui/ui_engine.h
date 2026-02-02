#pragma once

#include <cstdint>
#include "animation_engine.h"
#include "dirty_tracker.h"
#include "focus_manager.h"
#include "input.h"
#include "layout_engine.h"
#include "renderer.h"
#include "scene.h"
#include "style_manager.h"

namespace app_ui {

class Painter;
class Widget;

enum class UIPhase {
    Idle,
    Layout,
    Render
};

class UIEngine {
public:
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
