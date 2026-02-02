

# **ESP32 S3 v3 UI 可视化编辑器方案**

## **1️⃣ 总体目标**

* 在 UI 界面显示 **400×300 的画板**
* 支持控件：Button、Label、Image、Slider、CustomWidget
* 支持操作：

  * 拖拽 / 缩放 / 对齐
  * 属性修改（大小、位置、文本、样式、事件绑定）
  * ZOrder 调整
* 支持生成 JSON 数据 → Python 生成头文件 → ESP32 UIEngine
* 支持 Undo / Redo
* 支持多场景切换

---

## **2️⃣ 总体架构**

```
┌─────────────────────────────┐
│  可视化编辑器 UI层          │
│  ┌───────────────┐          │
│  │ 工具栏 Toolbar │          │
│  └───────────────┘          │
│  ┌───────────────┐          │
│  │ 属性面板       │          │
│  └───────────────┘          │
│  ┌────────────────────────┐ │
│  │ 画板 Canvas 400*300     │ │
│  │  ┌───────────────┐     │ │
│  │  │ WidgetTree     │     │ │
│  │  └───────────────┘     │ │
│  └────────────────────────┘ │
└─────────────────────────────┘

Data Layer:
  - Scene / Widget / Style / Event JSON
  - Undo/Redo Stack
  - Selection State
  - Snap/Grid 信息

Control Layer:
  - InputDispatcher → Canvas → WidgetTree
  - Focus/选中管理
  - 属性修改同步到 JSON & Widget
```

### **2.1 模块划分**

| 模块                    | 功能                                            |
| --------------------- | --------------------------------------------- |
| **Canvas**            | 400×300 画板渲染、控件拖拽、缩放、选中框、网格/辅助线显示             |
| **WidgetTree**        | 当前场景控件树，可操作节点、修改属性、生成 JSON                    |
| **属性面板**              | 显示选中控件属性（位置、大小、样式、文本、事件），可直接编辑                |
| **工具栏 Toolbar**       | 添加控件、删除控件、切换选择模式、保存/生成                        |
| **InputDispatcher**   | 鼠标/触摸事件路由 → Canvas / Widget / Toolbar / 属性面板  |
| **JSON生成器**           | 将 Canvas 中 WidgetTree 生成标准 JSON，可用于 Python生成器 |
| **Undo/Redo Manager** | 所有操作入栈，可撤销/重做                                 |
| **Snap/Grid Manager** | 拖拽对齐、网格辅助线                                    |

---

## **3️⃣ 数据结构**

### **3.1 Widget 数据结构**

```cpp
struct WidgetData {
    std::string id;          // 唯一标识
    std::string type;        // "Button", "Label", "Image"
    int16_t x, y;            // 画板坐标
    int16_t w, h;            // 尺寸
    std::string text;        // 文本
    std::string styleID;     // 样式引用
    int z_order;             // 层级
    std::map<std::string, std::string> events; // {"onClick":"handle_ok"}
};
```

### **3.2 Scene 数据结构**

```cpp
struct SceneData {
    std::string id;
    std::string name;
    std::vector<WidgetData> widgets;
};
```

### **3.3 编辑器状态**

```cpp
struct EditorState {
    SceneData currentScene;
    std::vector<SceneData> scenes;
    std::vector<WidgetData*> selection; // 当前选中控件
    int undoIndex;
    std::vector<SceneData> undoStack;
    std::vector<SceneData> redoStack;
    bool gridVisible;
    bool snapToGrid;
};
```

---

## **4️⃣ Canvas 操作逻辑**

1. **鼠标点击 / 触摸**

   * HitTest → 找到最顶层 Widget
   * 支持多选 (Shift/拖拽框选)

2. **拖拽**

   * 拖拽控件 → 更新 `WidgetData.x/y`
   * Snap/Grid 对齐
   * 更新属性面板

3. **缩放/调整大小**

   * 拖拽控件边缘或角点 → 更新 `w/h`
   * 显示实时尺寸

4. **ZOrder 调整**

   * 上移 / 下移 / 置顶 / 置底
   * 更新 WidgetData.z_order

5. **快捷操作**

   * Ctrl+C / Ctrl+V → 复制/粘贴控件
   * Delete → 删除控件

---

## **5️⃣ 属性面板逻辑**

* **绑定选中控件** → 自动显示属性
* **支持修改属性**：

  * 位置 x/y
  * 尺寸 w/h
  * 文本
  * 样式 styleID
  * 事件绑定
* **修改同步** → 更新 WidgetTree + JSON 数据 + Canvas 重绘

---

## **6️⃣ JSON 生成规则**

```jsonc
{
  "version": "3.0",
  "scenes": [
    {
      "id": "main_scene",
      "name": "MainScene",
      "widgets": [
        {
          "id": "btn_ok",
          "type": "Button",
          "pos": {"x": 50, "y": 100},
          "size": {"w": 80, "h": 40},
          "style": "primary",
          "text": "OK",
          "events": {"onClick":"handle_ok"}
        }
      ]
    }
  ]
}
```

