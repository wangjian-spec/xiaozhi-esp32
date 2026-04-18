#include "eteacher/apps/device_setting/et_server_client.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <mbedtls/base64.h>
#include <sodium.h>

#include <algorithm>
#include <sstream>
#include <utility>
#include <vector>

#include "boards/common/board.h"
#include "settings.h"
#include "system_info.h"

namespace {
constexpr const char* kTag = "EtServerClient";
constexpr const char* kDefaultApiBaseUrl = CONFIG_ET_SERVER_BASE_URL;
constexpr const char* kEtServerNs = "et_server";
constexpr const char* kRegisterChallengePath = "/api/v1/device/register/challenge";
constexpr const char* kRegisterVerifyPath = "/api/v1/device/register/verify";
constexpr const char* kAuthChallengePath = "/api/v1/device/auth/challenge";
constexpr const char* kAuthVerifyPath = "/api/v1/device/auth/verify";
constexpr const char* kUserRegisterCodeSendPath = "/api/v1/user/register/code/send";
constexpr const char* kUserLoginCodeSendPath = "/api/v1/user/login/code/send";
constexpr const char* kUserRegisterPath = "/api/v1/user/register";
constexpr const char* kUserLoginPath = "/api/v1/user/login";
constexpr const char* kResourceIndexPath = "/api/v1/resources/index";
constexpr const char* kResourceDownloadInitPath = "/api/v1/resources/download/init";
constexpr const char* kResourceDownloadDir = "/sdcard/et_server_resources";

constexpr const char* kKeyBaseUrl = "base_url";
constexpr const char* kKeyDeviceId = "device_id";
constexpr const char* kKeyDevicePubkey = "dev_pub_b64";
constexpr const char* kKeyDevicePrivateKey = "dev_priv_b64";
constexpr const char* kKeyDeviceToken = "dev_token";
constexpr const char* kKeyUserToken = "user_token";
constexpr const char* kKeyUserRefreshToken = "user_rtoken";
constexpr const char* kKeyUsername = "username";
constexpr const char* kKeyPhone = "phone";
constexpr const char* kKeyResourceName = "res_name";
constexpr const char* kKeyResourceVersion = "res_ver";
constexpr const char* kKeyResourceFormat = "res_fmt";
constexpr const char* kKeyInstalledResourceName = "inst_res_nm";
constexpr const char* kKeyInstalledResourceVersion = "inst_res_v";
constexpr const char* kKeyActivationStatus = "act_stat";
constexpr const char* kKeyPendingAutoUsername = "pend_user";
constexpr const char* kKeyLastDownloadPath = "last_dl";
constexpr const char* kKeyPendingFlowMode = "pend_flow";
constexpr int kHttpTimeoutMs = 15000;

static_assert(sizeof("base_url") - 1 <= 15);
static_assert(sizeof("device_id") - 1 <= 15);
static_assert(sizeof("dev_pub_b64") - 1 <= 15);
static_assert(sizeof("dev_priv_b64") - 1 <= 15);
static_assert(sizeof("dev_token") - 1 <= 15);
static_assert(sizeof("user_token") - 1 <= 15);
static_assert(sizeof("user_rtoken") - 1 <= 15);
static_assert(sizeof("username") - 1 <= 15);
static_assert(sizeof("phone") - 1 <= 15);
static_assert(sizeof("res_name") - 1 <= 15);
static_assert(sizeof("res_ver") - 1 <= 15);
static_assert(sizeof("res_fmt") - 1 <= 15);
static_assert(sizeof("inst_res_nm") - 1 <= 15);
static_assert(sizeof("inst_res_v") - 1 <= 15);
static_assert(sizeof("act_stat") - 1 <= 15);
static_assert(sizeof("pend_user") - 1 <= 15);
static_assert(sizeof("last_dl") - 1 <= 15);
static_assert(sizeof("pend_flow") - 1 <= 15);

bool EnsureSodiumReady() {
    static bool ready = false;
    static bool init_ok = false;
    if (!ready) {
        init_ok = sodium_init() >= 0;
        ready = true;
    }
    return init_ok;
}

std::string Base64Encode(const unsigned char* data, size_t size) {
    size_t out_len = 0;
    mbedtls_base64_encode(nullptr, 0, &out_len, data, size);
    std::string out;
    out.resize(out_len);
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out.data()), out.size(), &out_len, data, size) != 0) {
        return {};
    }
    out.resize(out_len);
    while (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

bool Base64Decode(const std::string& text, std::vector<unsigned char>& out) {
    size_t out_len = 0;
    if (mbedtls_base64_decode(nullptr, 0, &out_len,
                              reinterpret_cast<const unsigned char*>(text.data()), text.size()) != 0 &&
        out_len == 0) {
        return false;
    }
    out.resize(out_len);
    if (mbedtls_base64_decode(out.data(), out.size(), &out_len,
                              reinterpret_cast<const unsigned char*>(text.data()), text.size()) != 0) {
        return false;
    }
    out.resize(out_len);
    return true;
}

std::string HexEncode(const unsigned char* data, size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out[i * 2] = kHex[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

std::string Trim(std::string text) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

std::string JsonStringValue(cJSON* root, const char* key) {
    if (!root) {
        return {};
    }
    cJSON* item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) {
        return {};
    }
    return item->valuestring;
}

std::string BuildJson(cJSON* root) {
    if (!root) {
        return {};
    }
    char* text = cJSON_PrintUnformatted(root);
    std::string out = text ? text : "";
    if (text) {
        cJSON_free(text);
    }
    return out;
}

std::string ExtractErrorMessage(const std::string& body, int status_code) {
    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        std::ostringstream oss;
        oss << "HTTP " << status_code;
        if (!body.empty()) {
            oss << ": " << body;
        }
        return oss.str();
    }
    const std::string code = JsonStringValue(root, "code");
    const std::string message = JsonStringValue(root, "message");
    cJSON_Delete(root);
    if (!code.empty() && !message.empty()) {
        return code + ": " + message;
    }
    if (!message.empty()) {
        return message;
    }
    if (!code.empty()) {
        return code;
    }
    std::ostringstream oss;
    oss << "HTTP " << status_code;
    return oss.str();
}

std::string JoinUrl(const std::string& base, const std::string& path) {
    if (base.empty()) {
        return path;
    }
    if (!path.empty() && path.front() == '/') {
        return base + path;
    }
    return base + "/" + path;
}

std::string ShortForLog(const std::string& text, size_t max_len = 160) {
    if (text.size() <= max_len) {
        return text;
    }
    return text.substr(0, max_len) + "...";
}

std::string DropAuthorityPort(const std::string& authority) {
    if (authority.empty()) {
        return {};
    }
    if (authority.front() == '[') {
        const size_t end = authority.find(']');
        if (end != std::string::npos) {
            return authority.substr(0, end + 1);
        }
    }
    const size_t first_colon = authority.find(':');
    const size_t last_colon = authority.rfind(':');
    if (first_colon != std::string::npos && first_colon == last_colon) {
        return authority.substr(0, first_colon);
    }
    return authority;
}

bool IsLoopbackAuthority(const std::string& authority) {
    const std::string host = DropAuthorityPort(authority);
    return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

bool IsLoopbackBaseUrl(const std::string& url) {
    const size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) {
        return false;
    }
    return IsLoopbackAuthority(url.substr(scheme_pos + 3));
}

std::string NormalizeApiBaseUrl(const std::string& raw_url, bool drop_port) {
    std::string url = Trim(raw_url);
    if (url.empty()) {
        return {};
    }

    const size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) {
        return {};
    }

    std::string scheme = url.substr(0, scheme_pos);
    if (scheme == "wss") {
        scheme = "https";
    } else if (scheme == "ws") {
        scheme = "http";
    }

    const size_t authority_start = scheme_pos + 3;
    size_t authority_end = url.find_first_of("/?#", authority_start);
    if (authority_end == std::string::npos) {
        authority_end = url.size();
    }

    std::string authority = url.substr(authority_start, authority_end - authority_start);
    if (authority.empty()) {
        return {};
    }
    if (drop_port) {
        authority = DropAuthorityPort(authority);
    }
    if (authority.empty()) {
        return {};
    }

    std::string normalized = scheme + "://" + authority;
    std::string path = url.substr(authority_end);
    while (!path.empty() && path.back() == '/') {
        path.pop_back();
    }
    if (path == "/api/v1") {
        return normalized;
    }
    if (path.empty()) {
        return normalized;
    }
    return normalized;
}

