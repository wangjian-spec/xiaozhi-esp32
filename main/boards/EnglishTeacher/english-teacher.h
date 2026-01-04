#pragma once

#include "wifi_board.h"
#include "button.h"
#include "custom_epd_display.h"
#include "custom_sd_fat.h"

class EnglishTeacherBoard : public WifiBoard {
private:
    CustomEpdDisplay display_;
    CustomSdFat sd_;

    // EnglishTeacher 的全部物理按键直接作为成员对象持有：
    // - 初始化更直观（无需 ButtonManager 中转）
    // - 回调绑定集中在 InitializeButtons()，便于维护
    Button up_button_;
    Button left_button_;
    Button down_button_;
    Button right_button_;
    Button boot_button_;        // 复用 A
    Button touch_button_;       // 复用 B
    Button c_button_;
    Button d_button_;
    Button select_button_;
    Button start_button_;
    Button volume_up_button_;
    Button volume_down_button_;

    void InitializeArduinoAndSharedSpi();
    void InitializeSdCard();
    void InitializeEpd();
    void InitializeButtons();
    void InitializeTools();

public:
    EnglishTeacherBoard();

    static EnglishTeacherBoard& GetInstance() {
        return static_cast<EnglishTeacherBoard&>(Board::GetInstance());
    }

    Led* GetLed() override;
    AudioCodec* GetAudioCodec() override;
    CustomEpdDisplay* GetDisplay() override;
    CustomSdFat* GetSd();
};
