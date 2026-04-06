#pragma once

#include <string>

#include <cJSON.h>

struct EtHttpResult {
    bool ok = false;
    int status_code = 0;
    std::string body;
    std::string error_code;
    std::string error_message;
};

class EtClientHttp {
public:
    static std::string GetBaseUrl();
    static std::string BuildUrl(const std::string& path);

    static EtHttpResult PostJson(const std::string& path,
                                 cJSON* body,
                                 const std::string& authorization = "");
};