bool EnsureDirExists(const char* path) {
    struct stat st = {};
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return mkdir(path, 0777) == 0;
}

struct HttpResponse {
    int status_code = 0;
    std::string body;
};

bool IsRedirectStatusCode(int status_code) {
    return status_code == 301 || status_code == 302 || status_code == 307 || status_code == 308;
}

std::string UpgradeHttpUrlToHttps(const std::string& url) {
    if (url.rfind("http://", 0) != 0) {
        return {};
    }
    return "https://" + url.substr(sizeof("http://") - 1);
}

bool RequestHttp(const std::string& method,
                 const std::string& url,
                 const std::string& body,
                 const std::vector<std::pair<std::string, std::string>>& headers,
                 HttpResponse& response,
                 std::string& error) {
    auto* network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        error = "网络接口不可用";
        return false;
    }

    auto http = network->CreateHttp(3);
    if (!http) {
        error = "HTTP客户端创建失败";
        return false;
    }

    http->SetTimeout(kHttpTimeoutMs);
    http->SetKeepAlive(false);
    http->SetHeader("User-Agent", SystemInfo::GetUserAgent());
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    for (const auto& header : headers) {
        http->SetHeader(header.first, header.second);
    }
    if (!body.empty()) {
        http->SetHeader("Content-Type", "application/json");
        http->SetContent(std::string(body));
    }

    ESP_LOGI(kTag, "HTTP %s %s", method.c_str(), url.c_str());
    if (!headers.empty()) {
        ESP_LOGI(kTag, "HTTP headers count=%u", static_cast<unsigned>(headers.size()));
    }
    if (!body.empty()) {
        ESP_LOGI(kTag, "HTTP body: %s", ShortForLog(body).c_str());
    }

    if (!http->Open(method, url)) {
        std::ostringstream oss;
        oss << "HTTP连接失败: 0x" << std::hex << http->GetLastError();
        error = oss.str();
        ESP_LOGE(kTag, "HTTP open failed: %s", error.c_str());
        return false;
    }

    response.status_code = http->GetStatusCode();
    ESP_LOGI(kTag, "HTTP status=%d body_length=%u", response.status_code,
             static_cast<unsigned>(http->GetBodyLength()));
    response.body = http->ReadAll();
    http->Close();
    ESP_LOGI(kTag, "HTTP response body: %s", ShortForLog(response.body).c_str());

    if (response.status_code < 200 || response.status_code >= 300) {
        error = ExtractErrorMessage(response.body, response.status_code);
        ESP_LOGE(kTag, "HTTP request failed: %s", error.c_str());
        return false;
    }
    return true;
}

