# eteacher App Manager 说明

**概述**:
- **作用**: 管理设备上的可执行应用（App），负责菜单渲染、按键分发与应用生命周期管理。
- **主要组件**: `AppManager`, `AppBase`/`ActionApp`, `Menu`, `MenuController`。

**设计要点**:
- `AppManager` 持有已注册的 `AppBase` 列表并维护当前选中与运行的 App。
- 菜单在支持的 EPD（`CustomEpdDisplay`）上以部分刷新方式绘制（通过 `EpdManager::Schedule`），非 EPD 设备则走字符/聊天消息显示。
- 输入事件由上层板级代码收集并调用 `AppManager::HandleButton` 分发。
- `Tick(uint32_t delta_ms)` 用于驱动菜单状态的周期性刷新和运行中 App 的定时回调。

**主要类型与接口**:
- `AppContext`: 封装对 `Board` 的引用，传入 App 的生命周期回调中。
- `AppBase`:
	- 纯虚接口，App 必须实现 `GetMenuMeta()`, `OnEnter()`, `OnExit()`, `OnButton()`，可选实现 `OnTick()`。
	- `show_in_menu()` 控制是否显示在菜单上；`icon()` / `SetIcon()` 管理图标路径。
- `ActionApp`: 一个轻量回调式实现，便于用 lambda 快速构建简单 App。
- `AppButton` / `ButtonAction` / `ButtonEvent`: 定义按键 ID、动作类型与上报事件结构。

**AppManager 关键方法**:
- `Init(Board &board)`: 在启动时由 `main.cc`（或等效启动位置）调用，传入 `Board`。
- `Register(std::unique_ptr<AppBase>)`: 注册 App，若 `show_in_menu()==true` 则显示在菜单中。
- `FinalizeRegistration()`: 通知注册完成并首次渲染菜单。
- `ShowMenu()`: 强制进入菜单界面（退出当前运行 App）。
- `HandleButton(const ButtonEvent&)`: 按键入口，负责菜单移动、进入/退出 App 以及把事件转发给运行中的 App。
- `Tick(uint32_t)`: 驱动分钟变更、WiFi/电量变化导致的菜单刷新，或转发给正在运行的 App 的 `OnTick`。

**菜单渲染与样式（Menu / MenuStyle）**:
- `MenuStyle` 定义上栏/下栏高度、图标格尺寸、内边距、字体等。
- `Menu::ComputeLayout(screen_w, screen_h, item_count)` 计算网格布局（列/行/每页项数）。
- `Menu::Draw(...)` 在 EPD 上绘制：顶部状态栏（时间、音量、电量、WiFi）、中间图标网格、底部 footer 文本。
- 图标以二进制 `.bin` 资源加载（见 `assets` 机制）；默认图标名规则：若 `App` 未设置 icon，则使用 `MenuMeta.key + ".bin"`。

**按键与导航逻辑**:
- 菜单方向键: `Up/Down/Left/Right`（`VolumeUp/VolumeDown` 同步映射为上下）移动选择。
- `Start` 键用于 `EnterCurrent()`（启动选中 App）；`Select` 在 App 运行时用于 `ExitCurrent()`（退出 App 回菜单）。
- 当存在运行中的 App 时，按键事件会转发给该 App 的 `OnButton`，除非是 `Select` 用于退出。

**状态渲染**:
- `RenderStatus(headline, detail)` 用于显示简短的系统状态页，支持 EPD 部分刷新或 fallback 到聊天消息样式显示。

**示例：注册一个简单 App（使用 `ActionApp`）**
```cpp
using namespace eteacher;

// 在 board 初始化后（例如 main.cc）
auto &mgr = eteacher::AppManager::GetInstance();
mgr.Init(board); // board 为 Board&，由主流程调用一次

// 构建菜单元信息
MenuMeta meta;
meta.key = "hello";
meta.title = "Hello";
meta.subtitle = "示例应用";

// 简单回调 App
mgr.Register(std::make_unique<ActionApp>(
		meta,
		/* on_enter */ [](AppContext &ctx){ ctx.board.GetDisplay()->SetChatMessage("system","Hello Enter"); },
		/* on_exit  */ [](AppContext &ctx){ ctx.board.GetDisplay()->SetChatMessage("system","Hello Exit"); },
		/* on_button*/ [](AppContext &ctx, const ButtonEvent &e){ /* 处理按键 */ },
		/* icon   */ "hello.bin"
));

// 注册结束后调用一次以显示菜单
mgr.FinalizeRegistration();
```

**关于图标资源**:
- 图标为 1-bit 位图二进制，前 4 字节为宽度/高度（LE16），后跟像素数据（按行打包，每行字节数为 (w+7)/8）。
- 加载逻辑在 `menu.cc` 的 `LoadBinImage()`，若资源尺寸不足会用空白占位。

