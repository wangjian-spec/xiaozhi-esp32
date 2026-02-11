#include "word_practice_ui.h"


namespace app_ui::generated::word_practice {

// Scene page_main

static const app_ui::desc::TextDesc kText_page_main_label_de5914 = {
    "Label",
    0x5167BA30u
};

static const app_ui::desc::ProgressDesc kProgress_page_main_progress_82ab52 = {
    "Progress",
    0xE389052Bu,
    0
};

static const app_ui::desc::TextDesc kText_page_main_textarea_326055 = {
    "TextArea",
    0xA0B573FBu
};

static const app_ui::desc::WidgetDesc kScene_page_main_widgets[] = {
    { 0x81EE7077u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0xFC8E16C2u, 0x81EE7077u, app_ui::WidgetType::Label, {249, 124, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_main_label_de5914 },
    { 0xE3F59831u, 0x81EE7077u, app_ui::WidgetType::Progress, {97, 147, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kProgress_page_main_progress_82ab52 },
    { 0xF2E24675u, 0x81EE7077u, app_ui::WidgetType::TextArea, {135, 52, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_main_textarea_326055 },
};

static const app_ui::desc::SceneDesc kScene_page_main = {
    "page_main",
    0x81EE7077u,
    kScene_page_main_widgets,
    sizeof(kScene_page_main_widgets) / sizeof(kScene_page_main_widgets[0])
};

// Scene page_dc46

static const app_ui::desc::WidgetDesc kScene_page_dc46_widgets[] = {
    { 0xEFC7035Du, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
};

static const app_ui::desc::SceneDesc kScene_page_dc46 = {
    "page_dc46",
    0xEFC7035Du,
    kScene_page_dc46_widgets,
    sizeof(kScene_page_dc46_widgets) / sizeof(kScene_page_dc46_widgets[0])
};

// Scene public

static const app_ui::desc::TextDesc kText_public_dialog_e8ca72 = {
    "Dialog",
    0x86D1BB98u
};

static const app_ui::desc::TextDesc kText_public_button_041006 = {
    "Button",
    0xC613FA77u
};

static const app_ui::desc::TextDesc kText_public_image_21643f = {
    "Image",
    0x66179CE3u
};

static const app_ui::desc::WidgetDesc kScene_public_widgets[] = {
    { 0xDE440657u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0xCD15F8C6u, 0xDE440657u, app_ui::WidgetType::Dialog, {135, 62, 200, 200}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_public_dialog_e8ca72 },
    { 0x69AE4C19u, 0xCD15F8C6u, app_ui::WidgetType::Button, {185, 113, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_public_button_041006 },
    { 0xE473D565u, 0xDE440657u, app_ui::WidgetType::Image, {48, 160, 80, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_public_image_21643f },
};

static const app_ui::desc::SceneDesc kScene_public = {
    "public",
    0xDE440657u,
    kScene_public_widgets,
    sizeof(kScene_public_widgets) / sizeof(kScene_public_widgets[0])
};

static const app_ui::desc::SceneDesc kScenes[] = {
    kScene_page_main,
    kScene_page_dc46,
};

const app_ui::desc::UiDesc kUi = {
    kScenes,
    sizeof(kScenes) / sizeof(kScenes[0]),
    &kScene_public
};

} // namespace app_ui::generated
