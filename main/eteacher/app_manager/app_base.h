#pragma once

#include <functional>
#include <string>

// Lightweight App abstraction inspired by the reference project.
class AppBase {
public:
    AppBase(const std::string& name, const std::string& title);
    virtual ~AppBase() = default;

    virtual void OnEnter();
    virtual void OnResume();
    virtual void OnTick(uint32_t delta_ms);
    virtual void OnExit();

    const std::string& name() const { return name_; }
    const std::string& title() const { return title_; }
    bool show_in_list() const { return show_in_list_; }

protected:
    bool show_in_list_ = true;

private:
    std::string name_;
    std::string title_;
};

// Simple action-driven app that runs callbacks on enter/exit.
class ActionApp : public AppBase {
public:
    ActionApp(const std::string& name,
              const std::string& title,
              std::function<void()> on_enter = {},
              std::function<void()> on_exit = {});

    void OnEnter() override;
    void OnExit() override;

private:
    std::function<void()> on_enter_;
    std::function<void()> on_exit_;
};