**实现细节提示**:
- 若目标设备支持 `CustomEpdDisplay`，AppManager 会走 `EpdManager::Schedule` 提交绘制任务以实现非阻塞部分刷新。
- `MenuController` 负责根据 `MenuLayout` 做光标移动逻辑，支持环绕移动与分页。
- 菜单顶部会显示当前时间、音量、电池与 WiFi 状态；`AppManager::Tick` 每秒累计计时以在分钟变化或状态变化时刷新菜单。

**集成与扩展建议**:
- 在 `main.cc` 或板级初始化流程中调用 `AppManager::Init` 并注册所有 App；最后调用 `FinalizeRegistration()`。
- 复杂 App 可继承 `AppBase`，在 `OnEnter` 中启动自己的 UI 或任务，在 `OnExit` 中清理资源并在 `OnTick` 中处理周期逻辑。
- 若希望隐藏某个 App（例如只供内部调用），将 `show_in_menu_` 设为 `false`。

**参考源码**:
- `app_manager.cc`, `app_manager.h`, `app_base.h`, `menu.cc`, `menu.h`（位于 `main/eteacher/app_manager/`）

---

# 墨水屏布局引擎（LayoutEngine）

**目标**：为 EPD 提供“分区（Region）+模板（LayoutTemplate）”的稳定布局机制，支持多 Primary/Secondary/List/Overlay，并可扩展自定义模板。

## 主要类型说明
- **Rect**：矩形（x, y, w, h）
- **RegionId**：分区类型 + index（支持多个 Primary/Secondary/List/Overlay）
- **Region**：分区状态（可见、焦点、局部刷新、冻结、z_order）
- **LayoutTemplate**：布局模板（内置 + 自定义）
- **LayoutParams**：边距、间距、默认尺寸、分区数量等
- **LayoutResult**：最终区域列表
- **LayoutEngine**：根据模板计算布局结果

## 内置模板（示例）
- **FocusContent**：Header + Primary + Secondary + Footer
- **ListBrowse**：Header + List(可多个) + Footer
- **Dialog**：Overlay 居中弹窗（底层区域冻结）
- **FullScreenText**：Primary 全屏
- **InputKeyboard**：Header + Primary(输入预览) + Secondary(键盘) + Footer

## 示例：App 使用 LayoutEngine
```cpp
#include "eteacher/app_manager/layout_engine.h"

class MyApp final : public AppBase {
public:
	MenuMeta GetMenuMeta() const override {
		return {"my_app", "布局示例", "支持多区域"};
	}

	// 指定模板
	eteacher::layout::LayoutTemplate Template() const override {
		return eteacher::layout::LayoutTemplate::InputKeyboard();
	}

	// 接收布局结果
	void OnLayout(const eteacher::layout::LayoutResult &layout) override {
		layout_ = layout; // 保存起来用于绘制
	}

	// 按区域绘制
	void OnDraw(const eteacher::layout::RegionId &id) override {
		const auto *region = layout_.Find(id);
		if (!region || region->rect.IsEmpty()) {
			return;
		}
		// 在 region->rect 内绘制
	}

private:
	eteacher::layout::LayoutResult layout_{};
};
```

## 示例：自定义模板
```cpp
// 自定义模板：Header + List1 + List2 + Footer
static bool BuildDoubleList(const eteacher::layout::ScreenInfo &screen,
							const eteacher::layout::LayoutParams &params,
							eteacher::layout::LayoutResult &out)
{
	eteacher::layout::LayoutParams p = params;
	p.list_count = 2;
	p.primary_count = 0;
	p.secondary_count = 0;
	// 复用引擎内置 ListBrowse
	out = eteacher::layout::LayoutEngine::Compute(
		eteacher::layout::LayoutTemplate::ListBrowse(), screen, p);
	return true;
}

auto tpl = eteacher::layout::LayoutTemplate::Custom("DoubleList", &BuildDoubleList);
```

## 示例：布局参数设置（多 Primary/Secondary/List/Overlay）
```cpp
eteacher::layout::LayoutParams params;
params.primary_count = 2;   // Primary1 + Primary2
params.secondary_count = 1; // Secondary1
params.list_count = 2;      // List1 + List2
params.overlay_count = 1;   // Overlay1
params.secondary_height = eteacher::layout::SizeSpec::Percent(45); // 输入键盘高度
```

> 说明：Overlay 不影响底层布局，仅叠加显示；在 Dialog 模板或 `freeze_under_overlay=true` 时，底层区域会被冻结。

欢迎反馈要补充的使用示例或流程图。
