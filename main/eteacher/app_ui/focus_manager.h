#pragma once

#include <vector>

namespace app_ui {

class Widget;

class FocusManager {
public:
    void Build(Widget* root);
    void MoveUp();
    void MoveDown();
    void MoveLeft();
    void MoveRight();

    Widget* Current() const;

    void SetWrap(bool wrap);

private:
    void Move(int delta);
    void Traverse(Widget* node);

    std::vector<Widget*> focusables_{};
    int current_index_ = -1;
    bool wrap_ = true;
};

} // namespace app_ui
