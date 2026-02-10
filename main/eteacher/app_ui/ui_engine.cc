#include "ui_engine.h"

// UI 引擎实现
// 本文件实现 UI 引擎的核心循环、布局与渲染调度、事件处理队列等。
// UI 引擎负责管理根 widget、调度布局/渲染，并将绘制请求发送到 Renderer/Painter。

#include "renderer.h"
#include "widget.h"
#include "debug.h"
#include <cstdio>
#include <functional>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/epd_manager/epd_manager.h"

namespace app_ui {

void UIEngine::OnInput(const InputEvent& e) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (phase_ == UIPhase::Layout || phase_ == UIPhase::Render) {
        deferred_inputs_.push_back(e);
        return;
    }
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
        dirty_.SetFullRect(viewport_);
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
    focus_.Clear();
    active_root_.reset();
    pending_root_.reset();
    cached_root_ = nullptr;
    pending_focus_id_ = 0;
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
    dirty_.SetFullRect(viewport_);
    MarkLayoutDirty();
    ScheduleIfNeeded();
}

InputQueue& UIEngine::Input() {
    return input_queue_;
}

void UIEngine::AddDirty(const Rect& rect, DirtyReason reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    dirty_.Add(rect, reason);
    MarkRenderDirty();
    ScheduleIfNeeded();
}

void UIEngine::MarkFocusDirty() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    focus_.MarkDirty();
    MarkLayoutDirty();
}

void UIEngine::RequestFocus(uint32_t widget_id) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    pending_focus_id_ = widget_id;
    focus_.MarkDirty();
    MarkLayoutDirty();
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
    if (!deferred_inputs_.empty()) {
        for (const auto& pending : deferred_inputs_) {
            input_queue_.Push(pending);
        }
        deferred_inputs_.clear();
    }
    if (pending_root_) {
        // Print debug info about root swap
        Widget* prev_root = active_root_.get();
        app_ui::debug::PrintSceneSwitch(prev_root, pending_root_.get(), pending_focus_id_);
        // Clear focus before destroying the previous root to avoid stale pointers.
        focus_.Clear();
        active_root_ = std::move(pending_root_);
        cached_root_ = nullptr;
        if (active_root_) {
            active_root_->SetEngine(this);
        }
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
        if (cached_root_) {
            cached_root_->SetEngine(this);
        }
        focus_.Clear();
        dirty_.Clear();
        animation_.StopAll();
        style_.MarkDirty(true);
        input_queue_.Clear();
        focus_.MarkDirty();
        MarkLayoutDirty();
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
        need_layout_ = false;
        need_render_ = true;
    }

    focus_.RebuildIfNeeded(root);
    if (pending_focus_id_ != 0) {
        app_ui::debug::PrintApplyPendingFocus(pending_focus_id_, focus_.SetCurrentById(pending_focus_id_));
        // Debug: check visibility of known device_setting widgets (TabView and SavedList)
        Widget* root_check = active_root_.get();
        if (root_check) {
            const uint32_t kTabId = 0xA8EC2EDCu;
            const uint32_t kSavedListId = 0x0DB3CD15u;
            app_ui::debug::PrintDebugVisibility(root_check, kTabId, kSavedListId);
        }
        pending_focus_id_ = 0;
    }

    // Debug: print full widget tree visibility for the active root
    if (active_root_) {
        app_ui::debug::DumpWidgetTree(active_root_.get());
    }

    InputEvent event;
    while (input_queue_.TryPop(event)) {
        dispatcher_.Dispatch(event, root, focus_);
    }

    if (need_render_ && painter_) {
        phase_ = UIPhase::Render;
        // 强制全屏脏区：避免部分刷新导致控件未被重绘（按用户要求牺牲效率以保证完整性）
        dirty_.Add(viewport_, DirtyReason::Full);
        renderer_.Render(render_list_, dirty_, *painter_);
        need_render_ = false;
    }

    phase_ = UIPhase::Idle;
}

} // namespace app_ui
