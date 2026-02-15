#include "device_setting_ui.h"


namespace app_ui::generated::device_setting {

// Scene page_main

static const app_ui::desc::TextDesc kText_page_main_button_scan = {
    "\u626B\u63CFWIFI",
    0x3A0565ADu
};

static const app_ui::desc::TextDesc kText_page_main_frame_right = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_main_frame_left = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_page_main_listview_scan = {
    "\u626B\u63CF\u7F51\u7EDC\u5217\u8868",
    0x2CA2B5A2u
};

static const app_ui::desc::TextDesc kText_page_main_listview_saved = {
    "\u5DF2\u4FDD\u5B58\u7F51\u7EDC\u5217\u8868",
    0x478A082Eu
};

static const app_ui::desc::TextDesc kText_page_main_label_alert = {
    "\u786E\u8BA4\u64CD\u4F5C\u63D0\u793A\u6587\u5B57",
    0xBEFC2D8Au
};

static const app_ui::desc::TextDesc kText_page_main_label_title = {
    "\u5DF2\u4FDD\u5B58\u7F51\u7EDC\u5217\u8868 - \u8FDE\u63A5(C)/\u5220\u9664(D)\u7F51\u7EDC",
    0x93BBAC2Au
};

static const app_ui::desc::WidgetDesc kScene_page_main_widgets[] = {
    { 0x81EE7077u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0xF5EFF05Fu, 0x81EE7077u, app_ui::WidgetType::Button, {20, 45, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 2, &kText_page_main_button_scan },
    { 0xE5461FCFu, 0x81EE7077u, app_ui::WidgetType::Frame, {200, 40, 200, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_main_frame_right },
    { 0x7B2D6EE2u, 0x81EE7077u, app_ui::WidgetType::Frame, {0, 40, 200, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_main_frame_left },
    { 0x3EE34E54u, 0x81EE7077u, app_ui::WidgetType::ListView, {5, 70, 190, 210}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_main_listview_scan },
    { 0xBAB5B4F8u, 0x81EE7077u, app_ui::WidgetType::ListView, {205, 70, 190, 210}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_main_listview_saved },
    { 0x2C695914u, 0x81EE7077u, app_ui::WidgetType::Label, {93, 45, 90, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_main_label_alert },
    { 0x5A54E4B0u, 0x81EE7077u, app_ui::WidgetType::Label, {205, 45, 180, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_main_label_title },
};

static const app_ui::desc::SceneDesc kScene_page_main = {
    "page_main",
    0x81EE7077u,
    kScene_page_main_widgets,
    sizeof(kScene_page_main_widgets) / sizeof(kScene_page_main_widgets[0])
};

// Scene page_d3f0

static const app_ui::desc::TextDesc kText_page_d3f0_label_language = {
    "\u8BED\u8A00\u9009\u62E9",
    0x81B55C96u
};

static const app_ui::desc::TextDesc kText_page_d3f0_label_speed = {
    "\u8BED\u901F\u8BBE\u7F6E",
    0x6585DCC7u
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_radio_65b136 = {
    "\u4E2D\u6587",
    0x87E785DEu,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_radio_00bb98 = {
    "\u82F1\u6587",
    0xA4B56A52u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_radio_0689d6 = {
    "\u4E2D\u901F",
    0xB3F3E064u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_radio_847f39 = {
    "\u6162\u901F",
    0x998CAE3Eu,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_d3f0_radio_b5ed3e = {
    "\u5FEB\u901F",
    0x4C1D79A7u,
    false
};

static const app_ui::desc::WidgetDesc kScene_page_d3f0_widgets[] = {
    { 0x65637D99u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0x5447BDE4u, 0x65637D99u, app_ui::WidgetType::Label, {7, 59, 100, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_d3f0_label_language },
    { 0x12BE0AB9u, 0x65637D99u, app_ui::WidgetType::Label, {7, 115, 100, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_d3f0_label_speed },
    { 0xE2EF915Cu, 0x65637D99u, app_ui::WidgetType::Radio, {134, 59, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_radio_65b136 },
    { 0x41E59F28u, 0x65637D99u, app_ui::WidgetType::Radio, {225, 59, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_radio_00bb98 },
    { 0x83CE2C32u, 0x65637D99u, app_ui::WidgetType::Radio, {225, 115, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_radio_0689d6 },
    { 0x50D4C0F8u, 0x65637D99u, app_ui::WidgetType::Radio, {134, 115, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_radio_847f39 },
    { 0xB3CFECE5u, 0x65637D99u, app_ui::WidgetType::Radio, {317, 115, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_d3f0_radio_b5ed3e },
};

static const app_ui::desc::SceneDesc kScene_page_d3f0 = {
    "page_d3f0",
    0x65637D99u,
    kScene_page_d3f0_widgets,
    sizeof(kScene_page_d3f0_widgets) / sizeof(kScene_page_d3f0_widgets[0])
};

// Scene page_1e8a

static const app_ui::desc::TextDesc kText_page_1e8a_label_290f0d = {
    "\u5F53\u524D\u7528\u6237\u540D\u79F0",
    0x3DD39313u
};

static const app_ui::desc::TextDesc kText_page_1e8a_button_ed0b73 = {
    "\u65B0\u5EFA\u7528\u6237",
    0x327392C1u
};

static const app_ui::desc::TextDesc kText_page_1e8a_button_480cdd = {
    "\u5207\u6362\u7528\u6237",
    0x5E5AAD99u
};

static const app_ui::desc::TextDesc kText_page_1e8a_listview_73f87f = {
    "\u6240\u6709\u7528\u6237\u5217\u8868",
    0xC0E9ACA6u
};

static const app_ui::desc::TextDesc kText_page_1e8a_label_a7787b = {
    "\u5B66\u4E60\u8FDB\u5EA6",
    0x06DA4F06u
};

static const app_ui::desc::TextDesc kText_page_1e8a_frame_dd16a9 = {
    "Frame",
    0x0E1DF90Cu
};

static const app_ui::desc::TextDesc kText_page_1e8a_frame_91b350 = {
    "Frame",
    0x0E1DF90Cu
};

static const app_ui::desc::TextDesc kText_page_1e8a_label_667a92 = {
    "\u638C\u63E1\u5355\u8BCD\u6570\u91CF",
    0x1F6B9A4Fu
};

static const app_ui::desc::TextDesc kText_page_1e8a_label_643532 = {
    "\u5B8C\u6210\u5BF9\u8BDD",
    0xA353B657u
};

static const app_ui::desc::TextDesc kText_page_1e8a_label_eb4a64 = {
    "\u82F1\u8BED\u6C34\u5E73",
    0x1998E9E6u
};

static const app_ui::desc::WidgetDesc kScene_page_1e8a_widgets[] = {
    { 0xBA47F103u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0x47A9CC25u, 0xBA47F103u, app_ui::WidgetType::Label, {21, 60, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_label_290f0d },
    { 0x1815548Bu, 0xBA47F103u, app_ui::WidgetType::Button, {22, 101, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_1e8a_button_ed0b73 },
    { 0x0128D6A3u, 0xBA47F103u, app_ui::WidgetType::Button, {90, 101, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_1e8a_button_480cdd },
    { 0xFD0E68F8u, 0xBA47F103u, app_ui::WidgetType::ListView, {16, 142, 150, 130}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kText_page_1e8a_listview_73f87f },
    { 0x4BA45650u, 0xBA47F103u, app_ui::WidgetType::Label, {223, 54, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_label_a7787b },
    { 0x54827056u, 0xBA47F103u, app_ui::WidgetType::Frame, {5, 40, 180, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_frame_dd16a9 },
    { 0x9AEA792Fu, 0xBA47F103u, app_ui::WidgetType::Frame, {209, 40, 180, 244}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_frame_91b350 },
    { 0x113D6751u, 0xBA47F103u, app_ui::WidgetType::Label, {223, 95, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_label_667a92 },
    { 0x80F9AF09u, 0xBA47F103u, app_ui::WidgetType::Label, {223, 136, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_label_643532 },
    { 0x511A6020u, 0xBA47F103u, app_ui::WidgetType::Label, {223, 177, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_1e8a_label_eb4a64 },
};

static const app_ui::desc::SceneDesc kScene_page_1e8a = {
    "page_1e8a",
    0xBA47F103u,
    kScene_page_1e8a_widgets,
    sizeof(kScene_page_1e8a_widgets) / sizeof(kScene_page_1e8a_widgets[0])
};

// Scene page_89db

static const app_ui::desc::TextDesc kText_page_89db_label_688a8f = {
    "Label",
    0xF14C187Bu
};

static const app_ui::desc::WidgetDesc kScene_page_89db_widgets[] = {
    { 0x4FE19AF7u, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0x2735CB55u, 0x4FE19AF7u, app_ui::WidgetType::Label, {13, 84, 100, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_89db_label_688a8f },
};

static const app_ui::desc::SceneDesc kScene_page_89db = {
    "page_89db",
    0x4FE19AF7u,
    kScene_page_89db_widgets,
    sizeof(kScene_page_89db_widgets) / sizeof(kScene_page_89db_widgets[0])
};

// Scene page_9b37

static const app_ui::desc::TextDesc kText_page_9b37_label_6a175c = {
    "\u81EA\u52A8\u5173\u673A\u65F6\u95F4",
    0x7317A8D5u
};

static const app_ui::desc::TextDesc kText_page_9b37_label_77b11d = {
    "\u5F85\u673A\u65F6\u95F4",
    0xD2F9BCB6u
};

static const app_ui::desc::CheckableDesc kCheckable_page_9b37_radio_f39191 = {
    "30\u5206\u949F",
    0x2EEA47C8u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_9b37_radio_43dde5 = {
    "10\u5206\u949F",
    0x24B7CC38u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_9b37_radio_52b717 = {
    "\u4ECE\u4E0D",
    0x4656841Bu,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_9b37_radio_ad67f9 = {
    "60\u5206\u949F",
    0x323E5028u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_9b37_radio_13a8af = {
    "60\u5206\u949F",
    0x323E5028u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_9b37_radio_374721 = {
    "10\u5206\u949F",
    0x24B7CC38u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_9b37_radio_482365 = {
    "30\u5206\u949F",
    0x2EEA47C8u,
    false
};

static const app_ui::desc::CheckableDesc kCheckable_page_9b37_radio_5f557e = {
    "\u4ECE\u4E0D",
    0x4656841Bu,
    false
};

static const app_ui::desc::TextDesc kText_page_9b37_label_8b983f = {
    "MAC\u5730\u5740",
    0xF3719812u
};

static const app_ui::desc::TextDesc kText_page_9b37_label_b54ae9 = {
    "\u56FA\u4EF6\u7248\u672C\u53F7",
    0x42DB8818u
};

static const app_ui::desc::TextDesc kText_page_9b37_label_7bdd5d = {
    "\u8BBE\u5907\u578B\u53F7",
    0xA3863140u
};

static const app_ui::desc::WidgetDesc kScene_page_9b37_widgets[] = {
    { 0x194AE4CFu, 0x00000000u, app_ui::WidgetType::Container, {0, 0, 400, 300}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, nullptr },
    { 0xD0737403u, 0x194AE4CFu, app_ui::WidgetType::Label, {19, 66, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_9b37_label_6a175c },
    { 0x739B280Cu, 0x194AE4CFu, app_ui::WidgetType::Label, {20, 134, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_9b37_label_77b11d },
    { 0xB2C80E9Eu, 0x194AE4CFu, app_ui::WidgetType::Radio, {271, 125, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_9b37_radio_f39191 },
    { 0x3B42111Au, 0x194AE4CFu, app_ui::WidgetType::Radio, {204, 125, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_9b37_radio_43dde5 },
    { 0x59283879u, 0x194AE4CFu, app_ui::WidgetType::Radio, {138, 125, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_9b37_radio_52b717 },
    { 0x6BE1780Au, 0x194AE4CFu, app_ui::WidgetType::Radio, {338, 125, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_9b37_radio_ad67f9 },
    { 0x31C55FCFu, 0x194AE4CFu, app_ui::WidgetType::Radio, {338, 58, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_9b37_radio_13a8af },
    { 0x24A65E97u, 0x194AE4CFu, app_ui::WidgetType::Radio, {204, 58, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_9b37_radio_374721 },
    { 0x243D4DFBu, 0x194AE4CFu, app_ui::WidgetType::Radio, {271, 58, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_9b37_radio_482365 },
    { 0x1B771DCCu, 0x194AE4CFu, app_ui::WidgetType::Radio, {138, 58, 50, 40}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 0, &kCheckable_page_9b37_radio_5f557e },
    { 0xA5FFEB50u, 0x194AE4CFu, app_ui::WidgetType::Label, {270, 221, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_9b37_label_8b983f },
    { 0x708485AEu, 0x194AE4CFu, app_ui::WidgetType::Label, {149, 221, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_9b37_label_b54ae9 },
    { 0x09683472u, 0x194AE4CFu, app_ui::WidgetType::Label, {29, 221, 100, 25}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 0, &kText_page_9b37_label_7bdd5d },
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
    "\"WIFI\u8BBE\u7F6E\"\"\u8BED\u8A00\u8BBE\u7F6E\"\"\u7528\u6237\u8BBE\u7F6E\"\"\u5B66\u4E60\u504F\u597D\"\"\u8BBE\u5907\u4FE1\u606F\"",
    0xC1E6DB0Eu
};

static const app_ui::desc::TextDesc kText_public_dialog_inputpassword = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_public_label_ssid = {
    "SSID\u540D\u79F0\uFF1A",
    0x3CD81B37u
};

static const app_ui::desc::TextDesc kText_public_label_inputpassword = {
    "\u8F93\u5165\u5BC6\u7801\uFF1A",
    0x60B0E0C9u
};

static const app_ui::desc::TextDesc kText_public_textarea_password = {
    "",
    0x00000000u
};

static const app_ui::desc::TextDesc kText_public_label_statusinformation = {
    "\u72B6\u6001\u4FE1\u606F",
    0xCEE93EB8u
};

static const app_ui::desc::TextDesc kText_public_dialog_hint = {
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
    { 0xF2B83403u, 0xDE440657u, app_ui::WidgetType::SoftKeyboard, {50, 184, 300, 100}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 5, nullptr },
    { 0x847FB9EBu, 0xDE440657u, app_ui::WidgetType::Dialog, {90, 55, 220, 120}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 20, &kText_public_dialog_inputpassword },
    { 0x36D75C59u, 0x847FB9EBu, app_ui::WidgetType::Label, {100, 107, 200, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 20, &kText_public_label_ssid },
    { 0xE39189D7u, 0x847FB9EBu, app_ui::WidgetType::Label, {100, 144, 50, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 20, &kText_public_label_inputpassword },
    { 0x0F95CADDu, 0x847FB9EBu, app_ui::WidgetType::TextArea, {160, 144, 140, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 20, &kText_public_textarea_password },
    { 0xB55C8996u, 0x847FB9EBu, app_ui::WidgetType::Label, {99, 71, 200, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 20, &kText_public_label_statusinformation },
    { 0x2452ECFFu, 0xDE440657u, app_ui::WidgetType::Dialog, {90, 55, 220, 120}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 10, &kText_public_dialog_hint },
    { 0x2C695914u, 0x2452ECFFu, app_ui::WidgetType::Label, {147, 86, 80, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled, 15, &kText_public_label_alert },
    { 0xD17B7AB2u, 0x2452ECFFu, app_ui::WidgetType::Button, {120, 131, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 10, &kText_public_button_submit },
    { 0x087A890Eu, 0x2452ECFFu, app_ui::WidgetType::Button, {220, 131, 60, 20}, app_ui::kInvalidStyleId, app_ui::desc::kWidgetFlagVisible | app_ui::desc::kWidgetFlagEnabled | app_ui::desc::kWidgetFlagFocusable, 10, &kText_public_button_cancle },
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