bool ParseJsonBody(const std::string& body, cJSON** out_root, std::string& error) {
    *out_root = cJSON_Parse(body.c_str());
    if (*out_root == nullptr) {
        error = "服务器返回的JSON无效";
        return false;
    }
    return true;
}

std::string WebsocketToHttpBase(const std::string& ws_url) {
    return NormalizeApiBaseUrl(ws_url, true);
}

std::string MqttToHttpBase(const std::string& endpoint) {
    const std::string host = DropAuthorityPort(Trim(endpoint));
    if (host.empty()) {
        return {};
    }
    return "http://" + host;
}

std::string LegacyWebsocketToHttpBase(const std::string& ws_url) {
    return NormalizeApiBaseUrl(ws_url, false);
}

std::string LegacyMqttToHttpBase(const std::string& endpoint) {
    std::string host = Trim(endpoint);
    if (host.empty()) {
        return {};
    }
    const size_t colon = host.find(':');
    if (colon != std::string::npos) {
        host = host.substr(0, colon);
    }
    if (host.empty()) {
        return {};
    }
    return "http://" + host + ":18080";
}

std::string MigrateLegacyDerivedBaseUrl(const std::string& saved_url) {
    const std::string normalized_saved = NormalizeApiBaseUrl(saved_url, false);
    if (normalized_saved.empty()) {
        return {};
    }

    Settings websocket_settings("websocket", false);
    const std::string legacy_ws = LegacyWebsocketToHttpBase(websocket_settings.GetString("url"));
    const std::string preferred_ws = WebsocketToHttpBase(websocket_settings.GetString("url"));
    if (!legacy_ws.empty() && normalized_saved == legacy_ws && !preferred_ws.empty() && preferred_ws != legacy_ws) {
        ESP_LOGW(kTag, "Migrating legacy websocket-derived base_url from %s to %s",
                 normalized_saved.c_str(), preferred_ws.c_str());
        return preferred_ws;
    }

    Settings mqtt_settings("mqtt", false);
    const std::string legacy_mqtt = LegacyMqttToHttpBase(mqtt_settings.GetString("endpoint"));
    const std::string preferred_mqtt = MqttToHttpBase(mqtt_settings.GetString("endpoint"));
    if (!legacy_mqtt.empty() && normalized_saved == legacy_mqtt && !preferred_mqtt.empty() && preferred_mqtt != legacy_mqtt) {
        ESP_LOGW(kTag, "Migrating legacy mqtt-derived base_url from %s to %s",
                 normalized_saved.c_str(), preferred_mqtt.c_str());
        return preferred_mqtt;
    }

    return normalized_saved;
}

std::string ConfiguredApiBaseUrl() {
    return NormalizeApiBaseUrl(kDefaultApiBaseUrl, false);
}
}

EtServerClient::EtServerClient() {
    LoadState();
}

void EtServerClient::LoadState() {
    Settings settings(kEtServerNs, false);
    api_base_url_ = MigrateLegacyDerivedBaseUrl(settings.GetString(kKeyBaseUrl));
    const std::string configured_base_url = ConfiguredApiBaseUrl();
    if (!configured_base_url.empty() && api_base_url_ != configured_base_url) {
        ESP_LOGW(kTag, "Overriding persisted et_server base_url from %s to configured %s",
                 api_base_url_.c_str(), configured_base_url.c_str());
        api_base_url_ = configured_base_url;
    }
    if (IsLoopbackBaseUrl(api_base_url_)) {
        ESP_LOGW(kTag, "Configured et_server base_url is loopback and cannot be reached from ESP32: %s",
                 api_base_url_.c_str());
    }
    device_id_ = settings.GetString(kKeyDeviceId);
    device_pubkey_b64_ = settings.GetString(kKeyDevicePubkey);
    device_private_key_b64_ = settings.GetString(kKeyDevicePrivateKey);
    device_token_ = settings.GetString(kKeyDeviceToken);
    user_token_ = settings.GetString(kKeyUserToken);
    user_refresh_token_ = settings.GetString(kKeyUserRefreshToken);
    username_ = settings.GetString(kKeyUsername);
    phone_ = settings.GetString(kKeyPhone);
    resource_name_ = settings.GetString(kKeyResourceName);
    resource_version_ = settings.GetString(kKeyResourceVersion);
    resource_format_ = settings.GetString(kKeyResourceFormat);
    installed_resource_name_ = settings.GetString(kKeyInstalledResourceName);
    installed_resource_version_ = settings.GetString(kKeyInstalledResourceVersion);
    activation_status_ = settings.GetString(kKeyActivationStatus, "未检查");
    pending_auto_username_ = settings.GetString(kKeyPendingAutoUsername);
    last_download_path_ = settings.GetString(kKeyLastDownloadPath);
    const std::string pending_mode = settings.GetString(kKeyPendingFlowMode, "login");
    pending_flow_mode_ = pending_mode == "register_then_login"
        ? EtServerPendingFlowMode::RegisterThenLogin
        : EtServerPendingFlowMode::Login;
    if (api_base_url_.empty()) {
        api_base_url_ = DeriveApiBaseUrl();
    }
    if (!api_base_url_.empty()) {
        SaveState();
    }
    ESP_LOGI(kTag, "LoadState base_url=%s device_id=%s logged_in=%d pending_mode=%d",
             api_base_url_.c_str(), device_id_.c_str(), !user_token_.empty(), static_cast<int>(pending_flow_mode_));
}

