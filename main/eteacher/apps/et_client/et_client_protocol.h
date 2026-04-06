#pragma once

#include <string>

namespace et_client {

inline constexpr const char* kRegisterChallengePath = "/api/v1/device/register/challenge";
inline constexpr const char* kRegisterVerifyPath = "/api/v1/device/register/verify";
inline constexpr const char* kAuthChallengePath = "/api/v1/device/auth/challenge";
inline constexpr const char* kAuthVerifyPath = "/api/v1/device/auth/verify";
inline constexpr const char* kUserRegisterCodeSendPath = "/api/v1/user/register/code/send";
inline constexpr const char* kUserLoginCodeSendPath = "/api/v1/user/login/code/send";
inline constexpr const char* kUserRegisterPath = "/api/v1/user/register";
inline constexpr const char* kUserLoginPath = "/api/v1/user/login";
inline constexpr const char* kUserRefreshPath = "/api/v1/user/refresh";
inline constexpr const char* kUserLogoutPath = "/api/v1/user/logout";

inline std::string MakeSigningMessage(const char* prefix, const std::string& challenge) {
    return std::string(prefix) + ":" + challenge;
}

}  // namespace et_client
