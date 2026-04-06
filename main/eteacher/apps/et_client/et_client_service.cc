#include "eteacher/apps/et_client/et_client_service.h"

#include <algorithm>
#include <array>
#include <ctime>

#include <cJSON.h>

#include "eteacher/apps/et_client/et_client_crypto.h"
#include "eteacher/apps/et_client/et_client_http.h"
#include "eteacher/apps/et_client/et_client_protocol.h"
#include "eteacher/apps/et_client/et_client_storage.h"

namespace {

std::string TokenPrefix(const std::string& token) {
    return token.empty() ? std::string("--") : token.substr(0, std::min<size_t>(16, token.size()));
}

std::string BearerToken(const std::string& token) {
    if (token.empty()) {
        return {};
    }
    if (token.rfind("Bearer ", 0) == 0) {
        return token;
    }
    return "Bearer " + token;
}

std::string DefaultUsernameForPhone(const std::string& phone) {
    std::string digits;
    digits.reserve(phone.size());
    for (char c : phone) {
        if (c >= '0' && c <= '9') {
            digits.push_back(c);
        }
    }
    if (digits.size() > 6) {
        digits = digits.substr(digits.size() - 6);
    }
    if (digits.empty()) {
        digits = "user";
    }
    return "u_" + digits;
}

bool ParseJsonObject(const std::string& body, cJSON*& root, std::string& error) {
    root = cJSON_Parse(body.c_str());
    if (root == nullptr || !cJSON_IsObject(root)) {
        error = "response is not valid JSON object";
        if (root != nullptr) {
            cJSON_Delete(root);
            root = nullptr;
        }
        return false;
    }
    return true;
}

bool GetJsonString(cJSON* root, const char* key, std::string& out, std::string& error) {
    cJSON* item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsString(item)) {
        error = std::string("missing or invalid field: ") + key;
        return false;
    }
    out = item->valuestring;
    return true;
}

bool GetJsonInt(cJSON* root, const char* key, int& out, std::string& error) {
    cJSON* item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsNumber(item)) {
        error = std::string("missing or invalid field: ") + key;
        return false;
    }
    out = item->valueint;
    return true;
}

bool GetDeviceSeed(const EtClientState& state, std::array<uint8_t, 32>& seed, std::string& error) {
    if (state.device.private_key_b64.empty()) {
        error = "device private key is empty";
        return false;
    }
    return EtClientCrypto::Base64Decode32(state.device.private_key_b64, seed, error);
}

}  // namespace

