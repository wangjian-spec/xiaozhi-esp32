#include "eteacher/apps/et_client/et_client_http.h"

#include "board.h"
#include "settings.h"

namespace {

std::string TrimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

EtHttpResult MakeErrorResult(const std::string& code,
                             const std::string& message,
                             int status_code,
                             std::string body = {}) {
    EtHttpResult result;
    result.ok = false;
    result.status_code = status_code;
    result.body = std::move(body);
    result.error_code = code;
    result.error_message = message;
    return result;
}

void FillErrorFromBody(EtHttpResult& result) {
    if (result.body.empty()) {
        if (result.error_code.empty()) {
            result.error_code = "HTTP_ERROR";
        }
        if (result.error_message.empty()) {
            result.error_message = "HTTP request failed";
        }
        return;
    }

    cJSON* root = cJSON_Parse(result.body.c_str());
    if (root == nullptr) {
        if (result.error_code.empty()) {
            result.error_code = "HTTP_ERROR";
        }
        if (result.error_message.empty()) {
            result.error_message = result.body;
        }
        return;
    }

    cJSON* code = cJSON_GetObjectItem(root, "code");
    cJSON* message = cJSON_GetObjectItem(root, "message");
    if (result.error_code.empty() && cJSON_IsString(code)) {
        result.error_code = code->valuestring;
    }
    if (result.error_message.empty() && cJSON_IsString(message)) {
        result.error_message = message->valuestring;
    }
    if (result.error_code.empty()) {
        result.error_code = "HTTP_ERROR";
    }
    if (result.error_message.empty()) {
        result.error_message = result.body;
    }
    cJSON_Delete(root);
}

}  // namespace

std::string EtClientHttp::GetBaseUrl() {
    Settings settings("et_client", false);
    std::string url = settings.GetString("base_url");
    if (url.empty()) {
        url = CONFIG_ET_SERVER_BASE_URL;
    }
    return TrimTrailingSlash(url);
}

std::string EtClientHttp::BuildUrl(const std::string& path) {
    return GetBaseUrl() + path;
}

EtHttpResult EtClientHttp::PostJson(const std::string& path,
                                    cJSON* body,
                                    const std::string& authorization) {
    if (body == nullptr) {
        return MakeErrorResult("HTTP_INVALID_REQUEST", "JSON body is null", 0);
    }

    char* json_str = cJSON_PrintUnformatted(body);
    if (json_str == nullptr) {
        return MakeErrorResult("HTTP_INVALID_REQUEST", "Failed to serialize JSON body", 0);
    }

    std::string payload(json_str);
    cJSON_free(json_str);

    auto http = Board::GetInstance().GetNetwork()->CreateHttp(0);
    if (http == nullptr) {
        return MakeErrorResult("HTTP_TRANSPORT_ERROR", "Failed to create HTTP client", 0);
    }

    http->SetTimeout(20000);
    http->SetHeader("Content-Type", "application/json");
    http->SetHeader("Accept", "application/json");
    if (!authorization.empty()) {
        http->SetHeader("Authorization", authorization);
    }
    http->SetContent(std::move(payload));

    const std::string url = BuildUrl(path);
    if (!http->Open("POST", url)) {
        return MakeErrorResult("HTTP_TRANSPORT_ERROR",
                               "Failed to open HTTP connection",
                               http->GetLastError());
    }

    EtHttpResult result;
    result.status_code = http->GetStatusCode();
    result.body = http->ReadAll();
    http->Close();

    if (result.status_code >= 200 && result.status_code < 300) {
        result.ok = true;
        return result;
    }

    result.ok = false;
    FillErrorFromBody(result);
    return result;
}
