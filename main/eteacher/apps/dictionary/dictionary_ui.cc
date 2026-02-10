#include "dictionary_ui.h"


namespace app_ui::generated::dictionary {

// Scene page_f3a5

static const app_ui::desc::TextDesc kText_page_f3a5_textarea_dd2b8b = {
    "TextArea",
    0x9F5F4224u
};

static const app_ui::desc::TextDesc kText_page_f3a5_label_8ba487 = {
    "\u8F93\u5165\u82F1\u6587\u5355\u8BCD",
    0x0AF8FFC6u
};

static const app_ui::desc::TextDesc kText_page_f3a5_frame_e86a40 = {
    "\u67E5\u8BE2\u7ED3\u679C\u663E\u793A\u533A",
    0x43698F4Du
};

static const app_ui::desc::WidgetDesc kScene_page_f3a5_widgets[] = {
    { 0x5E9C722Fu, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0x8F5C97CEu, 0x5E9C722Fu, app_ui::WidgetType::TextArea, {146, 30, 120, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_f3a5_textarea_dd2b8b },
    { 0x1DCE2948u, 0x5E9C722Fu, app_ui::WidgetType::Label, {44, 29, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_f3a5_label_8ba487 },
    { 0xFD7E1ABFu, 0x5E9C722Fu, app_ui::WidgetType::Frame, {10, 78, 380, 200}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_f3a5_frame_e86a40 },
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
    { 0xDE440657u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0x3D8F28B1u, 0xDE440657u, app_ui::WidgetType::BottomBar, {0, 284, 400, 16}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_bottombar_c9bb2e },
    { 0x2ED473F2u, 0xDE440657u, app_ui::WidgetType::TopBar, {0, 0, 400, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_topbar_f8a80a },
    { 0x5AF83D98u, 0xDE440657u, app_ui::WidgetType::SoftKeyboard, {50, 184, 300, 100}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
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
