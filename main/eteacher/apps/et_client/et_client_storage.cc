#include "eteacher/apps/et_client/et_client_storage.h"

#include "settings.h"

namespace {

constexpr const char* kDeviceNamespace = "et_device";
constexpr const char* kSessionNamespace = "et_session";

}  // namespace

void EtClientStorage::LoadDeviceRecord(EtClientDeviceRecord& record) {
    Settings settings(kDeviceNamespace, false);
    record.private_key_b64 = settings.GetString("priv_b64");
    record.public_key_b64 = settings.GetString("pub_b64");
    record.device_id = settings.GetString("device_id");
    record.registered = settings.GetBool("registered", false);
}

void EtClientStorage::SaveDeviceRecord(const EtClientDeviceRecord& record) {
    Settings settings(kDeviceNamespace, true);
    settings.SetString("priv_b64", record.private_key_b64);
    settings.SetString("pub_b64", record.public_key_b64);
    settings.SetString("device_id", record.device_id);
    settings.SetBool("registered", record.registered);
}

void EtClientStorage::LoadSessionRecord(EtClientSessionRecord& record) {
    Settings settings(kSessionNamespace, false);
    record.device_access_token = settings.GetString("device_token");
    record.device_access_token_expire_epoch = settings.GetInt("device_exp", 0);
    record.user_access_token = settings.GetString("user_token");
    record.user_refresh_token = settings.GetString("user_refresh");
    record.user_access_token_expire_epoch = settings.GetInt("user_exp", 0);
    record.user_refresh_token_expire_epoch = settings.GetInt("user_refresh_exp", 0);
    record.user_id = settings.GetString("user_id");
    record.phone = settings.GetString("phone");
    record.username = settings.GetString("username");
}

void EtClientStorage::SaveSessionRecord(const EtClientSessionRecord& record) {
    Settings settings(kSessionNamespace, true);
    settings.SetString("device_token", record.device_access_token);
    settings.SetInt("device_exp", static_cast<int32_t>(record.device_access_token_expire_epoch));
    settings.SetString("user_token", record.user_access_token);
    settings.SetString("user_refresh", record.user_refresh_token);
    settings.SetInt("user_exp", static_cast<int32_t>(record.user_access_token_expire_epoch));
    settings.SetInt("user_refresh_exp", static_cast<int32_t>(record.user_refresh_token_expire_epoch));
    settings.SetString("user_id", record.user_id);
    settings.SetString("phone", record.phone);
    settings.SetString("username", record.username);
}

void EtClientStorage::ClearUserSession() {
    Settings settings(kSessionNamespace, true);
    settings.EraseKey("user_token");
    settings.EraseKey("user_refresh");
    settings.EraseKey("user_exp");
    settings.EraseKey("user_refresh_exp");
    settings.EraseKey("user_id");
    settings.EraseKey("phone");
    settings.EraseKey("username");
}