void EtServerClient::SaveState() {
    Settings settings(kEtServerNs, true);
    settings.SetString(kKeyBaseUrl, api_base_url_);
    settings.SetString(kKeyDeviceId, device_id_);
    settings.SetString(kKeyDevicePubkey, device_pubkey_b64_);
    settings.SetString(kKeyDevicePrivateKey, device_private_key_b64_);
    settings.SetString(kKeyDeviceToken, device_token_);
    settings.SetString(kKeyUserToken, user_token_);
    settings.SetString(kKeyUserRefreshToken, user_refresh_token_);
    settings.SetString(kKeyUsername, username_);
    settings.SetString(kKeyPhone, phone_);
    settings.SetString(kKeyResourceName, resource_name_);
    settings.SetString(kKeyResourceVersion, resource_version_);
    settings.SetString(kKeyResourceFormat, resource_format_);
    settings.SetString(kKeyInstalledResourceName, installed_resource_name_);
    settings.SetString(kKeyInstalledResourceVersion, installed_resource_version_);
    settings.SetString(kKeyActivationStatus, activation_status_);
    settings.SetString(kKeyPendingAutoUsername, pending_auto_username_);
    settings.SetString(kKeyLastDownloadPath, last_download_path_);
    settings.SetString(kKeyPendingFlowMode,
                       pending_flow_mode_ == EtServerPendingFlowMode::RegisterThenLogin
                           ? "register_then_login"
                           : "login");
}

void EtServerClient::ClearUserSession() {
    user_token_.clear();
    user_refresh_token_.clear();
    username_.clear();
    phone_.clear();
}

bool EtServerClient::RequestApi(const std::string& method,
                                const std::string& path_or_url,
                                const std::string& body,
                                const std::vector<std::pair<std::string, std::string>>& headers,
                                std::string& response_body,
                                std::string& error,
                                bool absolute_url) {
    HttpResponse response;
    std::string url = absolute_url ? path_or_url : JoinUrl(api_base_url_, path_or_url);
    if (!absolute_url && IsLoopbackBaseUrl(api_base_url_)) {
        error = "et_server地址不能是127.0.0.1或localhost，请改为电脑局域网IP，例如 http://192.168.1.195:18080";
        ESP_LOGE(kTag, "Reject unreachable loopback et_server base_url: %s", api_base_url_.c_str());
        return false;
    }
    if (RequestHttp(method, url, body, headers, response, error)) {
        response_body = std::move(response.body);
        return true;
    }

    if (!IsRedirectStatusCode(response.status_code)) {
        return false;
    }

    const std::string https_url = UpgradeHttpUrlToHttps(url);
    if (https_url.empty()) {
        return false;
    }

    ESP_LOGW(kTag, "HTTP redirect %d detected, retry with HTTPS: %s", response.status_code, https_url.c_str());
    if (!absolute_url && api_base_url_.rfind("http://", 0) == 0) {
        const std::string https_base = UpgradeHttpUrlToHttps(api_base_url_);
        if (!https_base.empty()) {
            api_base_url_ = https_base;
            SaveState();
            ESP_LOGI(kTag, "Promoted et_server base_url to HTTPS after redirect: %s", api_base_url_.c_str());
        }
    }
    error.clear();
    HttpResponse https_response;
    if (!RequestHttp(method, https_url, body, headers, https_response, error)) {
        return false;
    }

    response_body = std::move(https_response.body);
    return true;
}

std::string EtServerClient::DeriveApiBaseUrl() const {
    Settings et_settings(kEtServerNs, false);
    const std::string explicit_url = NormalizeApiBaseUrl(et_settings.GetString(kKeyBaseUrl), false);
    const std::string configured_base_url = ConfiguredApiBaseUrl();
    if (!configured_base_url.empty()) {
        ESP_LOGI(kTag, "Using configured et_server base_url: %s", configured_base_url.c_str());
        return configured_base_url;
    }

    if (!explicit_url.empty()) {
        ESP_LOGI(kTag, "Using explicit et_server base_url: %s", explicit_url.c_str());
        return explicit_url;
    }

    Settings websocket_settings("websocket", false);
    const std::string websocket_url = WebsocketToHttpBase(websocket_settings.GetString("url"));
    if (!websocket_url.empty()) {
        ESP_LOGI(kTag, "Derived base_url from websocket: %s", websocket_url.c_str());
        return websocket_url;
    }

    Settings mqtt_settings("mqtt", false);
    const std::string mqtt_url = MqttToHttpBase(mqtt_settings.GetString("endpoint"));
    if (!mqtt_url.empty()) {
        ESP_LOGI(kTag, "Derived base_url from mqtt: %s", mqtt_url.c_str());
        return mqtt_url;
    }

    ESP_LOGW(kTag, "Falling back to default base_url: %s", kDefaultApiBaseUrl);
    return kDefaultApiBaseUrl;
}

