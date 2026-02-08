#include "dictionary_ui.h"


namespace app_ui::generated::dictionary {

// Scene page_main

static const app_ui::desc::WidgetDesc kScene_page_main_widgets[] = {
    { 0x81EE7077u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
};

static const app_ui::desc::SceneDesc kScene_page_main = {
    "page_main",
    0x81EE7077u,
    kScene_page_main_widgets,
    sizeof(kScene_page_main_widgets) / sizeof(kScene_page_main_widgets[0])
};

// Scene page_f3a5

static const app_ui::desc::WidgetDesc kScene_page_f3a5_widgets[] = {
    { 0x5E9C722Fu, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
};

static const app_ui::desc::SceneDesc kScene_page_f3a5 = {
    "page_f3a5",
    0x5E9C722Fu,
    kScene_page_f3a5_widgets,
    sizeof(kScene_page_f3a5_widgets) / sizeof(kScene_page_f3a5_widgets[0])
};

// Scene page_9784

static const app_ui::desc::WidgetDesc kScene_page_9784_widgets[] = {
    { 0x11A3DDDAu, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
};

static const app_ui::desc::SceneDesc kScene_page_9784 = {
    "page_9784",
    0x11A3DDDAu,
    kScene_page_9784_widgets,
    sizeof(kScene_page_9784_widgets) / sizeof(kScene_page_9784_widgets[0])
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

static const app_ui::desc::TextDesc kText_public_dialog_21c226 = {
    "Dialog",
    0x7C50F740u
};

static const app_ui::desc::WidgetDesc kScene_public_widgets[] = {
    { 0xDE440657u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0x3D8F28B1u, 0xDE440657u, app_ui::WidgetType::BottomBar, {135, 243, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_bottombar_c9bb2e },
    { 0x2ED473F2u, 0xDE440657u, app_ui::WidgetType::TopBar, {130, 23, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_topbar_f8a80a },
    { 0x5AF83D98u, 0xDE440657u, app_ui::WidgetType::SoftKeyboard, {136, 133, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0xDC17E52Eu, 0xDE440657u, app_ui::WidgetType::Dialog, {32, 133, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_dialog_21c226 },
};

static const app_ui::desc::SceneDesc kScene_public = {
    "public",
    0xDE440657u,
    kScene_public_widgets,
    sizeof(kScene_public_widgets) / sizeof(kScene_public_widgets[0])
};

static const app_ui::desc::SceneDesc kScenes[] = {
    kScene_page_main,
    kScene_page_f3a5,
    kScene_page_9784,
};

const app_ui::desc::UiDesc kUi = {
    kScenes,
    sizeof(kScenes) / sizeof(kScenes[0]),
    &kScene_public
};

} // namespace app_ui::generated
