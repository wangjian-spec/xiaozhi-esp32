#pragma once

#include <cstdint>
#include <string>

struct EtClientDeviceRecord {
    std::string private_key_b64;
    std::string public_key_b64;
    std::string device_id;
    bool registered = false;
};

struct EtClientSessionRecord {
    std::string device_access_token;
    int64_t device_access_token_expire_epoch = 0;

    std::string user_access_token;
    std::string user_refresh_token;
    int64_t user_access_token_expire_epoch = 0;
    int64_t user_refresh_token_expire_epoch = 0;

    std::string user_id;
    std::string phone;
    std::string username;
};

struct EtClientState {
    bool network_ready = false;

    bool device_key_ready = false;
    bool device_registered = false;
    bool device_authed = false;
    bool user_logged_in = false;
    bool resource_index_ready = false;
    bool busy = false;

    std::string busy_action;
    std::string last_error_code;
    std::string last_error_message;
    std::string last_success_message;

    EtClientDeviceRecord device{};
    EtClientSessionRecord session{};

    std::string input_phone;
    std::string input_password;
    std::string input_code;
    std::string input_backup_kind = "learning";

    std::string selected_resource_name;
    std::string selected_resource_version;
};
