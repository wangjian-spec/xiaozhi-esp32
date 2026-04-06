#pragma once

#include <string>

#include "eteacher/apps/et_client/et_client_state.h"

class EtClientService {
public:
    bool InitializeState(EtClientState& state, std::string& error);
    bool EnsureDeviceIdentity(EtClientState& state, std::string& error);
    bool EnsureDeviceRegistered(EtClientState& state, std::string& error);
    bool EnsureDeviceAuthed(EtClientState& state, std::string& error);
    bool SendLoginCode(EtClientState& state, const std::string& phone, std::string& error);
    bool SendRegisterCode(EtClientState& state, const std::string& phone, std::string& error);
    bool RegisterUser(EtClientState& state,
                      const std::string& phone,
                      const std::string& code,
                      const std::string& password,
                      std::string& error);
    bool LoginUser(EtClientState& state,
                   const std::string& phone,
                   const std::string& code,
                   const std::string& password,
                   std::string& error);
    bool SendCodeSmart(EtClientState& state, const std::string& phone, std::string& error);
    bool LoginOrAutoRegister(EtClientState& state,
                             const std::string& phone,
                             const std::string& code,
                             const std::string& password,
                             std::string& error);
    bool Logout(EtClientState& state, std::string& error);
    void ClearUserSession(EtClientState& state);
};