* **自动生成 ID** → 保证唯一
* **事件表绑定** → 保留事件函数名
* **样式引用** → 支持 Python 生成器转换

---

## **7️⃣ Undo / Redo**

* 每次修改 Widget 属性、位置、尺寸、添加/删除控件 → 入栈
* Undo → 回退到上一次 SceneData 状态
* Redo → 重做

---

## **8️⃣ 工具栏与控件操作**

| 功能      | 操作说明                                |
| ------- | ----------------------------------- |
| 添加控件    | 点击 Toolbar 按钮 → 在 Canvas 中点击生成控件    |
| 删除控件    | 选中控件 → Delete                       |
| 选择模式    | 单选/多选/框选                            |
| 对齐工具    | 左对齐 / 右对齐 / 顶对齐 / 底对齐 / 水平居中 / 垂直居中 |
| ZOrder  | 上移 / 下移 / 置顶 / 置底                   |
| 保存 / 导出 | JSON → Python生成器 → C++头文件           |

---

## **9️⃣ 扩展机制**

1. **自定义控件**

   * Python生成器注册控件类型 → 可在画板生成
2. **属性扩展**

   * JSON 可增加自定义字段
   * 属性面板动态生成对应控件输入
3. **多场景管理**

   * 场景切换 → Canvas显示不同 Scene
4. **事件热绑定**

   * 编辑器可绑定函数名，生成 C++ EventBinding 表

---

## **🔟 可视化界面设计**

* **左侧**：工具栏 Toolbar（控件、选择、对齐、ZOrder）
* **中间**：400×300 Canvas
* **右侧**：属性面板（可修改位置、尺寸、文本、样式、事件）
* **底部**：Undo/Redo、Save/Export、Grid/Snap切换

```
┌─────────────┬──────────────┬───────────────┐
│ Toolbar     │ Canvas 400*300│ PropertyPanel │
└─────────────┴──────────────┴───────────────┘
┌───────────────────────────────────────────────┐
│ Undo / Redo | Save | Export | Grid | Snap    │
└───────────────────────────────────────────────┘
```
加“布局约束/对齐规则”与最小网格单位，避免生成不可用坐标。
加“资源面板”（字体/图片/样式）与引用校验，导出前做完整校验。
引入“组件库面板 + 预设模板”，提升复用。
明确“坐标系与缩放”（画布缩放不改变导出尺寸）。
事件绑定支持“参数/动作类型”，不只函数名。
多场景建议“共享样式/资源”与“场景模板”。
生成 JSON 时保留“编辑器元数据”（选中状态、对齐线），导出时剥离。



# **ESP32 S3 v3 UI 框架 Python生成器详细设计文档**

## **1️⃣ 总体目标**

* 使用 Python JSON → 生成 C++ 头文件（`.h`）

* 支持 ESP32 S3 编译

* 完整支持 **方案2/v3 UI框架**：

  * Widget 树
  * Scene/SceneManager
  * 样式系统（StyleManager）
  * 渲染与局部刷新（RenderList / DirtyTracker）
  * 动画（AnimationEngine）
  * Input/Focus/事件系统
  * JSON → Python → 生成头文件 → ESP32编译

* 生成器特点：

  * ID 唯一性检查
  * 事件绑定表
  * 样式/字体/图片引用表
  * Widget 初始化数组
  * Scene 注册初始化

---

## **2️⃣ JSON 数据源设计**

### **2.1 JSON 顶层结构**

```jsonc
{
  "version": "3.0",
  "scenes": [ ... ],
  "styles": [ ... ],
  "fonts": [ ... ],
  "images": [ ... ],
  "widgets": [ ... ] // 可选全局控件
}
```

### **2.2 Scene 定义**

```jsonc
{
  "id": "main_scene",
  "name": "MainScene",
  "widgets": [
    {
      "id": "btn_ok",
      "type": "Button",
      "text": "OK",
      "pos": {"x": 10, "y": 20},
      "size": {"w": 100, "h": 40},
      "style": "primary",
      "events": {
        "onClick": "handle_ok",
        "onLongPress": "handle_ok_long"
      }
    }
  ]
}
```

### **2.3 Style 定义**

```jsonc
{
  "id": "primary",
  "bgColor": 0xFF0000,
  "textColor": 0xFFFFFF,
  "font": "Roboto_16",
  "borderRadius": 4,
  "padding": {"x": 4, "y": 4}
}
```

### **2.4 Font 定义**

```jsonc
{
  "id": "Roboto_16",
  "path": "fonts/Roboto_16.bin",
  "size": 16
}
```

### **2.5 Image 定义**

```jsonc
{
  "id": "icon_ok",
  "path": "images/ok.bin",
  "w": 32,
  "h": 32
}
```

---

## **3️⃣ Python生成器设计**

### **3.1 功能**

1. 解析 JSON 数据
2. 校验 ID 唯一性、资源路径
3. 生成 C++ 头文件：

   * Widget ID、Style ID、Font ID、Image ID
   * Widget 初始化数组
   * 事件绑定表
   * Scene 注册函数
