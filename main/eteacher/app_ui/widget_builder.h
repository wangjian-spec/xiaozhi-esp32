#pragma once

#include <cstddef>
#include <memory>

#include "ui_desc.h"
#include "types.h"

namespace app_ui {

class Widget;
// 静态/资源构建接口（生产用）。
// 两个重载分别接受不同格式的编译期描述数据：
//
// 1) `BuildWidgetTree(const resource::WidgetInit* inits, size, root_id)`
//    - `inits`：指向 `resource::WidgetInit` 数组，通常由资源打包器生成的轻量初始化表。
//      每项包含 `id`/`parent_id`/`type`/`rect` 等基本字段，适合资源驱动的最小化初始化场景。
//    - `count`：数组长度。
//    - `root_id`：根控件的 id（0 表示无效）。
//    - 返回：拥有根 `Widget` 的 `std::unique_ptr<Widget>`；构建失败返回 `nullptr`。
//
// 2) `BuildWidgetTree(const desc::WidgetDesc* widgets, size, root_id)`
//    - `widgets`：指向 `desc::WidgetDesc` 数组，
//      包含更丰富的字段（`flags`、`style_id`、`specific` 指针等），用于完整的静态 UI 描述。
//    - `count` / `root_id`：同上。
//    - 返回：拥有根 `Widget` 的 `std::unique_ptr<Widget>`；构建失败返回 `nullptr`。
//
// 为什么有两个接口：两者兼容不同的生成/打包流程和数据格式。轻量表用于紧凑资源场景，
// 完整描述符用于需要样式和控件特定字段的生产路径。实现层会按 id/parent 关系组装树。
std::unique_ptr<Widget> BuildWidgetTree(const resource::WidgetInit* inits,
                                        size_t count,
                                        uint32_t root_id);

std::unique_ptr<Widget> BuildWidgetTree(const desc::WidgetDesc* widgets,
                                        size_t count,
                                        uint32_t root_id);

} // namespace app_ui
