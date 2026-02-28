#pragma once

#include <cstdint>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include "types.h"

// 输入接口定义
// 本头文件定义了输入事件类型、输入队列和焦点管理器的接口，供 UI 引擎使用。

namespace app_ui {

enum class InputType {
    KeyDown,
    KeyUp,
    KeyRepeat,
    LongPress,
    DoubleClick,
    Combo,
    PointerDown,
    PointerUp,
    PointerMove
};

enum class KeyCode : int {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
    A = 4,
    B = 5,
    C = 6,
    D = 7,
    Select = 8,
    Start = 9,
    VolumeUp = 10,
    VolumeDown = 11,
};

struct InputEvent {
    InputType type = InputType::KeyDown;
    int key = 0;
    Point pos{};
    uint32_t timestamp = 0;
};

enum class InputPhase : uint8_t {
    Capture,
    Target,
    Bubble,
};

class InputQueue {
public:
    void Push(const InputEvent& e);
    bool TryPop(InputEvent& out);
    void Clear();

private:
    mutable std::mutex mutex_;
    std::queue<InputEvent> queue_;
};

class Widget;
enum class FocusIntent : uint8_t;

class FocusManager {
public:
    void Build(Widget* root);
    void Clear();
    void MarkDirty() { dirty_ = true; }
    void RebuildIfNeeded(Widget* root);
    bool HandleDirectionalKey(KeyCode key);

    bool SetCurrentById(uint32_t id);

    Widget* Current() const;

private:
    bool MoveSpatial(KeyCode key);
    void MoveLinear(int delta);
    bool SetCurrent(Widget* target);
    Widget* FindSpatialTarget(Widget* current, KeyCode key) const;
    void Traverse(Widget* node);
    Widget* ResolveById(uint32_t id) const;
    Widget* ResolveByIndex(int index) const;

    std::vector<uint32_t> focus_ids_{};
    std::unordered_map<uint32_t, int> focus_index_by_id_{};
    std::unordered_map<uint32_t, Widget*> id_to_widget_{};
    int current_index_ = -1;
    bool wrap_ = true;
    bool dirty_ = true;
    Widget* root_ = nullptr;
};

class InputDispatcher {
public:
    void Dispatch(const InputEvent& event, Widget* root, FocusManager& focus);

private:
    bool DispatchPath(const std::vector<Widget*>& path, const InputEvent& event);
    bool FindPathTo(Widget* node, Widget* target, std::vector<Widget*>& path);
    bool FindPathHit(Widget* node, Point global, std::vector<Widget*>& path);
};

} // namespace app_ui