bool EtClientService::InitializeState(EtClientState& state, std::string& error) {
    EtClientStorage::LoadDeviceRecord(state.device);
    EtClientStorage::LoadSessionRecord(state.session);
    state.device_registered = state.device.registered;
    const auto now = static_cast<int64_t>(std::time(nullptr));
    state.device_authed = !state.session.device_access_token.empty() &&
                          state.session.device_access_token_expire_epoch > now;
    state.user_logged_in = !state.session.user_access_token.empty() &&
                           state.session.user_access_token_expire_epoch > now;
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

bool EtClientService::EnsureDeviceRegistered(EtClientState& state, std::string& error) {
    if (!EnsureDeviceIdentity(state, error)) {
        return false;
    }
    if (state.device_registered) {
        return true;
    }

    cJSON* challenge_request = cJSON_CreateObject();
    cJSON_AddStringToObject(challenge_request, "device_pubkey", state.device.public_key_b64.c_str());
    EtHttpResult challenge_result =
        EtClientHttp::PostJson(et_client::kRegisterChallengePath, challenge_request);
    cJSON_Delete(challenge_request);
    if (!challenge_result.ok) {
        error = challenge_result.error_message;
        state.last_error_code = challenge_result.error_code;
        state.last_error_message = challenge_result.error_message;
        return false;
    }

    cJSON* challenge_root = nullptr;
    std::string challenge_id;
    std::string challenge;
    std::string remote_device_id;
    if (!ParseJsonObject(challenge_result.body, challenge_root, error) ||
        !GetJsonString(challenge_root, "device_id", remote_device_id, error) ||
        !GetJsonString(challenge_root, "challenge_id", challenge_id, error) ||
        !GetJsonString(challenge_root, "challenge", challenge, error)) {
        if (challenge_root != nullptr) {
            cJSON_Delete(challenge_root);
        }
        state.last_error_code = "REGISTER_RESPONSE_INVALID";
        state.last_error_message = error;
        return false;
    }
    cJSON_Delete(challenge_root);

    if (remote_device_id != state.device.device_id) {
        error = "remote device_id mismatch";
        state.last_error_code = "DEVICE_ID_MISMATCH";
        state.last_error_message = error;
        return false;
    }

    std::array<uint8_t, 32> seed{};
    if (!GetDeviceSeed(state, seed, error)) {
        state.last_error_code = "DEVICE_KEY_INVALID";
        state.last_error_message = error;
        return false;
    }

    std::array<uint8_t, 64> signature{};
    const std::string signing_message = et_client::MakeSigningMessage("register", challenge);
    if (!EtClientCrypto::Sign(seed,
                              std::span<const uint8_t>(
                                  reinterpret_cast<const uint8_t*>(signing_message.data()),
                                  signing_message.size()),
                              signature,
                              error)) {
        state.last_error_code = "SIGNATURE_FAILED";
        state.last_error_message = error;
        return false;
    }

    cJSON* verify_request = cJSON_CreateObject();
    cJSON_AddStringToObject(verify_request, "device_id", state.device.device_id.c_str());
    cJSON_AddStringToObject(verify_request, "challenge_id", challenge_id.c_str());
    const std::string signature_b64 = EtClientCrypto::Base64Encode(signature);
    cJSON_AddStringToObject(verify_request, "signature", signature_b64.c_str());
    EtHttpResult verify_result = EtClientHttp::PostJson(et_client::kRegisterVerifyPath, verify_request);
    cJSON_Delete(verify_request);
    if (!verify_result.ok) {
        error = verify_result.error_message;
        state.last_error_code = verify_result.error_code;
        state.last_error_message = verify_result.error_message;
        return false;
    }

    cJSON* verify_root = nullptr;
    std::string status;
    if (!ParseJsonObject(verify_result.body, verify_root, error) ||
        !GetJsonString(verify_root, "status", status, error)) {
        if (verify_root != nullptr) {
            cJSON_Delete(verify_root);
        }
        state.last_error_code = "REGISTER_VERIFY_INVALID";
        state.last_error_message = error;
        return false;
    }
    cJSON_Delete(verify_root);

    state.device.registered = true;
    state.device_registered = true;
    EtClientStorage::SaveDeviceRecord(state.device);
    state.last_error_code.clear();
    state.last_error_message.clear();
    state.last_success_message = "Device registered";
    return true;
}

bool EtClientService::EnsureDeviceAuthed(EtClientState& state, std::string& error) {
    const auto now = static_cast<int64_t>(std::time(nullptr));
    if (!state.session.device_access_token.empty() &&
        state.session.device_access_token_expire_epoch > now) {
        state.device_authed = true;
        return true;
    }

    if (!EnsureDeviceRegistered(state, error)) {
        return false;
    }

    cJSON* challenge_request = cJSON_CreateObject();
    cJSON_AddStringToObject(challenge_request, "device_id", state.device.device_id.c_str());
    EtHttpResult challenge_result =
        EtClientHttp::PostJson(et_client::kAuthChallengePath, challenge_request);
    cJSON_Delete(challenge_request);
    if (!challenge_result.ok) {
        error = challenge_result.error_message;
        state.last_error_code = challenge_result.error_code;
        state.last_error_message = challenge_result.error_message;
        return false;
    }

    cJSON* challenge_root = nullptr;
    std::string challenge_id;
    std::string challenge;
    if (!ParseJsonObject(challenge_result.body, challenge_root, error) ||
        !GetJsonString(challenge_root, "challenge_id", challenge_id, error) ||
        !GetJsonString(challenge_root, "challenge", challenge, error)) {
        if (challenge_root != nullptr) {
            cJSON_Delete(challenge_root);
        }
        state.last_error_code = "AUTH_RESPONSE_INVALID";
        state.last_error_message = error;
        return false;
    }
    cJSON_Delete(challenge_root);

    std::array<uint8_t, 32> seed{};
    if (!GetDeviceSeed(state, seed, error)) {
        state.last_error_code = "DEVICE_KEY_INVALID";
        state.last_error_message = error;
        return false;
    }

    std::array<uint8_t, 64> signature{};
    const std::string signing_message = et_client::MakeSigningMessage("auth", challenge);
    if (!EtClientCrypto::Sign(seed,
                              std::span<const uint8_t>(
                                  reinterpret_cast<const uint8_t*>(signing_message.data()),
                                  signing_message.size()),
                              signature,
                              error)) {
        state.last_error_code = "SIGNATURE_FAILED";
        state.last_error_message = error;
        return false;
    }

    cJSON* verify_request = cJSON_CreateObject();
    cJSON_AddStringToObject(verify_request, "device_id", state.device.device_id.c_str());
    cJSON_AddStringToObject(verify_request, "challenge_id", challenge_id.c_str());
    const std::string signature_b64 = EtClientCrypto::Base64Encode(signature);
    cJSON_AddStringToObject(verify_request, "signature", signature_b64.c_str());
    EtHttpResult verify_result = EtClientHttp::PostJson(et_client::kAuthVerifyPath, verify_request);
    cJSON_Delete(verify_request);
    if (!verify_result.ok) {
        error = verify_result.error_message;
        state.last_error_code = verify_result.error_code;
        state.last_error_message = verify_result.error_message;
        return false;
    }

    cJSON* verify_root = nullptr;
    std::string access_token;
    int expires_in = 0;
    if (!ParseJsonObject(verify_result.body, verify_root, error) ||
        !GetJsonString(verify_root, "access_token", access_token, error) ||
        !GetJsonInt(verify_root, "expires_in", expires_in, error)) {
        if (verify_root != nullptr) {
            cJSON_Delete(verify_root);
        }
        state.last_error_code = "AUTH_VERIFY_INVALID";
        state.last_error_message = error;
        return false;
    }
    cJSON_Delete(verify_root);

    state.session.device_access_token = access_token;
    state.session.device_access_token_expire_epoch = now + expires_in;
    EtClientStorage::SaveSessionRecord(state.session);
    state.device_authed = true;
    state.last_error_code.clear();
    state.last_error_message.clear();
    state.last_success_message = "Device authed: " + TokenPrefix(access_token);
    return true;
}

bool EtClientService::SendLoginCode(EtClientState& state, const std::string& phone, std::string& error) {
    if (!EnsureDeviceAuthed(state, error)) {
        return false;
    }

    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "phone", phone.c_str());
    EtHttpResult result = EtClientHttp::PostJson(et_client::kUserLoginCodeSendPath,
                                                 request,
                                                 BearerToken(state.session.device_access_token));
    cJSON_Delete(request);
    if (!result.ok) {
        error = result.error_message;
        state.last_error_code = result.error_code;
        state.last_error_message = result.error_message;
        return false;
    }

    state.last_error_code.clear();
    state.last_error_message.clear();
    state.last_success_message = "Login code sent: " + phone;
    return true;
}

