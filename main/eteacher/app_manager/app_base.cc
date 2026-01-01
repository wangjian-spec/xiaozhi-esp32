#include "eteacher/app_manager/app_base.h"

ActionApp::ActionApp(MenuMeta meta,
                     std::function<void(AppContext &)> on_enter,
                     std::function<void(AppContext &)> on_exit,
                     std::function<void(AppContext &, const ButtonEvent &)> on_button)
    : meta_(std::move(meta)), on_enter_(std::move(on_enter)), on_exit_(std::move(on_exit)), on_button_(std::move(on_button)) {}

void ActionApp::OnEnter(AppContext &ctx)
{
    if (on_enter_)
    {
        on_enter_(ctx);
    }
}

void ActionApp::OnExit(AppContext &ctx)
{
    if (on_exit_)
    {
        on_exit_(ctx);
    }
}

void ActionApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    if (on_button_)
    {
        on_button_(ctx, event);
    }
}
