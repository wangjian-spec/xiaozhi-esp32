#pragma once

#include <stdbool.h>
#include <esp_log.h>

#ifndef DATABASE_DEBUG
#define DATABASE_DEBUG 1
#endif

#ifndef DATABASE_DEBUG_LOG_ENABLED_DEFAULT
#define DATABASE_DEBUG_LOG_ENABLED_DEFAULT 0
#endif

#if DATABASE_DEBUG

namespace eteacher::database_manager::debug {

inline bool g_database_debug_logging_enabled = DATABASE_DEBUG_LOG_ENABLED_DEFAULT != 0;

inline bool DatabaseDebugLoggingEnabled() {
	return g_database_debug_logging_enabled;
}

inline void SetDatabaseDebugLoggingEnabled(bool enabled) {
	g_database_debug_logging_enabled = enabled;
}

}  // namespace eteacher::database_manager::debug

#define DB_LOGE(tag, format, ...)                                                                                  \
	do {                                                                                                             \
		if (eteacher::database_manager::debug::DatabaseDebugLoggingEnabled()) {                                        \
			esp_log_write(ESP_LOG_ERROR, tag, format, ##__VA_ARGS__);                                                   \
		}                                                                                                              \
	} while (0)
#define DB_LOGW(tag, format, ...)                                                                                  \
	do {                                                                                                             \
		if (eteacher::database_manager::debug::DatabaseDebugLoggingEnabled()) {                                        \
			esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__);                                                    \
		}                                                                                                              \
	} while (0)
#define DB_LOGI(tag, format, ...)                                                                                  \
	do {                                                                                                             \
		if (eteacher::database_manager::debug::DatabaseDebugLoggingEnabled()) {                                        \
			esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__);                                                    \
		}                                                                                                              \
	} while (0)
#define DB_LOGD(tag, format, ...)                                                                                  \
	do {                                                                                                             \
		if (eteacher::database_manager::debug::DatabaseDebugLoggingEnabled()) {                                        \
			esp_log_write(ESP_LOG_DEBUG, tag, format, ##__VA_ARGS__);                                                   \
		}                                                                                                              \
	} while (0)
#else
#define DatabaseDebugLoggingEnabled() false
#define DB_LOGE(tag, format, ...) do { (void)(tag); } while (0)
#define DB_LOGW(tag, format, ...) do { (void)(tag); } while (0)
#define DB_LOGI(tag, format, ...) do { (void)(tag); } while (0)
#define DB_LOGD(tag, format, ...) do { (void)(tag); } while (0)
#endif
