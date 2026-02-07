#include "ui_engine.h"

#include "renderer.h"
#include "widget.h"

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"

namespace app_ui {

void UIEngine::OnInput(const InputEvent& e) {
    input_queue_.Push(e);
}

void UIEngine::RequestLayout() {
    MarkLayoutDirty();
    ScheduleIfNeeded();
}

void UIEngine::RequestRender() {
    MarkRenderDirty();
    ScheduleIfNeeded();
}

void UIEngine::SetEpd(::CustomEpdDisplay* epd) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    epd_ = epd;
    if (epd_) {
        viewport_ = {0, 0, static_cast<int16_t>(epd_->width()),
                     static_cast<int16_t>(epd_->height())};
        RequestLayout();
    }
}

void UIEngine::SetRoot(std::unique_ptr<Widget> root) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    pending_root_ = std::move(root);
    MarkLayoutDirty();
    ScheduleIfNeeded();
}

void UIEngine::Reset() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    active_root_.reset();
    pending_root_.reset();
    cached_root_ = nullptr;
    focus_.Clear();
    input_queue_.Clear();
    dirty_.Clear();
    animation_.StopAll();
    style_.MarkDirty(true);
    need_layout_ = true;
    need_render_ = true;
}

void UIEngine::SetPainter(Painter* painter) {
    painter_ = painter;
}

void UIEngine::SetViewport(const Rect& rect) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    viewport_ = rect;
    MarkLayoutDirty();
    ScheduleIfNeeded();
}

InputQueue& UIEngine::Input() {
    return input_queue_;
}

void UIEngine::MarkLayoutDirty() {
    need_layout_ = true;
    need_render_ = true;
}

void UIEngine::MarkRenderDirty() {
    need_render_ = true;
}

void UIEngine::ScheduleIfNeeded() {
    if (!epd_) {
        return;
    }
    if (scheduled_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    EpdManager::GetInstance().Schedule(
        EpdManager::TaskType::kPartial,
        &UIEngine::RenderCallback,
        this,
        nullptr,
        EpdManager::Rect(0, 0, epd_->width(), epd_->height()));
}

void UIEngine::RenderCallback(::Adafruit_GFX& gfx, void* ctx) {
    auto* engine = static_cast<UIEngine*>(ctx);
    if (!engine) {
        return;
    }
    engine->RenderInternal(gfx);
}

void UIEngine::RenderInternal(::Adafruit_GFX& gfx) {
    if (!epd_) {
        scheduled_.store(false, std::memory_order_release);
        return;
    }
    EpdPainter painter(epd_, gfx);
    SetPainter(&painter);
    Tick(0);
    SetPainter(nullptr);
    scheduled_.store(false, std::memory_order_release);
    if (need_layout_ || need_render_) {
        ScheduleIfNeeded();
    }
}

void UIEngine::Tick(uint32_t delta_ms) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (pending_root_) {
        active_root_ = std::move(pending_root_);
        cached_root_ = nullptr;
        focus_.Clear();
        dirty_.Clear();
        animation_.StopAll();
        style_.MarkDirty(true);
        input_queue_.Clear();
        MarkLayoutDirty();
    }
    Widget* root = active_root_.get();
    if (!root) {
        dirty_.Clear();
        need_layout_ = false;
        need_render_ = false;
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
        MarkLayoutDirty();
    }

    InputEvent event;
    while (input_queue_.TryPop(event)) {
        dispatcher_.Dispatch(event, root, focus_);
    }

    if (style_.ConsumeLayoutDirty()) {
        MarkLayoutDirty();
    } else if (style_.ConsumeRenderDirty()) {
        MarkRenderDirty();
    }

    if (animation_.Tick(delta_ms)) {
        MarkRenderDirty();
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