std::string EtServerClient::GenerateAutoUsername(const std::string& phone) const {
    std::string digits;
    digits.reserve(phone.size());
    for (char ch : phone) {
        if (ch >= '0' && ch <= '9') {
            digits.push_back(ch);
        }
    }
    std::string suffix = digits.empty() ? "user" : digits.substr(digits.size() > 6 ? digits.size() - 6 : 0);
    const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000ULL);
    std::ostringstream oss;
    oss << "user_" << suffix << "_" << std::hex << (now_ms & 0xFFFFULL);
    return oss.str();
}

bool EtServerClient::EnsureDeviceBundle(std::string& error) {
    ESP_LOGI(kTag, "EnsureDeviceBundle");
    if (!EnsureSodiumReady()) {
        error = "libsodium初始化失败";
        return false;
    }
    if (!device_pubkey_b64_.empty() && !device_private_key_b64_.empty()) {
        return true;
    }

    std::vector<unsigned char> public_key(crypto_sign_PUBLICKEYBYTES);
    std::vector<unsigned char> secret_key(crypto_sign_SECRETKEYBYTES);
    if (crypto_sign_keypair(public_key.data(), secret_key.data()) != 0) {
        error = "生成设备密钥失败";
        return false;
    }

    device_pubkey_b64_ = Base64Encode(public_key.data(), public_key.size());
    device_private_key_b64_ = Base64Encode(secret_key.data(), secret_key.size());

    unsigned char hash[crypto_hash_sha256_BYTES] = {0};
    crypto_hash_sha256(hash, public_key.data(), public_key.size());
    device_id_ = HexEncode(hash, sizeof(hash));
    SaveState();
    return true;
}

bool EtServerClient::EnsureRegistered(std::string& error) {
    ESP_LOGI(kTag, "EnsureRegistered device_id=%s", device_id_.c_str());
    if (!EnsureDeviceBundle(error)) {
        return false;
    }

    std::vector<unsigned char> secret_key;
    if (!Base64Decode(device_private_key_b64_, secret_key) || secret_key.size() != crypto_sign_SECRETKEYBYTES) {
        error = "设备私钥损坏";
        return false;
    }

    cJSON* challenge_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(challenge_payload, "device_pubkey", device_pubkey_b64_.c_str());
    const std::string challenge_body = BuildJson(challenge_payload);
    cJSON_Delete(challenge_payload);
    std::string response_body;
    if (!RequestApi("POST", kRegisterChallengePath, challenge_body, {}, response_body, error)) {
        if (error.find("DEVICE_REVOKED") != std::string::npos) {
            activation_status_ = "设备已禁用";
            SaveState();
        }
        if (error.find("AUTH_INVALID_PUBKEY") == std::string::npos) {
            return false;
        }
    }

    cJSON* root = nullptr;
    if (!ParseJsonBody(response_body, &root, error)) {
        return false;
    }
    const std::string challenge = JsonStringValue(root, "challenge");
    const std::string challenge_id = JsonStringValue(root, "challenge_id");
    const std::string server_device_id = JsonStringValue(root, "device_id");
    cJSON_Delete(root);
    ESP_LOGI(kTag, "Register challenge parsed: challenge_id=%s server_device_id=%s challenge_len=%u",
             challenge_id.c_str(), server_device_id.c_str(), static_cast<unsigned>(challenge.size()));
    if (challenge.empty() || challenge_id.empty() || server_device_id.empty()) {
        error = "注册挑战响应缺少字段";
        return false;
    }

    unsigned char signature[crypto_sign_BYTES] = {0};
    unsigned long long signature_len = 0;
    const std::string signing_text = "register:" + challenge;
    if (crypto_sign_detached(signature,
                             &signature_len,
                             reinterpret_cast<const unsigned char*>(signing_text.data()),
                             signing_text.size(),
                             secret_key.data()) != 0) {
        error = "设备签名失败";
        return false;
    }

    cJSON* verify_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(verify_payload, "device_id", server_device_id.c_str());
    cJSON_AddStringToObject(verify_payload, "challenge_id", challenge_id.c_str());
    const std::string signature_b64 = Base64Encode(signature, signature_len);
    cJSON_AddStringToObject(verify_payload, "signature", signature_b64.c_str());
    const std::string verify_body = BuildJson(verify_payload);
    cJSON_Delete(verify_payload);

    if (!RequestApi("POST", kRegisterVerifyPath, verify_body, {}, response_body, error)) {
        activation_status_ = "注册失败";
        SaveState();
        return false;
    }

    device_id_ = server_device_id;
    activation_status_ = "已激活";
    ESP_LOGI(kTag, "Register verify succeeded, activation_status=%s device_id=%s",
             activation_status_.c_str(), device_id_.c_str());
    SaveState();
    return true;
}

