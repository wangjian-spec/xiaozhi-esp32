#include "eteacher/app_manager/app_base.h"

AppBase::AppBase(const std::string &name, const std::string &title)
    : name_(name), title_(title) {}

void AppBase::OnEnter() {}
void AppBase::OnResume() {}
void AppBase::OnTick(uint32_t /*delta_ms*/) {}
void AppBase::OnExit() {}

ActionApp::ActionApp(const std::string &name,
                     const std::string &title,
                     std::function<void()> on_enter,
                     std::function<void()> on_exit)
    : AppBase(name, title), on_enter_(std::move(on_enter)), on_exit_(std::move(on_exit)) {}

void ActionApp::OnEnter()
{
    if (on_enter_)
    {
        on_enter_();
    }
}

void ActionApp::OnExit()
{
    if (on_exit_)
    {
        on_exit_();
    }
}