4. 支持多语言/国际化
5. 支持资源热更新/版本管理
6. 支持扩展控件/自定义控件

---

### **3.2 模块划分**

| 模块                    | 功能                  |
| --------------------- | ------------------- |
| `parser.py`           | 解析 JSON、校验、生成中间数据结构 |
| `id_generator.py`     | 生成唯一 ID（16 位或 32 位） |
| `cpp_writer.py`       | 根据中间数据生成 `.h` 文件    |
| `resource_manager.py` | 管理字体/图片引用计数、路径检查    |
| `event_manager.py`    | 生成事件绑定表             |
| `scene_manager.py`    | 生成 Scene 注册和初始化代码   |
| `i18n_manager.py`     | 国际化文本生成 ID          |

---

### **3.3 中间数据结构（Python）**

```python
class Widget:
    def __init__(self, id, type, pos, size, style, text="", events={}):
        self.id = id
        self.type = type
        self.pos = pos  # {'x':int, 'y':int}
        self.size = size  # {'w':int, 'h':int}
        self.style = style
        self.text = text
        self.events = events  # {'onClick':'func_name'}

class Scene:
    def __init__(self, id, name, widgets=[]):
        self.id = id
        self.name = name
        self.widgets = widgets

class Style:
    def __init__(self, id, bgColor, textColor, font, **kwargs):
        self.id = id
        self.bgColor = bgColor
        self.textColor = textColor
        self.font = font
        self.extra = kwargs

class Font:
    def __init__(self, id, path, size):
        self.id = id
        self.path = path
        self.size = size

class Image:
    def __init__(self, id, path, w, h):
        self.id = id
        self.path = path
        self.w = w
        self.h = h
```

---

## **4️⃣ C++头文件结构**

### **4.1 ID常量**

```cpp
namespace UI {
namespace WidgetID {
    constexpr int BTN_OK = 0xA12B;
    constexpr int LBL_TITLE = 0xF34C;
}
namespace StyleID {
    constexpr int PRIMARY = 0x1F2E;
    constexpr int TITLE = 0x3B4A;
}
namespace FontID {
    constexpr int ROBOTO_16 = 0x5C6D;
    constexpr int ROBOTO_18_BOLD = 0x7A8B;
}
namespace ImageID {
    constexpr int ICON_OK = 0x9F1D;
}
}
```

### **4.2 Widget初始化结构**

```cpp
struct WidgetInit {
    const char* id;
    const char* type;
    int x, y, w, h;
    int styleID;
    const char* text;
};

constexpr WidgetInit g_widgets[] = {
    {"btn_ok", "Button", 10, 20, 100, 40, UI::StyleID::PRIMARY, "OK"},
    {"lbl_title", "Label", 10, 0, 200, 30, UI::StyleID::TITLE, "Hello World"},
};
```

### **4.3 事件绑定表**

```cpp
struct EventBinding {
    int widgetID;
    const char* eventType; // "onClick", "onLongPress"
    void(*handler)();      // 函数指针
};

constexpr EventBinding g_eventBindings[] = {
    {UI::WidgetID::BTN_OK, "onClick", handle_ok},
    {UI::WidgetID::BTN_OK, "onLongPress", handle_ok_long}
};
```

### **4.4 Scene注册函数**

```cpp
void registerScenes() {
    Scene* scene = new Scene("MainScene");
    for (auto& w : UI::g_widgets) {
        Widget* widget = WidgetFactory::Create(w.type);
        widget->SetPos(w.x, w.y);
        widget->SetSize(w.w, w.h);
        widget->ApplyStyle(w.styleID);
        widget->SetText(w.text);
        scene->AddChild(std::unique_ptr<Widget>(widget));
    }
    SceneManager::Instance().Push(scene);
}
```

---

## **5️⃣ 扩展机制**

1. **自定义控件**

   * JSON 中 `"type": "CustomSlider"`
   * Python生成器生成 WidgetInit + Factory注册

2. **国际化**

   * 文本字段 `textID` → C++ `constexpr int TEXT_ID_XYZ`
   * 多语言表在 Flash / SPIFFS

3. **热更新**

   * JSON 重新生成头文件
   * ESP32 重置或 Scene 切换加载新 UI

4. **事件热绑定**

   * Python生成器生成 `EventBinding` 表
   * 支持在运行时通过 ID 动态绑定/解绑

---

## **6️⃣ Python生成器工作流程**

```
ui.json -> parser.py -> ID生成器 -> resource_manager.py -> event_manager.py -> cpp_writer.py
-> ui_generated.h -> ESP32 编译 -> UIEngine 使用
```

1. **解析 JSON** → 生成 Python对象
2. **ID生成** → WidgetID / StyleID / FontID / ImageID
3. **校验** → 重复ID/缺失资源/文本为空
4. **事件绑定表生成** → EventBinding
5. **C++头文件生成** → `ui_generated.h`
6. **ESP32 UIEngine初始化** → SceneManager注册Widget

---


