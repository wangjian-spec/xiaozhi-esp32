#include "dictionary_ui.h"


namespace app_ui::generated::dictionary {

// Scene page_f3a5

static const app_ui::desc::TextDesc kText_page_f3a5_textarea_word = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_f3a5_label_1 = {
    "\u8F93\u5165\u5355\u8BCD\uFF1A",
    0xAA755847u
};

static const app_ui::desc::TextDesc kText_page_f3a5_label_status = {
    "\u63D0\u793A\uFF1A",
    0xFE0CFC16u
};

static const app_ui::desc::TextDesc kText_page_f3a5_frame_d3f67d = {
    "Frame",
    0x1E367B3Fu
};

static const app_ui::desc::WidgetDesc kScene_page_f3a5_widgets[] = {
    { 0x5E9C722Fu, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0x93B8C7C4u, 0x5E9C722Fu, app_ui::WidgetType::TextArea, {75, 33, 140, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 3, &kText_page_f3a5_textarea_word },
    { 0x91803295u, 0x5E9C722Fu, app_ui::WidgetType::Label, {9, 33, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 1, &kText_page_f3a5_label_1 },
    { 0x79252C88u, 0x5E9C722Fu, app_ui::WidgetType::Label, {223, 33, 160, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_f3a5_label_status },
    { 0x8B8E0425u, 0x5E9C722Fu, app_ui::WidgetType::Frame, {10, 64, 380, 220}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_f3a5_frame_d3f67d },
};

static const app_ui::desc::SceneDesc kScene_page_f3a5 = {
    "page_f3a5",
    0x5E9C722Fu,
    kScene_page_f3a5_widgets,
    sizeof(kScene_page_f3a5_widgets) / sizeof(kScene_page_f3a5_widgets[0])
};

// Scene public

static const app_ui::desc::TextDesc kText_public_bottombar_c9bb2e = {
    "BottomBar",
    0xC53F836Fu
};

static const app_ui::desc::TextDesc kText_public_topbar_f8a80a = {
    "TopBar",
    0x0370A2A8u
};

static const app_ui::desc::WidgetDesc kScene_public_widgets[] = {
    { 0xDE440657u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0x3D8F28B1u, 0xDE440657u, app_ui::WidgetType::BottomBar, {0, 284, 400, 16}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_public_bottombar_c9bb2e },
    { 0x2ED473F2u, 0xDE440657u, app_ui::WidgetType::TopBar, {0, 0, 400, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_public_topbar_f8a80a },
    { 0xD7467530u, 0xDE440657u, app_ui::WidgetType::SoftKeyboard, {50, 184, 300, 100}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
};

static const app_ui::desc::SceneDesc kScene_public = {
    "public",
    0xDE440657u,
    kScene_public_widgets,
    sizeof(kScene_public_widgets) / sizeof(kScene_public_widgets[0])
};

static const app_ui::desc::SceneDesc kScenes[] = {
    kScene_page_f3a5,
};

const app_ui::desc::UiDesc kUi = {
    kScenes,
    sizeof(kScenes) / sizeof(kScenes[0]),
    &kScene_public
};

} // namespace app_ui::generated
