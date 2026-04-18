#pragma once

#include <utility>
#include <string>
#include <vector>

enum class EtServerPendingFlowMode {
    Login,
    RegisterThenLogin,
};

enum class EtServerLoginActionResult {
    LoggedIn,
    RegisteredNeedLoginCode,
};

class EtServerClient {
public:
    EtServerClient();

    bool RefreshStatus(std::string& message, std::string& error);
    bool SendVerificationCode(const std::string& phone, std::string& message, std::string& error);
    bool CompleteLoginOrRegister(const std::string& phone,
                                 const std::string& password,
                                 const std::string& code,
                                 EtServerLoginActionResult& result,
                                 std::string& message,
                                 std::string& error);
    bool DownloadCurrentResource(std::string& message, std::string& error);

    const std::string& ApiBaseUrl() const { return api_base_url_; }
    const std::string& DeviceId() const { return device_id_; }
    const std::string& Username() const { return username_; }
    const std::string& Phone() const { return phone_; }
    const std::string& ResourceName() const { return resource_name_; }
    const std::string& ResourceVersion() const { return resource_version_; }
    const std::string& InstalledResourceName() const { return installed_resource_name_; }
    const std::string& InstalledResourceVersion() const { return installed_resource_version_; }
    const std::string& ActivationStatus() const { return activation_status_; }
    const std::string& PendingAutoUsername() const { return pending_auto_username_; }
    const std::string& LastDownloadPath() const { return last_download_path_; }
    EtServerPendingFlowMode PendingFlowMode() const { return pending_flow_mode_; }
    bool IsUserLoggedIn() const { return !user_token_.empty(); }

private:
    void LoadState();
    void SaveState();
    void ClearUserSession();
    bool EnsureDeviceBundle(std::string& error);
    bool EnsureRegistered(std::string& error);
    bool EnsureAuthenticated(std::string& error);
    bool RefreshResourcesIndex(std::string& error);
    bool RequestApi(const std::string& method,
                    const std::string& path_or_url,
                    const std::string& body,
                    const std::vector<std::pair<std::string, std::string>>& headers,
                    std::string& response_body,
                    std::string& error,
                    bool absolute_url = false);

    std::string DeriveApiBaseUrl() const;
    std::string GenerateAutoUsername(const std::string& phone) const;

    std::string api_base_url_{};
    std::string device_id_{};
    std::string device_pubkey_b64_{};
    std::string device_private_key_b64_{};
    std::string device_token_{};
    std::string user_token_{};
    std::string user_refresh_token_{};
    std::string username_{};
    std::string phone_{};
    std::string resource_name_{};
    std::string resource_version_{};
    std::string resource_format_{};
    std::string installed_resource_name_{};
    std::string installed_resource_version_{};
    std::string activation_status_{};
    std::string pending_auto_username_{};
    std::string last_download_path_{};
    EtServerPendingFlowMode pending_flow_mode_ = EtServerPendingFlowMode::Login;
};