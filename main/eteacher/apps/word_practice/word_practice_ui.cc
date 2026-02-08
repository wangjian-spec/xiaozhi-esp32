#include "word_practice_ui.h"


namespace app_ui::generated::word_practice {

// Scene page_main

static const app_ui::desc::TextDesc kText_page_main_menu_94d40e = {
    "Menu",
    0xC69C69A3u
};

static const app_ui::desc::TextDesc kText_page_main_image_1ff9c6 = {
    "Image",
    0xA89AE422u
};

static const app_ui::desc::TextDesc kText_page_main_image_ba2b90 = {
    "Image",
    0xA89AE422u
};

static const app_ui::desc::TextDesc kText_page_main_button_8020a6 = {
    "Button",
    0x6CD8EFF9u
};

static const app_ui::desc::WidgetDesc kScene_page_main_widgets[] = {
    { 0x81EE7077u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0x8FBAE2C9u, 0x81EE7077u, app_ui::WidgetType::Menu, {147, 40, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_main_menu_94d40e },
    { 0x9BF6DC74u, 0x81EE7077u, app_ui::WidgetType::Image, {39, 114, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_main_image_1ff9c6 },
    { 0x7F409727u, 0x81EE7077u, app_ui::WidgetType::Image, {255, 109, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_main_image_ba2b90 },
    { 0xBBB6026Fu, 0x81EE7077u, app_ui::WidgetType::Button, {156, 206, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_main_button_8020a6 },
};

static const app_ui::desc::SceneDesc kScene_page_main = {
    "page_main",
    0x81EE7077u,
    kScene_page_main_widgets,
    sizeof(kScene_page_main_widgets) / sizeof(kScene_page_main_widgets[0])
};

// Scene page_71d5

static const app_ui::desc::TextDesc kText_page_71d5_button_fbe7b5 = {
    "Button",
    0x6CD8EFF9u
};

static const app_ui::desc::TextDesc kText_page_71d5_image_45f92b = {
    "Image",
    0xA89AE422u
};

static const app_ui::desc::TextDesc kText_page_71d5_image_cbeed7 = {
    "Image",
    0xA89AE422u
};

static const app_ui::desc::TextDesc kText_page_71d5_button_f2f198 = {
    "Button",
    0x6CD8EFF9u
};

static const app_ui::desc::TextDesc kText_page_71d5_textarea_251db0 = {
    "TextArea",
    0x77B85DD4u
};

static const app_ui::desc::WidgetDesc kScene_page_71d5_widgets[] = {
    { 0x27C4D191u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0x6D9B863Bu, 0x27C4D191u, app_ui::WidgetType::Button, {49, 66, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_71d5_button_fbe7b5 },
    { 0xEA9FB2D5u, 0x27C4D191u, app_ui::WidgetType::Image, {49, 124, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_71d5_image_45f92b },
    { 0xAC56DF81u, 0x27C4D191u, app_ui::WidgetType::Image, {265, 119, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_page_71d5_image_cbeed7 },
    { 0xB4B8F946u, 0x27C4D191u, app_ui::WidgetType::Button, {262, 66, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_71d5_button_f2f198 },
    { 0xF1FFC622u, 0x27C4D191u, app_ui::WidgetType::TextArea, {154, 93, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, &kText_page_71d5_textarea_251db0 },
};

static const app_ui::desc::SceneDesc kScene_page_71d5 = {
    "page_71d5",
    0x27C4D191u,
    kScene_page_71d5_widgets,
    sizeof(kScene_page_71d5_widgets) / sizeof(kScene_page_71d5_widgets[0])
};

// Scene public

static const app_ui::desc::TextDesc kText_public_topbar_f84f1f = {
    "TopBar",
    0x2A7EB5B3u
};

static const app_ui::desc::TextDesc kText_public_bottombar_86b332 = {
    "BottomBar",
    0x9D586CFAu
};

static const app_ui::desc::TextDesc kText_public_dialog_e8631b = {
    "Dialog",
    0x3C57451Du
};

static const app_ui::desc::WidgetDesc kScene_public_widgets[] = {
    { 0xDE440657u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0x5D44DF95u, 0xDE440657u, app_ui::WidgetType::TopBar, {134, 24, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_topbar_f84f1f },
    { 0x734CEB40u, 0xDE440657u, app_ui::WidgetType::BottomBar, {144, 239, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_bottombar_86b332 },
    { 0x9758B35Cu, 0xDE440657u, app_ui::WidgetType::SoftKeyboard, {192, 133, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, nullptr },
    { 0x2D70FB93u, 0xDE440657u, app_ui::WidgetType::Dialog, {85, 135, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, &kText_public_dialog_e8631b },
};

static const app_ui::desc::SceneDesc kScene_public = {
    "public",
    0xDE440657u,
    kScene_public_widgets,
    sizeof(kScene_public_widgets) / sizeof(kScene_public_widgets[0])
};

static const app_ui::desc::SceneDesc kScenes[] = {
    kScene_page_main,
    kScene_page_71d5,
};

const app_ui::desc::UiDesc kUi = {
    kScenes,
    sizeof(kScenes) / sizeof(kScenes[0]),
    &kScene_public
};

} // namespace app_ui::generated
