#include "calendar_schedule_ui.h"


namespace app_ui::generated::calendar_schedule {

// Scene page_main

static const app_ui::desc::TextDesc kText_page_main_frame = {
    "",
    0x00000000u
};

static const app_ui::desc::WidgetDesc kScene_page_main_widgets[] = {
    { 0x81EE7077u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0xD20A71A6u, 0x81EE7077u, app_ui::WidgetType::Frame, {0, 44, 400, 240}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_main_frame },
};

static const app_ui::desc::SceneDesc kScene_page_main = {
    "page_main",
    0x81EE7077u,
    kScene_page_main_widgets,
    sizeof(kScene_page_main_widgets) / sizeof(kScene_page_main_widgets[0])
};

// Scene page_d3f0

static const app_ui::desc::TextDesc kText_page_d3f0_listview_task = {
    "",
    0x00000000u
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_radio_today = {
    "\u5F53\u65E5",
    0xF4348AC4u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_radio_ThisWeek = {
    "\u672C\u5468",
    0xE9CB72D3u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_radio_all = {
    "\u5168\u90E8",
    0x5587D5FEu,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_checkbox_todo = {
    "\u672A\u5B8C\u6210",
    0x7B353B77u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_checkbox_done = {
    "\u5DF2\u5B8C\u6210",
    0x5B2736D3u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_checkbox_delete = {
    "\u5DF2\u5220\u9664",
    0xDC608314u,
    false
};

static const app_ui::desc::WidgetDesc kScene_page_d3f0_widgets[] = {
    { 0x65637D99u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0xBCABB678u, 0x65637D99u, app_ui::WidgetType::ListView, {0, 60, 400, 224}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_d3f0_listview_task },
    { 0x5B96521Au, 0x65637D99u, app_ui::WidgetType::Radio, {280, 42, 40, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_radio_today },
    { 0x7EC7C59Du, 0x65637D99u, app_ui::WidgetType::Radio, {340, 42, 40, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_radio_ThisWeek },
    { 0xB144D128u, 0x65637D99u, app_ui::WidgetType::Radio, {220, 42, 40, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_radio_all },
    { 0x0C430A7Du, 0x65637D99u, app_ui::WidgetType::Checkbox, {10, 42, 40, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_checkbox_todo },
    { 0x00DD8371u, 0x65637D99u, app_ui::WidgetType::Checkbox, {70, 42, 40, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_checkbox_done },
    { 0x542B7F4Au, 0x65637D99u, app_ui::WidgetType::Checkbox, {130, 42, 40, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_checkbox_delete },
};

static const app_ui::desc::SceneDesc kScene_page_d3f0 = {
    "page_d3f0",
    0x65637D99u,
    kScene_page_d3f0_widgets,
    sizeof(kScene_page_d3f0_widgets) / sizeof(kScene_page_d3f0_widgets[0])
};

// Scene page_1e8a

static const app_ui::desc::TextDesc kText_page_1e8a_frame_alarmclock = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_1e8a_frame_clock = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_1e8a_frame_timer = {
    "",
    0x00000000u
};

static const app_ui::desc::WidgetDesc kScene_page_1e8a_widgets[] = {
    { 0xBA47F103u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0x61FC8D1Eu, 0xBA47F103u, app_ui::WidgetType::Frame, {0, 60, 200, 224}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_frame_alarmclock },
    { 0xDDB184DFu, 0xBA47F103u, app_ui::WidgetType::Frame, {200, 60, 200, 112}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_frame_clock },
    { 0x65F69F00u, 0xBA47F103u, app_ui::WidgetType::Frame, {200, 172, 200, 112}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_frame_timer },
};

static const app_ui::desc::SceneDesc kScene_page_1e8a = {
    "page_1e8a",
    0xBA47F103u,
    kScene_page_1e8a_widgets,
    sizeof(kScene_page_1e8a_widgets) / sizeof(kScene_page_1e8a_widgets[0])
};

// Scene page_89db

static const app_ui::desc::TextDesc kText_page_89db_frame_1 = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_89db_frame_2 = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_89db_frame_3 = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_89db_frame_4 = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_89db_frame_5 = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_89db_frame_6 = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_89db_frame_7 = {
    "",
    0x00000000u
};

static const app_ui::desc::WidgetDesc kScene_page_89db_widgets[] = {
    { 0x4FE19AF7u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0xC98B1BFEu, 0x4FE19AF7u, app_ui::WidgetType::Frame, {4, 40, 56, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_89db_frame_1 },
    { 0xC88B1A6Bu, 0x4FE19AF7u, app_ui::WidgetType::Frame, {60, 40, 56, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_89db_frame_2 },
    { 0xC78B18D8u, 0x4FE19AF7u, app_ui::WidgetType::Frame, {116, 40, 56, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_89db_frame_3 },
    { 0xCE8B23DDu, 0x4FE19AF7u, app_ui::WidgetType::Frame, {172, 40, 56, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_89db_frame_4 },
    { 0xCD8B224Au, 0x4FE19AF7u, app_ui::WidgetType::Frame, {228, 40, 56, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_89db_frame_5 },
    { 0xCC8B20B7u, 0x4FE19AF7u, app_ui::WidgetType::Frame, {284, 40, 56, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_89db_frame_6 },
    { 0xCB8B1F24u, 0x4FE19AF7u, app_ui::WidgetType::Frame, {340, 40, 56, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_89db_frame_7 },
};

static const app_ui::desc::SceneDesc kScene_page_89db = {
    "page_89db",
    0x4FE19AF7u,
    kScene_page_89db_widgets,
    sizeof(kScene_page_89db_widgets) / sizeof(kScene_page_89db_widgets[0])
};

// Scene page_9b37

static const app_ui::desc::TextDesc kText_page_9b37_button_PhoneConnect = {
    "\u624B\u673A\u8FDE\u63A5\u8BBE\u7F6E",
    0xAE390A5Eu
};

static const app_ui::desc::TextDesc kText_page_9b37_image_894102 = {
    "Image",
    0xB5F1EAD3u
};

static const app_ui::desc::TextDesc kText_page_9b37_label_e0038a = {
    "\u7F51\u5740",
    0x4F8FE09Bu
};

static const app_ui::desc::TextDesc kText_page_9b37_textarea_572403 = {
    "ASR",
    0xE44F6AC5u
};

static const app_ui::desc::WidgetDesc kScene_page_9b37_widgets[] = {
    { 0x194AE4CFu, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0x86570FC8u, 0x194AE4CFu, app_ui::WidgetType::Button, {41, 63, 80, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_9b37_button_PhoneConnect },
    { 0xA1D5D681u, 0x194AE4CFu, app_ui::WidgetType::Image, {5, 87, 150, 150}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_9b37_image_894102 },
    { 0xF31EF511u, 0x194AE4CFu, app_ui::WidgetType::Label, {35, 246, 50, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_9b37_label_e0038a },
    { 0x1D40B35Fu, 0x194AE4CFu, app_ui::WidgetType::TextArea, {179, 90, 200, 100}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_9b37_textarea_572403 },
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
    "\"\u65E5\u5386\"\"\u5F85\u529E\u4E8B\u9879\"\"\u95F9\u949F\u8BA1\u65F6\"\"\u65E5\u7A0B\u8868\"\"\u65E5\u7A0B\u8BBE\u7F6E\"",
    0xC1E6DB0Eu
};

static const app_ui::desc::TextDesc kText_public_dialog_alert = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_public_label_alert = {
    "\u786E\u8BA4\u64CD\u4F5C\u63D0\u793A\u6587\u5B57",
    0xBEFC2D8Au
};

static const app_ui::desc::TextDesc kText_public_button_submit = {
    "\u786E\u5B9A",
    0xA1F59E1Cu
};

static const app_ui::desc::TextDesc kText_public_button_cancle = {
    "\u53D6\u6D88",
    0xD757562Cu
};

static const app_ui::desc::WidgetDesc kScene_public_widgets[] = {
    { 0xDE440657u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0xCCF6B1E4u, 0xDE440657u, app_ui::WidgetType::TopBar, {0, 0, 400, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_public_topbar_408adc },
    { 0x80DAA8ADu, 0xDE440657u, app_ui::WidgetType::BottomBar, {0, 284, 400, 16}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_public_bottombar_aabb85 },
    { 0xA8EC2EDCu, 0xDE440657u, app_ui::WidgetType::TabView, {0, 20, 400, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 10, &kText_public_tabview_439e98 },
    { 0xAE9E9A20u, 0xDE440657u, app_ui::WidgetType::Dialog, {90, 108, 220, 120}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 20, &kText_public_dialog_alert },
    { 0x2C695914u, 0xAE9E9A20u, app_ui::WidgetType::Label, {136, 143, 80, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 20, &kText_public_label_alert },
    { 0xD17B7AB2u, 0xAE9E9A20u, app_ui::WidgetType::Button, {123, 191, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 20, &kText_public_button_submit },
    { 0x087A890Eu, 0xAE9E9A20u, app_ui::WidgetType::Button, {221, 187, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 20, &kText_public_button_cancle },
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

const app_ui::desc::UiDesc kUi = {
    kScenes,
    sizeof(kScenes) / sizeof(kScenes[0]),
    &kScene_public
};

} // namespace app_ui::generated
