#include "eteacher/apps/et_client/et_client_service.h"

#include "eteacher/apps/et_client/et_client_crypto.h"
#include "eteacher/apps/et_client/et_client_storage.h"

bool EtClientService::InitializeState(EtClientState& state, std::string& error) {
    EtClientStorage::LoadDeviceRecord(state.device);
    EtClientStorage::LoadSessionRecord(state.session);
    state.device_registered = state.device.registered;
    state.user_logged_in = !state.session.user_access_token.empty();
    return EnsureDeviceIdentity(state, error);
}

bool EtClientService::EnsureDeviceIdentity(EtClientState& state, std::string& error) {
    if (!state.device.private_key_b64.empty() &&
        !state.device.public_key_b64.empty() &&
        !state.device.device_id.empty()) {
        state.device_key_ready = true;
        return true;
    }

    EtKeyPair key_pair{};
    if (!EtClientCrypto::GenerateKeyPair(key_pair, error)) {
        state.last_error_code = "DEVICE_KEY_CREATE_FAILED";
        state.last_error_message = error;
        return false;
    }

    state.device.private_key_b64 = EtClientCrypto::Base64Encode(key_pair.private_key_seed);
    state.device.public_key_b64 = EtClientCrypto::Base64Encode(key_pair.public_key);
    state.device.device_id = EtClientCrypto::ComputeDeviceIdHex(key_pair.public_key);
    state.device.registered = false;
    state.device_key_ready = true;
    EtClientStorage::SaveDeviceRecord(state.device);
    state.last_success_message = "Device identity initialized";
    return true;
}

void EtClientService::ClearUserSession(EtClientState& state) {
    EtClientStorage::ClearUserSession();
    state.session.user_access_token.clear();
    state.session.user_refresh_token.clear();
    state.session.user_access_token_expire_epoch = 0;
    state.session.user_refresh_token_expire_epoch = 0;
    state.session.user_id.clear();
    state.session.phone.clear();
    state.session.username.clear();
    state.user_logged_in = false;
}
