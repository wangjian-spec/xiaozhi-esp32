#pragma once

#include <string>

#include "eteacher/apps/et_client/et_client_state.h"

class EtClientService {
public:
    bool InitializeState(EtClientState& state, std::string& error);
    bool EnsureDeviceIdentity(EtClientState& state, std::string& error);
    void ClearUserSession(EtClientState& state);
};
