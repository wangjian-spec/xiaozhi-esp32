# app_ui — UI 架构与工作原理（当前版本）

本文件描述当前项目 UI 架构、工作原理、工作流程、调用关系、数据结构与使用方法。
内容基于 `main/eteacher/app_ui` 当前实现（运行期 Widget Tree + JSON 解析 + 渲染管线）。

---

# 1. 架构总览

## 1.1 分层架构

当前 UI 体系分为 3 层：

1. **UI 描述层（资源层）**
	 - JSON / Generated C++ 只做“资源描述”，不参与运行期状态。
2. **运行期对象层（Runtime）**
	 - `Widget` / `Scene` / `FocusManager` 等构成 Widget Tree 与交互状态。
3. **渲染执行层（Renderer / Painter）**
	 - `RenderList` → `Renderer` → `Painter` 完成绘制与刷新。

依赖方向为单向：资源层 → 运行期层 → 渲染层。

## 1.2 模块划分

- **资源描述层**：`ui_layout_types.h`（资源结构定义）
- **运行期对象层**：`widget.h/.cc`, `scene.h/.cc`, `scene_runtime.*`, `focus_manager.*`
- **布局系统**：`layout_engine.*`, `layout_cache.h`
- **渲染系统**：`renderer.*`, `dirty_tracker.*`, `painter.h`
- **Widget 构建**：`widget_builder.*`
- **Schema 校验**：`ui_schema_validator.*`
- **基础控件**：`basic_widgets.*`
- **输入系统**：`input.h/.cc`
- **引擎调度**：`ui_engine.*`

## 1.3 当前合并说明（文件精简）

- `RenderList` 已合并进 `renderer.h/.cc`
- `SceneManager` 已合并进 `scene.h/.cc`
- 输入系统合并为 `input.h/.cc`
- `WidgetFactory` 与资源树构建合并进 `widget_builder.*`

---

# 2. 工作原理

## 2.1 Scene 与 Widget Tree

- Scene 必须拥有 **root**，并以 root 为入口形成树。
- Widget Tree 是运行期对象结构；所有状态只存在运行期对象。
- Scene 生命周期：`OnEnter` → `OnPause/OnResume` → `OnExit`。

## 2.2 资源/运行期边界

- 资源层只保存构造参数（type/rect/properties/资源引用）。
- 运行期仅保存状态（visible/enabled/checked/value/focus）。
- 状态变化不会写回资源层。

## 2.3 严格 Schema

- `scene.root` 必须存在且合法。
- 每个 widget 必须有 `type` 与完整 `rect`。
- `children` 只能出现在容器类型。
- Builder 不做默认补齐，缺失字段即失败。

---

# 3. 数据结构

## 3.1 JSON 顶层结构（严格树模型）

```json
{
	"meta": {},
	"resources": { "texts": {}, "images": {}, "fonts": {} },
	"styles": {},
	"themes": {},
	"data": {},
	"scenes": {
		"scene_id": { "name": "", "root": "widget_id" }
	},
	"widgets": {
		"widget_id": {
			"type": "Button",
			"rect": {"x":0,"y":0,"w":0,"h":0},
			"properties": {},
			"children": ["child_widget_id"]
		}
	},
	"events": [],
	"navigation": { "focus": {}, "scene_flow": {} },
	"states": {}
}
```

## 3.2 资源层结构（C++）

- `resource::WidgetInit` / `resource::SceneInit`
- `GeneratedTextResource` / `GeneratedImageResource` / `GeneratedFontResource`
- `GeneratedStyle` / `GeneratedTheme`

## 3.3 运行期结构

- `Widget`：运行期 UI 对象
- `Scene` / `SceneManager`：Scene 栈与生命周期
- `SceneRuntime` / `runtime::SceneManager`：JSON 场景加载
- `LayoutEngine`：Measure / Layout
- `Renderer` + `RenderList`：绘制排序与刷新

---

# 4. 工作流程

## 4.1 UI 开发流程

1. UI 编辑器导出 JSON（Scene 必须有 root）
2. 可选：Python 生成器生成 C++ 头文件
3. 运行期加载 JSON 或 Generated 资源

## 4.2 启动流程（Runtime JSON）

1. 读取 JSON
2. `UiSchemaValidator::Validate`
3. `WidgetBuilder::BuildScene` 递归构建树
4. `FocusManager::Build`
5. `LayoutEngine::LayoutTree`
6. `Renderer::Render`

## 4.3 Scene 切换流程

- `SceneManager::Push/Pop/Replace`
- Scene 生命周期回调执行

---

# 5. 调用关系（Call Graph）

## 5.1 模块级调用链

- App → `runtime::SceneManager::LoadFromJson`
- `UiSchemaValidator` → `WidgetBuilder`
- `LayoutEngine` → `Renderer` → `Painter`

## 5.2 渲染链

`RenderList::Build` → `Renderer::Render` → `Painter`（设备绘制）

## 5.3 输入链

`InputQueue` → `InputDispatcher` → `Widget::OnInput`

---

# 6. 使用方法

## 6.1 运行期 JSON 使用

1. 准备 JSON（root + children 树结构）
2. 调用 `runtime::SceneManager::LoadFromJson` 加载 Scene
3. 使用 `LayoutEngine` + `Renderer` 进行渲染

## 6.2 关键入口

- Schema 校验：`UiSchemaValidator::Validate`
- Widget 构建：`WidgetBuilder::BuildScene`
- 场景管理：`SceneManager` / `runtime::SceneManager`
- 渲染管线：`LayoutEngine` → `RenderList` → `Renderer`
- 输入处理：`InputQueue` / `InputDispatcher`

---

# 7. 设计原则与约束

- **单向依赖**：资源层不依赖运行期与渲染层。
- **严格 Schema**：缺字段直接失败，避免隐式行为。
- **状态只在运行期**：资源层不可保存运行期状态。
- **易扩展**：新增控件仅扩展 `basic_widgets` 与 `widget_builder` 映射。

---

# 8. 目录索引（当前关键文件）

- 入口：`ui_engine.*`
- Scene：`scene.h/.cc`，`scene_runtime.*`
- Builder：`widget_builder.*`
- Schema：`ui_schema_validator.*`
- 渲染：`renderer.*`，`dirty_tracker.*`，`painter.h`
- 布局：`layout_engine.*`
- 控件：`basic_widgets.*`
- 输入：`input.h/.cc`

