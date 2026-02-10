#pragma once

#include "eteacher/app_ui/ui_desc.h"


namespace app_ui::generated::device_setting {

// Scene page_main

static const app_ui::desc::TextDesc kText_page_main_button_1ddb53 = {
    "\u626B\u63CFWIFI",
    0xA7C5D077u
};

static const app_ui::desc::TextDesc kText_page_main_button_e1d4be = {
    "\u626B\u63CFWIFI",
    0xA7C5D077u
};

static const app_ui::desc::TextDesc kText_page_main_frame_1f9aac = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_main_frame_b83959 = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_main_listview_ce6360 = {
    "ListView",
    0x19445EBEu
};

static const app_ui::desc::TextDesc kText_page_main_listview_92ccf1 = {
    "ListView",
    0x19445EBEu
};

static const app_ui::desc::WidgetDesc kScene_page_main_widgets[] = {
    { 0x81EE7077u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0x54A65955u, 0x81EE7077u, app_ui::WidgetType::Button, {20, 45, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_main_button_1ddb53 },
    { 0x5686ECCDu, 0x81EE7077u, app_ui::WidgetType::Button, {105, 45, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_main_button_e1d4be },
    { 0x72C2EDD2u, 0x81EE7077u, app_ui::WidgetType::Frame, {200, 40, 200, 248}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_main_frame_1f9aac },
    { 0xFD09C1ABu, 0x81EE7077u, app_ui::WidgetType::Frame, {0, 40, 200, 248}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_main_frame_b83959 },
    { 0xA7DCD964u, 0x81EE7077u, app_ui::WidgetType::ListView, {11, 73, 180, 200}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_main_listview_ce6360 },
    { 0x0DB3CD15u, 0x81EE7077u, app_ui::WidgetType::ListView, {209, 73, 180, 200}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_main_listview_92ccf1 },
};

static const app_ui::desc::SceneDesc kScene_page_main = {
    "page_main",
    0x81EE7077u,
    kScene_page_main_widgets,
    sizeof(kScene_page_main_widgets) / sizeof(kScene_page_main_widgets[0])
};

// Scene page_d3f0

static const app_ui::desc::WidgetDesc kScene_page_d3f0_widgets[] = {
    { 0x65637D99u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
};

static const app_ui::desc::SceneDesc kScene_page_d3f0 = {
    "page_d3f0",
    0x65637D99u,
    kScene_page_d3f0_widgets,
    sizeof(kScene_page_d3f0_widgets) / sizeof(kScene_page_d3f0_widgets[0])
};

// Scene page_1e8a

static const app_ui::desc::WidgetDesc kScene_page_1e8a_widgets[] = {
    { 0xBA47F103u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
};

static const app_ui::desc::SceneDesc kScene_page_1e8a = {
    "page_1e8a",
    0xBA47F103u,
    kScene_page_1e8a_widgets,
    sizeof(kScene_page_1e8a_widgets) / sizeof(kScene_page_1e8a_widgets[0])
};

// Scene page_89db

static const app_ui::desc::WidgetDesc kScene_page_89db_widgets[] = {
    { 0x4FE19AF7u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
};

static const app_ui::desc::SceneDesc kScene_page_89db = {
    "page_89db",
    0x4FE19AF7u,
    kScene_page_89db_widgets,
    sizeof(kScene_page_89db_widgets) / sizeof(kScene_page_89db_widgets[0])
};

// Scene page_9b37

static const app_ui::desc::WidgetDesc kScene_page_9b37_widgets[] = {
    { 0x194AE4CFu, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
};

static const app_ui::desc::SceneDesc kScene_page_9b37 = {
    "page_9b37",
    0x194AE4CFu,
    kScene_page_9b37_widgets,
    sizeof(kScene_page_9b37_widgets) / sizeof(kScene_page_9b37_widgets[0])
};

// Scene public

static const app_ui::desc::TextDesc kText_public_topbar_408adc = {
    "TopBar",
    0xCA2049E2u
};

static const app_ui::desc::TextDesc kText_public_bottombar_aabb85 = {
    "BottomBar",
    0x00AF9363u
};

static const app_ui::desc::TextDesc kText_public_tabview_439e98 = {
    "\u201CWIFI\u8BBE\u7F6E\u201D\"\u8BED\u8A00\u8BBE\u7F6E\"\"\u7528\u6237\u8BBE\u7F6E\"\"\u5B66\u4E60\u504F\u597D\"\"\u7535\u6E90\u8BBE\u7F6E\"",
    0xC1E6DB0Eu
};

static const app_ui::desc::WidgetDesc kScene_public_widgets[] = {
    { 0xDE440657u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0xCCF6B1E4u, 0xDE440657u, app_ui::WidgetType::TopBar, {0, 0, 400, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_topbar_408adc },
    { 0x80DAA8ADu, 0xDE440657u, app_ui::WidgetType::BottomBar, {0, 284, 400, 16}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_bottombar_aabb85 },
    { 0xA8EC2EDCu, 0xDE440657u, app_ui::WidgetType::TabView, {0, 20, 400, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_tabview_439e98 },
};

static const app_ui::desc::SceneDesc kScene_public = {
    "public",
    0xDE440657u,
    kScene_public_widgets,
    sizeof(kScene_public_widgets) / sizeof(kScene_public_widgets[0])
};

static const app_ui::desc::SceneDesc kScenes[] = {
    kScene_page_main,
    kScene_page_d3f0,
    kScene_page_1e8a,
    kScene_page_89db,
    kScene_page_9b37,
};

static const app_ui::desc::UiDesc kUi = {
    kScenes,
    sizeof(kScenes) / sizeof(kScenes[0]),
    &kScene_public
};

} // namespace app_ui::generated