bool EtServerClient::EnsureAuthenticated(std::string& error) {
    ESP_LOGI(kTag, "EnsureAuthenticated device_id=%s", device_id_.c_str());
    if (!EnsureRegistered(error)) {
        return false;
    }

    std::vector<unsigned char> secret_key;
    if (!Base64Decode(device_private_key_b64_, secret_key) || secret_key.size() != crypto_sign_SECRETKEYBYTES) {
        error = "设备私钥损坏";
        return false;
    }

    cJSON* challenge_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(challenge_payload, "device_id", device_id_.c_str());
    const std::string challenge_body = BuildJson(challenge_payload);
    cJSON_Delete(challenge_payload);
    std::string response_body;
    if (!RequestApi("POST", kAuthChallengePath, challenge_body, {}, response_body, error)) {
        return false;
    }

    cJSON* root = nullptr;
    if (!ParseJsonBody(response_body, &root, error)) {
        return false;
    }
    const std::string challenge = JsonStringValue(root, "challenge");
    const std::string challenge_id = JsonStringValue(root, "challenge_id");
    cJSON_Delete(root);
    ESP_LOGI(kTag, "Auth challenge parsed: challenge_id=%s challenge_len=%u",
             challenge_id.c_str(), static_cast<unsigned>(challenge.size()));
    if (challenge.empty() || challenge_id.empty()) {
        error = "鉴权挑战响应缺少字段";
        return false;
    }

    unsigned char signature[crypto_sign_BYTES] = {0};
    unsigned long long signature_len = 0;
    const std::string signing_text = "auth:" + challenge;
    if (crypto_sign_detached(signature,
                             &signature_len,
                             reinterpret_cast<const unsigned char*>(signing_text.data()),
                             signing_text.size(),
                             secret_key.data()) != 0) {
        error = "设备鉴权签名失败";
        return false;
    }

    cJSON* verify_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(verify_payload, "device_id", device_id_.c_str());
    cJSON_AddStringToObject(verify_payload, "challenge_id", challenge_id.c_str());
    const std::string signature_b64 = Base64Encode(signature, signature_len);
    cJSON_AddStringToObject(verify_payload, "signature", signature_b64.c_str());
    const std::string verify_body = BuildJson(verify_payload);
    cJSON_Delete(verify_payload);

    if (!RequestApi("POST", kAuthVerifyPath, verify_body, {}, response_body, error)) {
        return false;
    }

    if (!ParseJsonBody(response_body, &root, error)) {
        return false;
    }
    device_token_ = JsonStringValue(root, "access_token");
    cJSON_Delete(root);
    ESP_LOGI(kTag, "Auth verify parsed: token_len=%u", static_cast<unsigned>(device_token_.size()));
    if (device_token_.empty()) {
        error = "设备令牌为空";
        return false;
    }
    activation_status_ = "已认证";
    SaveState();
    return true;
}

bool EtServerClient::RefreshResourcesIndex(std::string& error) {
    ESP_LOGI(kTag, "RefreshResourcesIndex resource_name=%s resource_version=%s",
             resource_name_.c_str(), resource_version_.c_str());
    const std::vector<std::pair<std::string, std::string>> headers = {
        {"Authorization", "Bearer " + device_token_},
    };
    std::string response_body;
    if (!RequestApi("GET", kResourceIndexPath, "", headers, response_body, error)) {
        return false;
    }

    cJSON* root = nullptr;
    if (!ParseJsonBody(response_body, &root, error)) {
        return false;
    }
    cJSON* resources = cJSON_GetObjectItem(root, "resources");
    if (!cJSON_IsArray(resources) || cJSON_GetArraySize(resources) == 0) {
        resource_name_.clear();
        resource_version_.clear();
        resource_format_.clear();
        cJSON_Delete(root);
        SaveState();
        return true;
    }

    cJSON* chosen = cJSON_GetArrayItem(resources, 0);
    if (!resource_name_.empty()) {
        const int count = cJSON_GetArraySize(resources);
        for (int i = 0; i < count; ++i) {
            cJSON* item = cJSON_GetArrayItem(resources, i);
            if (JsonStringValue(item, "name") == resource_name_) {
                chosen = item;
                break;
            }
        }
    }

    resource_name_ = JsonStringValue(chosen, "name");
    resource_version_ = JsonStringValue(chosen, "version");
    resource_format_ = JsonStringValue(chosen, "format");
    cJSON_Delete(root);
    ESP_LOGI(kTag, "Resource selected: name=%s version=%s format=%s",
             resource_name_.c_str(), resource_version_.c_str(), resource_format_.c_str());
    SaveState();
    return true;
}

bool EtServerClient::RefreshStatus(std::string& message, std::string& error) {
    ESP_LOGI(kTag, "RefreshStatus base_url=%s", api_base_url_.c_str());
    if (api_base_url_.empty()) {
        api_base_url_ = DeriveApiBaseUrl();
    }

    if (!EnsureAuthenticated(error)) {
        if (activation_status_.empty() || activation_status_ == "未检查") {
            activation_status_ = "认证失败";
        }
        SaveState();
        return false;
    }
    if (!RefreshResourcesIndex(error)) {
        SaveState();
        return false;
    }

    std::ostringstream oss;
    oss << "设备已认证";
    if (!resource_name_.empty() && !resource_version_.empty()) {
        oss << "，资源 " << resource_name_ << "@" << resource_version_;
    } else {
        oss << "，当前无可用资源";
    }
    message = oss.str();
    SaveState();
    return true;
}

