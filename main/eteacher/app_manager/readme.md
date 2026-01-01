AppManager 是 englishteacher 设备的简易应用管理器，设备启动后由 main.cc 启动。

- AppManager 从 Board 获取 Display，用于菜单/状态渲染。
- AppManager 提供 app 注册机制，注册后出现在菜单。
- AppManager 将 Board 透传给 app，便于 app 自己控制按键/外设。

当前重构要点：

1) 按键事件：由板级代码直接调用 `AppManager::HandleButton(const ButtonEvent&)`，不再需要 `ButtonManager` 中转。硬件按键需要自己映射到 `AppButton::{Up,Down,Select,Back, Ptt, PttAlt}`。
2) App 基类：`AppBase` 提供 `GetMenuMeta()`（菜单名称/副标题/唯一 key）、`OnEnter/OnExit/OnButton/OnTick`，统一传入 `AppContext`（包含 `Board&`）。
3) AppManager：`Init(Board&)` 保存上下文并绘制菜单，`Register(unique_ptr<AppBase>)` 注册 app，`ShowMenu()` 返回菜单，`HandleButton()` 分发按键，`Tick(delta_ms)` 让当前 app 收到周期事件。
4) 菜单 UI：默认用 `Board::GetDisplay()->SetChatMessage()` 画一个文本菜单；需要更复杂的 EPD/LVGL，可以在 `RenderMenu/RenderStatus` 里替换实现。

接入步骤示例：

```cpp
// main.cc
auto &app_mgr = AppManager::GetInstance();
app_mgr.Init(Board::GetInstance());
app_mgr.Register(std::make_unique<ActionApp>(
	MenuMeta{"ai_chat", "AI Chat", "press PTT"},
	[](AppContext &ctx){ ctx.board.GetDisplay()->SetStatus("AI Chat"); },
	[](AppContext &ctx){ ctx.board.GetDisplay()->SetStatus("Menu"); },
	[](AppContext &, const ButtonEvent &ev){ /* handle PTT... */ }
));

// 在板级按键回调里：
app_mgr.HandleButton(ButtonEvent{AppButton::Up});
```