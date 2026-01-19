#include "eteacher/apps/app_demo.h"

#include <string>

namespace {

class DemoApp final : public AppBase {
public:
    explicit DemoApp(std::string title) : meta_{"", std::move(title), ""} {}

    MenuMeta GetMenuMeta() const override { return meta_; }
    void OnEnter(AppContext &ctx) override { (void)ctx; }
    void OnExit(AppContext &ctx) override { (void)ctx; }
    void OnButton(AppContext &ctx, const ButtonEvent &event) override
    {
        (void)ctx;
        (void)event;
    }

private:
    MenuMeta meta_;
};

std::unique_ptr<AppBase> MakeDemoApp(const char *title)
{
    return std::make_unique<DemoApp>(title);
}

} // namespace

std::unique_ptr<AppBase> MakeAppDemo1App() { return MakeDemoApp("app_demo1"); }
std::unique_ptr<AppBase> MakeAppDemo2App() { return MakeDemoApp("app_demo2"); }
std::unique_ptr<AppBase> MakeAppDemo3App() { return MakeDemoApp("app_demo3"); }
std::unique_ptr<AppBase> MakeAppDemo4App() { return MakeDemoApp("app_demo4"); }
std::unique_ptr<AppBase> MakeAppDemo5App() { return MakeDemoApp("app_demo5"); }
std::unique_ptr<AppBase> MakeAppDemo6App() { return MakeDemoApp("app_demo6"); }
std::unique_ptr<AppBase> MakeAppDemo7App() { return MakeDemoApp("app_demo7"); }
std::unique_ptr<AppBase> MakeAppDemo8App() { return MakeDemoApp("app_demo8"); }
std::unique_ptr<AppBase> MakeAppDemo9App() { return MakeDemoApp("app_demo9"); }
std::unique_ptr<AppBase> MakeAppDemo10App() { return MakeDemoApp("app_demo10"); }
std::unique_ptr<AppBase> MakeAppDemo11App() { return MakeDemoApp("app_demo11"); }
std::unique_ptr<AppBase> MakeAppDemo12App() { return MakeDemoApp("app_demo12"); }
std::unique_ptr<AppBase> MakeAppDemo13App() { return MakeDemoApp("app_demo13"); }
std::unique_ptr<AppBase> MakeAppDemo14App() { return MakeDemoApp("app_demo14"); }
std::unique_ptr<AppBase> MakeAppDemo15App() { return MakeDemoApp("app_demo15"); }
std::unique_ptr<AppBase> MakeAppDemo16App() { return MakeDemoApp("app_demo16"); }
