#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "input.h"
#include "renderer.h"

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

class Adafruit_GFX;
class CustomEpdDisplay;
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
    // Keep JSON/Widget creation in Scene/SceneRuntime/WidgetBuilder and
    // drawing logic inside Painter/Widget implementations.
    void OnInput(const InputEvent& e);
    void RequestLayout();
    void RequestRender();

    void Tick(uint32_t delta_ms);

    void SetEpd(::CustomEpdDisplay* epd);
    void SetRoot(std::unique_ptr<Widget> root);
    void Reset();

    void SetPainter(Painter* painter);
    void SetViewport(const Rect& rect);

    InputQueue& Input();

private:
    void MarkLayoutDirty();
    void MarkRenderDirty();
    void ScheduleIfNeeded();
    static void RenderCallback(::Adafruit_GFX& gfx, void* ctx);
    void RenderInternal(::Adafruit_GFX& gfx);

    UIPhase phase_ = UIPhase::Idle;
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
    std::unique_ptr<Widget> active_root_{};
    std::unique_ptr<Widget> pending_root_{};
    Painter* painter_ = nullptr;
    Rect viewport_{0, 0, 0, 0};
    bool need_layout_ = true;
    bool need_render_ = true;
    std::atomic_bool scheduled_{false};
    mutable std::recursive_mutex mutex_{};
    ::CustomEpdDisplay* epd_ = nullptr;
};

} // namespace app_ui
