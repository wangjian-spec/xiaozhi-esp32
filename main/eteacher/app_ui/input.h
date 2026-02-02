#pragma once

#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

#include "geometry.h"

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

struct InputEvent {
    InputType type = InputType::KeyDown;
    int key = 0;
    Point pos{};
    uint32_t timestamp = 0;
};

class InputQueue {
public:
    void Push(const InputEvent& e);
    bool TryPop(InputEvent& out);
    void Clear();
    size_t Size() const;

private:
    mutable std::mutex mutex_;
    std::queue<InputEvent> queue_;
};

class FocusManager;
class Widget;

class InputDispatcher {
public:
    void Dispatch(const InputEvent& event, Widget* root, FocusManager& focus);

private:
    bool DispatchPath(const std::vector<Widget*>& path, const InputEvent& event);
    bool FindPathTo(Widget* node, Widget* target, std::vector<Widget*>& path);
    bool FindPathHit(Widget* node, Point global, std::vector<Widget*>& path);
};

} // namespace app_ui
