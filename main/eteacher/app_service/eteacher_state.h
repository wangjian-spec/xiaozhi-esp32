#ifndef _ETEACHER_STATE_H_
#define _ETEACHER_STATE_H_

enum EteacherState {
    kEteacherStateUnknown,
    kEteacherStateStarting,
    kEteacherStateWifiConfiguring,
    kEteacherStateIdle,
    kEteacherStateConnecting,
    kEteacherStateListening,
    kEteacherStateSpeaking,
    kEteacherStateUpgrading,
    kEteacherStateActivating,
    kEteacherStateAudioTesting,
    kEteacherStateFatalError
};

#endif // _ETEACHER_STATE_H_ 