bool EtClientService::SendRegisterCode(EtClientState& state, const std::string& phone, std::string& error) {
    if (!EnsureDeviceAuthed(state, error)) {
        return false;
    }

    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "phone", phone.c_str());
    EtHttpResult result = EtClientHttp::PostJson(et_client::kUserRegisterCodeSendPath,
                                                 request,
                                                 BearerToken(state.session.device_access_token));
    cJSON_Delete(request);
    if (!result.ok) {
        error = result.error_message;
        state.last_error_code = result.error_code;
        state.last_error_message = result.error_message;
        return false;
    }

    state.last_error_code.clear();
    state.last_error_message.clear();
    state.last_success_message = "Register code sent: " + phone;
    return true;
}

bool EtClientService::RegisterUser(EtClientState& state,
                                   const std::string& phone,
                                   const std::string& code,
                                   const std::string& password,
                                   std::string& error) {
    if (!EnsureDeviceAuthed(state, error)) {
        return false;
    }

    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "phone", phone.c_str());
    cJSON_AddStringToObject(request, "code", code.c_str());
    const std::string username = DefaultUsernameForPhone(phone);
    cJSON_AddStringToObject(request, "username", username.c_str());
    cJSON_AddStringToObject(request, "password", password.c_str());
    EtHttpResult result = EtClientHttp::PostJson(et_client::kUserRegisterPath,
                                                 request,
                                                 BearerToken(state.session.device_access_token));
    cJSON_Delete(request);
    if (!result.ok) {
        error = result.error_message;
        state.last_error_code = result.error_code;
        state.last_error_message = result.error_message;
        return false;
    }

    state.last_error_code.clear();
    state.last_error_message.clear();
    state.last_success_message = "User registered: " + username;
    return true;
}