bool EtServerClient::SendVerificationCode(const std::string& phone, std::string& message, std::string& error) {
    ESP_LOGI(kTag, "SendVerificationCode phone=%s base_url=%s", phone.c_str(), api_base_url_.c_str());
    if (!EnsureAuthenticated(error)) {
        return false;
    }
    const std::string normalized_phone = Trim(phone);
    if (normalized_phone.empty()) {
        error = "手机号不能为空";
        return false;
    }

    cJSON* payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "phone", normalized_phone.c_str());
    const std::string body = BuildJson(payload);
    cJSON_Delete(payload);

    const std::vector<std::pair<std::string, std::string>> headers = {
        {"Authorization", "Bearer " + device_token_},
    };

    std::string response_body;
    if (RequestApi("POST", kUserLoginCodeSendPath, body, headers, response_body, error)) {
        ESP_LOGI(kTag, "Login code send succeeded for phone=%s", normalized_phone.c_str());
        pending_flow_mode_ = EtServerPendingFlowMode::Login;
        pending_auto_username_.clear();
        phone_ = normalized_phone;
        message = "登录验证码已发送";
        SaveState();
        return true;
    }

    if (error.find("USER_PHONE_NOT_FOUND") == std::string::npos) {
        ESP_LOGE(kTag, "Login code send failed without fallback: %s", error.c_str());
        return false;
    }

    pending_auto_username_ = GenerateAutoUsername(normalized_phone);
    ESP_LOGW(kTag, "Phone not found, fallback to register flow, auto_username=%s", pending_auto_username_.c_str());
    if (!RequestApi("POST", kUserRegisterCodeSendPath, body, headers, response_body, error)) {
        return false;
    }
    ESP_LOGI(kTag, "Register code send succeeded for phone=%s", normalized_phone.c_str());

    pending_flow_mode_ = EtServerPendingFlowMode::RegisterThenLogin;
    phone_ = normalized_phone;
    std::ostringstream oss;
    oss << "手机号未注册，已发送注册验证码，用户名将自动生成为 " << pending_auto_username_;
    message = oss.str();
    SaveState();
    return true;
}

bool EtServerClient::CompleteLoginOrRegister(const std::string& phone,
                                             const std::string& password,
                                             const std::string& code,
                                             EtServerLoginActionResult& result,
                                             std::string& message,
                                             std::string& error) {
    ESP_LOGI(kTag, "CompleteLoginOrRegister phone=%s code_len=%u password_len=%u mode=%d",
             phone.c_str(), static_cast<unsigned>(code.size()), static_cast<unsigned>(password.size()),
             static_cast<int>(pending_flow_mode_));
    if (!EnsureAuthenticated(error)) {
        return false;
    }

    const std::string normalized_phone = Trim(phone);
    const std::string normalized_password = password;
    const std::string normalized_code = Trim(code);
    if (normalized_phone.empty() || normalized_password.empty() || normalized_code.empty()) {
        error = "手机号、密码、验证码不能为空";
        return false;
    }

    const std::vector<std::pair<std::string, std::string>> headers = {
        {"Authorization", "Bearer " + device_token_},
    };

    std::string response_body;
    if (pending_flow_mode_ == EtServerPendingFlowMode::RegisterThenLogin) {
        cJSON* register_payload = cJSON_CreateObject();
        cJSON_AddStringToObject(register_payload, "phone", normalized_phone.c_str());
        cJSON_AddStringToObject(register_payload, "code", normalized_code.c_str());
        cJSON_AddStringToObject(register_payload, "username", pending_auto_username_.c_str());
        cJSON_AddStringToObject(register_payload, "password", normalized_password.c_str());
        const std::string register_body = BuildJson(register_payload);
        cJSON_Delete(register_payload);

        if (!RequestApi("POST", kUserRegisterPath, register_body, headers, response_body, error)) {
            return false;
        }
        ESP_LOGI(kTag, "User register succeeded for phone=%s username=%s",
                 normalized_phone.c_str(), pending_auto_username_.c_str());

        cJSON* login_code_payload = cJSON_CreateObject();
        cJSON_AddStringToObject(login_code_payload, "phone", normalized_phone.c_str());
        const std::string login_code_body = BuildJson(login_code_payload);
        cJSON_Delete(login_code_payload);
        if (!RequestApi("POST", kUserLoginCodeSendPath, login_code_body, headers, response_body, error)) {
            return false;
        }
        ESP_LOGI(kTag, "Follow-up login code send succeeded for phone=%s", normalized_phone.c_str());

        pending_flow_mode_ = EtServerPendingFlowMode::Login;
        result = EtServerLoginActionResult::RegisteredNeedLoginCode;
        message = "用户已注册，已自动发送登录验证码，请输入新的登录验证码后再次按登录";
        SaveState();
        return true;
    }

    cJSON* login_payload = cJSON_CreateObject();
    cJSON_AddStringToObject(login_payload, "phone", normalized_phone.c_str());
    cJSON_AddStringToObject(login_payload, "code", normalized_code.c_str());
    cJSON_AddStringToObject(login_payload, "password", normalized_password.c_str());
    const std::string login_body = BuildJson(login_payload);
    cJSON_Delete(login_payload);
    if (!RequestApi("POST", kUserLoginPath, login_body, headers, response_body, error)) {
        return false;
    }

    cJSON* root = nullptr;
    if (!ParseJsonBody(response_body, &root, error)) {
        return false;
    }
    user_token_ = JsonStringValue(root, "access_token");
    user_refresh_token_ = JsonStringValue(root, "refresh_token");
    username_ = JsonStringValue(root, "username");
    phone_ = JsonStringValue(root, "phone");
    cJSON_Delete(root);
    ESP_LOGI(kTag, "User login parsed: token_len=%u refresh_len=%u username=%s phone=%s",
             static_cast<unsigned>(user_token_.size()), static_cast<unsigned>(user_refresh_token_.size()),
             username_.c_str(), phone_.c_str());
    if (user_token_.empty()) {
        error = "用户登录失败，缺少访问令牌";
        return false;
    }

    result = EtServerLoginActionResult::LoggedIn;
    message = "用户登录成功";
    SaveState();
    return true;
}

