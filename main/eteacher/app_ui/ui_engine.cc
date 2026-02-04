#include "ui_engine.h"

#include "renderer.h"
#include "scene.h"
#include "widget.h"

namespace app_ui {

void UIEngine::OnInput(const InputEvent& e) {
    input_queue_.Push(e);
}

void UIEngine::RequestLayout() {
    need_layout_ = true;
    need_render_ = true;
}

void UIEngine::RequestRender() {
    need_render_ = true;
}

void UIEngine::SetPainter(Painter* painter) {
    painter_ = painter;
}

void UIEngine::SetViewport(const Rect& rect) {
    viewport_ = rect;
    RequestLayout();
}

SceneManager& UIEngine::Scenes() {
    return scenes_;
}

InputQueue& UIEngine::Input() {
    return input_queue_;
}

Widget* UIEngine::ResolveRoot() {
    Scene* scene = scenes_.Current();
    if (!scene) {
        return nullptr;
    }

    if (!scene->Root()) {
        cached_root_ = scene->BuildUI();
        if (!scene->Root()) {
            return cached_root_;
        }
    }

    cached_root_ = scene->Root();
    return cached_root_;
}

void UIEngine::Tick(uint32_t delta_ms) {
    Widget* root = ResolveRoot();
    if (!root) {
        return;
    }

    if (cached_root_ != root) {
        cached_root_ = root;
        focus_.Clear();
        dirty_.Clear();
        animation_.StopAll();
        style_.MarkDirty(true);
        input_queue_.Clear();
        focus_.Build(root);
        RequestLayout();
    }

    InputEvent event;
    while (input_queue_.TryPop(event)) {
        dispatcher_.Dispatch(event, root, focus_);
    }

    if (style_.ConsumeLayoutDirty()) {
        RequestLayout();
    } else if (style_.ConsumeRenderDirty()) {
        RequestRender();
    }

    if (animation_.Tick(delta_ms)) {
        RequestRender();
    }

    if (need_layout_) {
        phase_ = UIPhase::Layout;
        layout_.LayoutTree(root, viewport_);
        render_list_.Build(root);
        dirty_.Add(viewport_, DirtyReason::Full);
        need_layout_ = false;
        need_render_ = true;
    }

    if (need_render_ && painter_) {
        phase_ = UIPhase::Render;
        renderer_.Render(render_list_, dirty_, *painter_);
        need_render_ = false;
    }

    phase_ = UIPhase::Idle;
}

} // namespace app_ui
