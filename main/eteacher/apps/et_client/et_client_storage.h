#pragma once

#include "eteacher/apps/et_client/et_client_state.h"

class EtClientStorage {
public:
    static void LoadDeviceRecord(EtClientDeviceRecord& record);
    static void SaveDeviceRecord(const EtClientDeviceRecord& record);

    static void LoadSessionRecord(EtClientSessionRecord& record);
    static void SaveSessionRecord(const EtClientSessionRecord& record);
    static void ClearUserSession();
};
