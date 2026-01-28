# eteacher/app_service/tool
**Tool 文件详解**
此目录包含与 UI、控件与小工具相关的实现，主要用于在 EPD（墨水屏）设备上绘制并处理用户交互。模块化设计将绘制、输入处理与资源加载分离：控件负责绘制和语义输入，显示刷新由上层应用或渲染器调度。

## 文件总览

- `ui_widget.h` / `ui_widget.cc` — 基础控件及常用控件（Label、Button、List、TextInput、Dialog）。
- `soft_keyboard.h` / `soft_keyboard.cc` — 软键盘控件（4x9 网格，3 页），带选择、确认和回调接口。
- `status_bar.h` / `status_bar.cc` — 顶部/底部状态栏绘制与共享绘图工具（图标加载、字体度量、电池绘制等）。
- `seq_button.h` / `seq_button.c` — 低层连击/序列检测（C API），用于聚合短时间内的按键事件。
- `nav_sequence.h` / `nav_sequence.cc` — 将 seq 事件映射为 `AppButton` 序列并提供网格导航辅助函数。
- `tabs.h` / `tabs.cc` — 选项卡视图（上部选项区 + 下部属性区），支持键盘导航与属性编辑。
- `image_play.cc` — 简单图片序列播放工具，用于播放 assets 中按序号排列的图片资源。

## 设计要点

- 绘制由调用方触发：所有 `Draw(...)` 接口都在调用者上下文（通常为应用主循环或渲染器）中被调用，控件不主动发起全局 EPD 刷新。
- 语义按键与事件：底层按键通过 `seq_button` 等模块处理后转换为语义 `Action` / `ButtonEvent`，控件只需处理语义事件，从而与物理按键映射解耦。
- 回调与输出：需要把事件或输入传给应用时，控件提供回调（例如 `SetOnInput`、`SetOnDelete`）或通过 `HandleButton` 的输出参数返回数据。

## 模块详解

### `ui_widget`（基础控件）

- `WidgetBase`：管理绑定的 `Region`、脏矩形与焦点。主要方法：`AttachRegion()`、`rect()`、`Draw()`、`OnAction()`、`MarkDirty()`。
- 常用控件：`LabelWidget`, `ButtonWidget`, `ListWidget`, `TextInputWidget`, `DialogWidget`。这些控件封装了常见的文本绘制、选择与交互模式。

使用模式：应用创建控件、设置区域或通过布局引擎分配 `Region`，在事件循环中将语义按键转给控件，在渲染周期调用 `Draw()`。

### `soft_keyboard`（软键盘）

- 网格：4 行 × 9 列，3 页（小写、 大写、 符号）。
- 主要接口：
  - 显示/关闭：`ShowKeyboard(CustomEpdDisplay*, x,y,w,h)`、`ShowKeyboard(CustomEpdDisplay*)`、`CloseKeyboard()`。
  - 输入处理：`HandleButton(const ButtonEvent&, std::string& output, bool* consumed)`。确认（D）会通过 `output` 返回字符并返回 true；A 键切换页；方向键移动选中；Delete 会触发删除回调。
  - 状态查询：`IsVisible()`, `GetSelectedLabel()`, `GetBounds(...)`。
- 绘制：`Draw(Adafruit_GFX&, CustomEpdDisplay*)` 绘制键盘和圆角选中框。建议与 `seq_button` 配合：在一次连击/单击序列结束时，统一更新选中位置并执行一次局部刷新以减少 EPD 刷新次数。

示例（伪代码）：
```cpp
KeyboardWidget kb;
kb.SetOnInput([](std::string_view s){ /* append to text input */ });
kb.ShowKeyboard(epd);
// 在主循环绘制
kb.Draw(gfx, epd);
// 按键事件
std::string out;
if (kb.HandleButton(evt, out, &consumed)) { /* got char in out */ }
```

### `status_bar`（状态栏与绘图工具）

- 提供 `DrawTopBar(...)`、`DrawBottomBar(...)`。
- 共享工具：`BinImage`、`LoadBinImage()`、`GetFontHeight()`、`GetFontAscent()`、`DrawIconWithText()`、`DrawBatteryIcon()` 等，供菜单与状态栏复用。

### `seq_button` / `nav_sequence`

- `seq_button`：收集按键短按并在超时窗口后生成 `seq_event_t`，支持注册回调。用于实现短时间内多次按键的“连击”识别。
- `nav_sequence`：将 `seq_event_t` 转换为 `AppButton` 序列，并提供网格移动应用函数（例如把连续的 Up/Down/Left/Right 映射为网格中的行列移动）。

### `tabs`（选项卡）

- `TabView`：选项卡展示 + 属性区。通过 `TabConfig` 设置位置、大小与网格布局。提供 `Show()/Close()/HandleButton()` 等接口，C 键进入属性编辑，B 键返回。

### `image_play`

- 简单图片播放器，按序号播放 `assets` 下的图片资源，常用于演示或欢迎动画。

## 集成建议

- 对于高频方向键输入，使用 `seq_button` 聚合事件，在序列决策后执行一次局部刷新，以减少 EPD 更新频率并改善体验。
- 控件绘制与资源加载（字体、图标）使用现有的 `font_manager` 和 `assets` 接口，避免重复实现。

## 后续工作建议

- 可以补充：
  - `combo_click` 示例实现并与 `soft_keyboard` 的局部刷新集成；
  - 一个小 demo 应用展示 `TextInputWidget` + `KeyboardWidget` 的完整交互流程；
  - 为头文件增加更详尽的注释与 API 示例。

文件位置：`main/eteacher/app_service/tool`



帮我编写连击代码，在combo_click.cc和其头文件中修改，不修改其它文件
1.单击事件通过EnglishTeacherBoard::InitializeButtons()中onclick获取，仅对上下左右做连击事件；
2.编写一个上下左右按键进行组合连击的事件，如连续单击 上 上 下 下 或者 上 上 上 上  上 等作为一次组合连击事件，提供统一的API接口，返回类型为单击事件或连击事件，包括连击的内容，供其它模块调用
3.默认两次click按键周期不超过150ms就可以作为连击事件，如果没有连击事件发生时就按单击事件处理
4.用一个统一API返回给调用对象，如果是单击事件返回单击事件，如果是连击事件返回连击事件




基于eteacher\app_service和eteacher\app_ui文件夹下资源，设计一个软键盘控件，代码编写在eteacher\app_ui\soft_keyboard控件，满足以下功能：
1.软键盘每页是4行9列，一共3页，按照设定的键盘尺寸均匀分布，每个按钮格宽和高相等；
2.第1页包括小写字母a到z，数字0到9,第2页包括大写字母A到Z和! " # $ % & ' ( ) * 第3页包括 + , - . / : ; < = > ? @ [ \ ] ^ _ ` { | } ~ "空格"
3.通过上下左右按键可以按照行和列进行选择，当在行列首尾处继续方向键时从反方向选中；
4.通过D键确定按键输出，并返回该按键信息，通过A键对软键盘在第1页，第2页和第3页切换
5.选中的字母用圆角矩形圈上
6.通过eteacher\app_ui\seq_button连击按键逻辑判断，当输出单击或者组合连击事件时，按照返回的单击事件或连击事件一次性移动矩形选中框;





image_play，可以播放该文件夹下根据序号播放所有图片，每张0.3s，一共3秒


