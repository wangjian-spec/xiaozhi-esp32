#pragma once

#include <esp_log.h>

#ifndef DATABASE_DEBUG_ENABLE
#define DATABASE_DEBUG_ENABLE 0
#endif

#if DATABASE_DEBUG_ENABLE
#define DB_LOGE(tag, format, ...) esp_log_write(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__)
#define DB_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define DB_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define DB_LOGD(tag, format, ...) esp_log_write(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__)
#else
#define DB_LOGE(tag, format, ...) do { (void)(tag); } while (0)
#define DB_LOGW(tag, format, ...) do { (void)(tag); } while (0)
#define DB_LOGI(tag, format, ...) do { (void)(tag); } while (0)
#define DB_LOGD(tag, format, ...) do { (void)(tag); } while (0)
#endif