bool EtClientService::LoginUser(EtClientState& state,
                                const std::string& phone,
                                const std::string& code,
                                const std::string& password,
                                std::string& error) {
    if (!EnsureDeviceAuthed(state, error)) {
        return false;
    }

    cJSON* request = cJSON_CreateObject();
    cJSON_AddStringToObject(request, "phone", phone.c_str());
    cJSON_AddStringToObject(request, "code", code.c_str());
    cJSON_AddStringToObject(request, "password", password.c_str());
    EtHttpResult result = EtClientHttp::PostJson(et_client::kUserLoginPath,
                                                 request,
                                                 BearerToken(state.session.device_access_token));
    cJSON_Delete(request);
    if (!result.ok) {
        error = result.error_message;
        state.last_error_code = result.error_code;
        state.last_error_message = result.error_message;
        return false;
    }

    cJSON* root = nullptr;
    std::string access_token;
    std::string refresh_token;
    std::string user_id;
    std::string username;
    std::string phone_value;
    int expires_in = 0;
    int refresh_expires_in = 0;
    if (!ParseJsonObject(result.body, root, error) ||
        !GetJsonString(root, "access_token", access_token, error) ||
        !GetJsonString(root, "refresh_token", refresh_token, error) ||
        !GetJsonString(root, "user_id", user_id, error) ||
        !GetJsonString(root, "username", username, error) ||
        !GetJsonString(root, "phone", phone_value, error) ||
        !GetJsonInt(root, "expires_in", expires_in, error) ||
        !GetJsonInt(root, "refresh_expires_in", refresh_expires_in, error)) {
        if (root != nullptr) {
            cJSON_Delete(root);
        }
        state.last_error_code = "USER_LOGIN_INVALID";
        state.last_error_message = error;
        return false;
    }
    cJSON_Delete(root);

    const auto now = static_cast<int64_t>(std::time(nullptr));
    state.session.user_access_token = access_token;
    state.session.user_refresh_token = refresh_token;
    state.session.user_access_token_expire_epoch = now + expires_in;
    state.session.user_refresh_token_expire_epoch = now + refresh_expires_in;
    state.session.user_id = user_id;
    state.session.phone = phone_value;
    state.session.username = username;
    EtClientStorage::SaveSessionRecord(state.session);
    state.user_logged_in = true;
    state.last_error_code.clear();
    state.last_error_message.clear();
    state.last_success_message = "User logged in: " + username;
    return true;
}

bool EtClientService::SendCodeSmart(EtClientState& state, const std::string& phone, std::string& error) {
    if (SendLoginCode(state, phone, error)) {
        return true;
    }

    if (state.last_error_code == "USER_PHONE_NOT_FOUND" || state.last_error_code == "USER_PHONE_NOT_REGISTERED") {
        error.clear();
        return SendRegisterCode(state, phone, error);
    }

    return false;
}

bool EtClientService::LoginOrAutoRegister(EtClientState& state,
                                          const std::string& phone,
                                          const std::string& code,
                                          const std::string& password,
                                          std::string& error) {
    if (LoginUser(state, phone, code, password, error)) {
        return true;
    }

    if (state.last_error_code == "PHONE_CODE_NOT_FOUND" ||
        state.last_error_code == "PHONE_CODE_INVALID" ||
        state.last_error_code == "PHONE_CODE_EXPIRED" ||
        state.last_error_code == "USER_INVALID_CREDENTIALS") {
        return false;
    }

    if (state.last_error_code == "USER_PHONE_NOT_FOUND" || state.last_error_code == "USER_PHONE_NOT_REGISTERED") {
        error.clear();
        if (!RegisterUser(state, phone, code, password, error)) {
            return false;
        }
        return true;
    }

    return false;
}

bool EtClientService::Logout(EtClientState& state, std::string& error) {
    if (state.session.user_access_token.empty()) {
        ClearUserSession(state);
        state.last_success_message = "User already logged out";
        return true;
    }

    cJSON* request = cJSON_CreateObject();
    EtHttpResult result = EtClientHttp::PostJson(et_client::kUserLogoutPath,
                                                 request,
                                                 BearerToken(state.session.user_access_token));
    cJSON_Delete(request);
    if (!result.ok) {
        error = result.error_message;
        state.last_error_code = result.error_code;
        state.last_error_message = result.error_message;
        return false;
    }

    ClearUserSession(state);
    state.last_error_code.clear();
    state.last_error_message.clear();
    state.last_success_message = "User logged out";
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