bool EtServerClient::DownloadCurrentResource(std::string& message, std::string& error) {
    if (!EnsureAuthenticated(error)) {
        return false;
    }
    if ((resource_name_.empty() || resource_version_.empty()) && !RefreshResourcesIndex(error)) {
        return false;
    }
    if (resource_name_.empty() || resource_version_.empty()) {
        error = "没有可下载的资源，请先执行状态检查";
        return false;
    }

    std::ostringstream url;
    url << JoinUrl(api_base_url_, kResourceDownloadInitPath)
        << "?name=" << resource_name_
        << "&version=" << resource_version_;

    const std::vector<std::pair<std::string, std::string>> headers = {
        {"Authorization", "Bearer " + device_token_},
    };
    std::string response_body;
    if (!RequestApi("GET", url.str(), "", headers, response_body, error, true)) {
        return false;
    }

    cJSON* root = nullptr;
    if (!ParseJsonBody(response_body, &root, error)) {
        return false;
    }
    const std::string get_url = JsonStringValue(root, "get_url");
    const std::string sha256 = JsonStringValue(root, "sha256");
    cJSON_Delete(root);
    if (get_url.empty()) {
        error = "资源下载地址为空";
        return false;
    }

    if (!EnsureDirExists(kResourceDownloadDir)) {
        error = "创建资源目录失败";
        return false;
    }

    const std::string file_ext = resource_format_ == "tar.gz" ? ".tar.gz" : ".zip";
    const std::string file_path = std::string(kResourceDownloadDir) + "/" + resource_name_ + "-" + resource_version_ + file_ext;

    auto* network = Board::GetInstance().GetNetwork();
    if (!network) {
        error = "网络接口不可用";
        return false;
    }
    auto http = network->CreateHttp(3);
    if (!http) {
        error = "HTTP客户端下载器创建失败";
        return false;
    }
    if (!http->Open("GET", get_url)) {
        std::ostringstream oss;
        oss << "资源下载连接失败: 0x" << std::hex << http->GetLastError();
        error = oss.str();
        return false;
    }
    if (http->GetStatusCode() != 200) {
        error = "资源下载失败: HTTP " + std::to_string(http->GetStatusCode());
        return false;
    }

    std::FILE* fp = std::fopen(file_path.c_str(), "wb");
    if (!fp) {
        error = "无法写入资源文件";
        return false;
    }

    crypto_hash_sha256_state hash_state;
    crypto_hash_sha256_init(&hash_state);
    char buffer[1024];
    while (true) {
        const int read_size = http->Read(buffer, sizeof(buffer));
        if (read_size < 0) {
            std::fclose(fp);
            std::remove(file_path.c_str());
            error = "资源下载读取失败";
            return false;
        }
        if (read_size == 0) {
            break;
        }
        if (std::fwrite(buffer, 1, static_cast<size_t>(read_size), fp) != static_cast<size_t>(read_size)) {
            std::fclose(fp);
            std::remove(file_path.c_str());
            error = "资源文件写入失败";
            return false;
        }
        crypto_hash_sha256_update(&hash_state, reinterpret_cast<const unsigned char*>(buffer), read_size);
    }
    std::fclose(fp);
    http->Close();

    if (!sha256.empty()) {
        unsigned char hash[crypto_hash_sha256_BYTES] = {0};
        crypto_hash_sha256_final(&hash_state, hash);
        if (HexEncode(hash, sizeof(hash)) != sha256) {
            std::remove(file_path.c_str());
            error = "资源校验失败，SHA256不匹配";
            return false;
        }
    }

    installed_resource_name_ = resource_name_;
    installed_resource_version_ = resource_version_;
    last_download_path_ = file_path;
    SaveState();

    std::ostringstream oss;
    oss << "资源已下载到 " << file_path;
    message = oss.str();
    return true;
}