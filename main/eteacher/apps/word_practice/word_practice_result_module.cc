#include "eteacher/apps/word_practice/word_practice_result_module.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "eteacher/apps/word_practice/word_practice_db_utils.h"
#include "eteacher/apps/word_practice/word_practice_time_utils.h"
#include "eteacher/database_manager/database_debug.h"
#include "eteacher/database_manager/sqlite_db_api.h"

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#define ESP_LOGE DB_LOGE
#define ESP_LOGW DB_LOGW
#define ESP_LOGI DB_LOGI
#define ESP_LOGD DB_LOGD

#define WP_RESULT_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define WP_RESULT_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)

namespace word_practice {
namespace {

int64_t NowSec() {
	return CurrentPersistentEpochSeconds();
}

std::string TodayDate() {
	return CurrentCalendarDateString();
}

}  // namespace

UserProgressDao::UserProgressDao(const char *log_tag, int user_id)
	: log_tag_(log_tag),
	  user_id_(std::max(0, user_id)) {
}

void UserProgressDao::SetUserId(int user_id) {
	user_id_ = std::max(0, user_id);
}

std::string UserProgressDao::DiscoverUserDbPath() const {
	return eteacher::database_manager::DiscoverUserDataDbPath(log_tag_, nullptr);
}

bool UserProgressDao::EnsureStatsTables(sqlite3 *db) const {
	if (!db) {
		return false;
	}
	const char *sql_learned =
		"CREATE TABLE IF NOT EXISTS learned ("
		"user_id INTEGER NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"word_id INTEGER NOT NULL,"
		"correct_count INTEGER DEFAULT 0,"
		"wrong_count INTEGER DEFAULT 0,"
		"last_seen_at INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, textbook_name, word_id)"
		");";

	const char *sql_stats =
		"CREATE TABLE IF NOT EXISTS word_practice_stats_daily ("
		"user_id INTEGER NOT NULL,"
		"date TEXT NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"total_count INTEGER DEFAULT 0,"
		"correct_count INTEGER DEFAULT 0,"
		"wrong_count INTEGER DEFAULT 0,"
		"pass_count INTEGER DEFAULT 0,"
		"fail_count INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, date, textbook_name)"
		");";

	const char *sql_daily_progress =
		"CREATE TABLE IF NOT EXISTS word_practice_daily_progress ("
		"user_id INTEGER NOT NULL,"
		"date TEXT NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"completed_words INTEGER DEFAULT 0,"
		"target_words INTEGER DEFAULT 0,"
		"progress_percent INTEGER DEFAULT 0,"
		"updated_at INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, date, textbook_name)"
		");";

	const char *sql_daily_completed_words =
		"CREATE TABLE IF NOT EXISTS word_practice_daily_completed_words ("
		"user_id INTEGER NOT NULL,"
		"date TEXT NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"word_id INTEGER NOT NULL,"
		"completed_at INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, date, textbook_name, word_id)"
		");";

	const char *sql_runtime_state =
		"CREATE TABLE IF NOT EXISTS word_practice_runtime_state ("
		"user_id INTEGER NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"completed_rounds INTEGER DEFAULT 0,"
		"last_round_passed INTEGER DEFAULT 0,"
		"last_round_at INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, textbook_name)"
		");";

	const char *sql_app_state =
		"CREATE TABLE IF NOT EXISTS app_state ("
		"user_id INTEGER NOT NULL,"
		"key TEXT NOT NULL,"
		"value TEXT NOT NULL,"
		"updated_at INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, key)"
		");";

	char *err = nullptr;
	if (sqlite3_exec(db, sql_learned, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(log_tag_, "create learned failed: %s", err ? err : "unknown");
		if (err) {
			sqlite3_free(err);
		}
		return false;
	}
	if (sqlite3_exec(db, sql_stats, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(log_tag_, "create word_practice_stats_daily failed: %s", err ? err : "unknown");
		if (err) {
			sqlite3_free(err);
		}
		return false;
	}
	if (sqlite3_exec(db, sql_daily_progress, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(log_tag_, "create word_practice_daily_progress failed: %s", err ? err : "unknown");
		if (err) {
			sqlite3_free(err);
		}
		return false;
	}
	if (sqlite3_exec(db, sql_daily_completed_words, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(log_tag_, "create word_practice_daily_completed_words failed: %s", err ? err : "unknown");
		if (err) {
			sqlite3_free(err);
		}
		return false;
	}
	if (sqlite3_exec(db, sql_runtime_state, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(log_tag_, "create word_practice_runtime_state failed: %s", err ? err : "unknown");
		if (err) {
			sqlite3_free(err);
		}
		return false;
	}
	if (sqlite3_exec(db, sql_app_state, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(log_tag_, "create app_state failed: %s", err ? err : "unknown");
		if (err) {
			sqlite3_free(err);
		}
		return false;
	}
	return true;
}

int UserProgressDao::QueryAppStateInt(const std::string &key, int fallback_value) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return fallback_value;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return fallback_value;
	}
	if (!EnsureStatsTables(db)) {
		sqlite3_close(db);
		return fallback_value;
	}
	const int value = QueryAppStateInt(db, key, fallback_value);
	sqlite3_close(db);
	return value;
}

int UserProgressDao::QueryAppStateInt(sqlite3 *db, const std::string &key, int fallback_value) const {
	if (!db || key.empty()) {
		return fallback_value;
	}
	const char *sql = "SELECT value FROM app_state WHERE user_id=? AND key=? LIMIT 1;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return fallback_value;
	}
	sqlite3_bind_int(stmt, 1, user_id_);
	sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
	int value = fallback_value;
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char *text = sqlite3_column_text(stmt, 0);
		if (text != nullptr) {
			value = std::atoi(reinterpret_cast<const char *>(text));
		}
	}
	sqlite3_finalize(stmt);
	return value;
}

bool UserProgressDao::SaveAppStateInt(const std::string &key, int value) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return false;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}
	if (!EnsureStatsTables(db) || !eteacher::database_manager::ConfigureWriteConnection(db, log_tag_)) {
		sqlite3_close(db);
		return false;
	}
	if (!eteacher::database_manager::BeginTransaction(db, log_tag_)) {
		sqlite3_close(db);
		return false;
	}
	const bool ok = SaveAppStateInt(db, key, value);
	if (!ok || !eteacher::database_manager::CommitTransaction(db, log_tag_)) {
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
		sqlite3_close(db);
		return false;
	}
	sqlite3_close(db);
	return true;
}

bool UserProgressDao::SaveAppStateInt(sqlite3 *db, const std::string &key, int value) const {
	return SaveAppStateText(db, key, std::to_string(value));
}

std::string UserProgressDao::QueryAppStateText(const std::string &key, const std::string &fallback_value) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return fallback_value;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return fallback_value;
	}
	if (!EnsureStatsTables(db)) {
		sqlite3_close(db);
		return fallback_value;
	}
	const std::string value = QueryAppStateText(db, key, fallback_value);
	sqlite3_close(db);
	return value;
}

std::string UserProgressDao::QueryAppStateText(sqlite3 *db, const std::string &key, const std::string &fallback_value) const {
	if (!db || key.empty()) {
		return fallback_value;
	}
	const char *sql = "SELECT value FROM app_state WHERE user_id=? AND key=? LIMIT 1;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return fallback_value;
	}
	sqlite3_bind_int(stmt, 1, user_id_);
	sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
	std::string value = fallback_value;
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char *text = sqlite3_column_text(stmt, 0);
		if (text != nullptr) {
			value = reinterpret_cast<const char *>(text);
		}
	}
	sqlite3_finalize(stmt);
	return value;
	}

bool UserProgressDao::SaveAppStateText(const std::string &key, const std::string &value) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return false;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}
	if (!EnsureStatsTables(db) || !eteacher::database_manager::ConfigureWriteConnection(db, log_tag_)) {
		sqlite3_close(db);
		return false;
	}
	if (!eteacher::database_manager::BeginTransaction(db, log_tag_)) {
		sqlite3_close(db);
		return false;
	}
	const bool ok = SaveAppStateText(db, key, value);
	if (!ok || !eteacher::database_manager::CommitTransaction(db, log_tag_)) {
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
		sqlite3_close(db);
		return false;
	}
	sqlite3_close(db);
	return true;
}

bool UserProgressDao::SaveAppStateText(sqlite3 *db, const std::string &key, const std::string &value) const {
	if (!db || key.empty()) {
		return false;
	}
	const char *update_sql =
		"UPDATE app_state SET value=?, updated_at=? WHERE user_id=? AND key=?;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return false;
	}
	const int64_t now_sec = NowSec();
	sqlite3_bind_text(stmt, 1, value.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 2, now_sec);
	sqlite3_bind_int(stmt, 3, user_id_);
	sqlite3_bind_text(stmt, 4, key.c_str(), -1, SQLITE_TRANSIENT);
	if (!word_practice::db::StepDone(stmt)) {
		sqlite3_finalize(stmt);
		return false;
	}
	sqlite3_finalize(stmt);
	if (sqlite3_changes(db) > 0) {
		return true;
	}

	const char *insert_sql =
		"INSERT INTO app_state(user_id, key, value, updated_at) VALUES(?, ?, ?, ?);";
	if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return false;
	}
	sqlite3_bind_int(stmt, 1, user_id_);
	sqlite3_bind_text(stmt, 2, key.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, value.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 4, now_sec);
	const bool ok = word_practice::db::StepDone(stmt);
	sqlite3_finalize(stmt);
	return ok;
}

bool UserProgressDao::RecordDailyCompletedWord(sqlite3 *db, const std::string &textbook_name, int word_id) const {
	if (!db || word_id <= 0) {
		return false;
	}
	const char *insert_sql =
		"INSERT OR IGNORE INTO word_practice_daily_completed_words(user_id, date, textbook_name, word_id, completed_at) "
		"VALUES(?, ?, ?, ?, ?);";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		WP_RESULT_LOGW(log_tag_, "record daily completed word prepare failed textbook=%s word_id=%d msg=%s", textbook_name.c_str(), word_id, sqlite3_errmsg(db));
		return false;
	}
	const std::string date = TodayDate();
	sqlite3_bind_int(stmt, 1, user_id_);
	sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, word_id);
	sqlite3_bind_int64(stmt, 5, NowSec());
	const bool ok = word_practice::db::StepDone(stmt);
	if (!ok) {
		WP_RESULT_LOGW(log_tag_, "record daily completed word step failed textbook=%s word_id=%d msg=%s", textbook_name.c_str(), word_id, sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return ok;
}

bool UserProgressDao::UpdateDailyProgress(sqlite3 *db, const std::string &textbook_name, int completed_words, int target_words, int progress_percent, int completed_word_id) const {
	if (!db) {
		return false;
	}
	const std::string date = TodayDate();
	const int normalized_target = std::max(0, target_words);
	const int normalized_completed = std::max(0, completed_words);
	if (completed_word_id > 0) {
		if (!RecordDailyCompletedWord(db, textbook_name, completed_word_id)) {
			return false;
		}
	}
	const int normalized_progress = std::max(0, progress_percent);
	const int64_t now_sec = NowSec();

	const char *update_sql =
		"UPDATE word_practice_daily_progress SET completed_words=?, target_words=?, progress_percent=?, updated_at=? "
		"WHERE user_id=? AND date=? AND textbook_name=?;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		WP_RESULT_LOGW(log_tag_, "update daily progress prepare update failed textbook=%s msg=%s", textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt, 1, normalized_completed);
	sqlite3_bind_int(stmt, 2, normalized_target);
	sqlite3_bind_int(stmt, 3, normalized_progress);
	sqlite3_bind_int64(stmt, 4, now_sec);
	sqlite3_bind_int(stmt, 5, user_id_);
	sqlite3_bind_text(stmt, 6, date.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 7, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	if (!word_practice::db::StepDone(stmt)) {
		WP_RESULT_LOGW(log_tag_, "update daily progress update failed textbook=%s msg=%s", textbook_name.c_str(), sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return false;
	}
	sqlite3_finalize(stmt);
	if (sqlite3_changes(db) > 0) {
		return true;
	}

	const char *insert_sql =
		"INSERT INTO word_practice_daily_progress(user_id, date, textbook_name, completed_words, target_words, progress_percent, updated_at) "
		"VALUES(?, ?, ?, ?, ?, ?, ?);";
	if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		WP_RESULT_LOGW(log_tag_, "update daily progress prepare insert failed textbook=%s msg=%s", textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt, 1, user_id_);
	sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, normalized_completed);
	sqlite3_bind_int(stmt, 5, normalized_target);
	sqlite3_bind_int(stmt, 6, normalized_progress);
	sqlite3_bind_int64(stmt, 7, now_sec);
	const bool ok = word_practice::db::StepDone(stmt);
	if (!ok) {
		WP_RESULT_LOGW(log_tag_, "update daily progress insert failed textbook=%s msg=%s", textbook_name.c_str(), sqlite3_errmsg(db));
	}
	sqlite3_finalize(stmt);
	return ok;
}
bool UserProgressDao::RecordRoundCompletion(const std::string &textbook_name, bool passed) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return false;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}
	if (!EnsureStatsTables(db)) {
		sqlite3_close(db);
		return false;
	}
	const bool ok = RecordRoundCompletion(db, textbook_name, passed);
	sqlite3_close(db);
	return ok;
}

bool UserProgressDao::RecordRoundCompletion(sqlite3 *db, const std::string &textbook_name, bool passed) const {
	if (!db) {
		return false;
	}
	const int64_t now_sec = NowSec();
	sqlite3_stmt *stmt = nullptr;
	const char *update_sql =
		"UPDATE word_practice_runtime_state SET completed_rounds = completed_rounds + 1, last_round_passed = ?, last_round_at = ? "
		"WHERE user_id = ? AND textbook_name = ?;";
	if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		WP_RESULT_LOGW(log_tag_,
			"record round completion prepare update failed textbook=%s passed=%d msg=%s",
			textbook_name.c_str(),
			passed ? 1 : 0,
			sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt, 1, passed ? 1 : 0);
	sqlite3_bind_int64(stmt, 2, now_sec);
	sqlite3_bind_int(stmt, 3, user_id_);
	sqlite3_bind_text(stmt, 4, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	if (!word_practice::db::StepDone(stmt)) {
		WP_RESULT_LOGW(log_tag_,
			"record round completion update failed textbook=%s passed=%d msg=%s",
			textbook_name.c_str(),
			passed ? 1 : 0,
			sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return false;
	}
	sqlite3_finalize(stmt);
	if (sqlite3_changes(db) > 0) {
		WP_RESULT_LOGI(log_tag_,
			"record round completion updated textbook=%s passed=%d",
			textbook_name.c_str(),
			passed ? 1 : 0);
		return true;
	}

	const char *insert_sql =
		"INSERT INTO word_practice_runtime_state(user_id, textbook_name, completed_rounds, last_round_passed, last_round_at) "
		"VALUES(?, ?, 1, ?, ?);";
	if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		WP_RESULT_LOGW(log_tag_,
			"record round completion prepare insert failed textbook=%s passed=%d msg=%s",
			textbook_name.c_str(),
			passed ? 1 : 0,
			sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt, 1, user_id_);
	sqlite3_bind_text(stmt, 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, passed ? 1 : 0);
	sqlite3_bind_int64(stmt, 4, now_sec);
	if (!word_practice::db::StepDone(stmt)) {
		WP_RESULT_LOGW(log_tag_,
			"record round completion insert failed textbook=%s passed=%d msg=%s",
			textbook_name.c_str(),
			passed ? 1 : 0,
			sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return false;
	}
	sqlite3_finalize(stmt);
	WP_RESULT_LOGI(log_tag_,
		"record round completion inserted textbook=%s passed=%d",
		textbook_name.c_str(),
		passed ? 1 : 0);
	return true;
}

bool UserProgressDao::SaveAnswerStats(sqlite3 *db,
			      const SessionModule &session,
			      int word_id,
			      const QuestionData &question,
			      const std::string &textbook_name,
			      bool correct,
			      bool skipped,
			      bool round_finished,
			      bool round_passed) const {
	(void)session;
	if (!db) {
		return false;
	}
	if (!EnsureStatsTables(db)) {
		WP_RESULT_LOGW(log_tag_, "save answer stats setup failed word_id=%d textbook=%s", word_id, textbook_name.c_str());
		return false;
	}

	const int64_t now_sec = NowSec();
	sqlite3_stmt *stmt = nullptr;
	const char *sql_learned =
		"UPDATE learned SET correct_count = correct_count + ?, wrong_count = wrong_count + ?, last_seen_at = ? "
		"WHERE user_id = ? AND textbook_name = ? AND word_id = ?;";
	if (sqlite3_prepare_v2(db, sql_learned, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		WP_RESULT_LOGW(log_tag_, "save answer stats prepare learned failed word_id=%d textbook=%s msg=%s", word_id, textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt, 1, correct ? 1 : 0);
	sqlite3_bind_int(stmt, 2, (!correct && !skipped) ? 1 : 0);
	sqlite3_bind_int64(stmt, 3, now_sec);
	sqlite3_bind_int(stmt, 4, user_id_);
	sqlite3_bind_text(stmt, 5, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 6, word_id);
	if (!word_practice::db::StepDone(stmt)) {
		WP_RESULT_LOGW(log_tag_, "save answer stats step learned failed word_id=%d textbook=%s msg=%s", word_id, textbook_name.c_str(), sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return false;
	}
	sqlite3_finalize(stmt);
	if (sqlite3_changes(db) == 0) {
		const char *insert_learned_sql =
			"INSERT INTO learned(user_id, textbook_name, word_id, correct_count, wrong_count, last_seen_at) "
			"VALUES(?, ?, ?, ?, ?, ?);";
		if (sqlite3_prepare_v2(db, insert_learned_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
			WP_RESULT_LOGW(log_tag_, "save answer stats prepare learned insert failed word_id=%d textbook=%s msg=%s", word_id, textbook_name.c_str(), sqlite3_errmsg(db));
			return false;
		}
		sqlite3_bind_int(stmt, 1, user_id_);
		sqlite3_bind_text(stmt, 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, 3, word_id);
		sqlite3_bind_int(stmt, 4, correct ? 1 : 0);
		sqlite3_bind_int(stmt, 5, (!correct && !skipped) ? 1 : 0);
		sqlite3_bind_int64(stmt, 6, now_sec);
		if (!word_practice::db::StepDone(stmt)) {
			WP_RESULT_LOGW(log_tag_, "save answer stats step learned insert failed word_id=%d textbook=%s msg=%s", word_id, textbook_name.c_str(), sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			return false;
		}
		sqlite3_finalize(stmt);
	}

	const char *sql_daily =
		"UPDATE word_practice_stats_daily SET total_count = total_count + 1, correct_count = correct_count + ?, wrong_count = wrong_count + ?, pass_count = pass_count + ?, fail_count = fail_count + ? "
		"WHERE user_id = ? AND date = ? AND textbook_name = ?;";
	if (sqlite3_prepare_v2(db, sql_daily, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		WP_RESULT_LOGW(log_tag_, "save answer stats prepare daily failed word_id=%d textbook=%s msg=%s", word_id, textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	const std::string date = TodayDate();
	const int pass_flag = round_finished && round_passed ? 1 : 0;
	const int fail_flag = round_finished && !round_passed ? 1 : 0;
	sqlite3_bind_int(stmt, 1, correct ? 1 : 0);
	sqlite3_bind_int(stmt, 2, (!correct && !skipped) ? 1 : 0);
	sqlite3_bind_int(stmt, 3, pass_flag);
	sqlite3_bind_int(stmt, 4, fail_flag);
	sqlite3_bind_int(stmt, 5, user_id_);
	sqlite3_bind_text(stmt, 6, date.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 7, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	if (!word_practice::db::StepDone(stmt)) {
		WP_RESULT_LOGW(log_tag_, "save answer stats step daily failed word_id=%d textbook=%s msg=%s", word_id, textbook_name.c_str(), sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return false;
	}
	sqlite3_finalize(stmt);
	if (sqlite3_changes(db) == 0) {
		const char *insert_daily_sql =
			"INSERT INTO word_practice_stats_daily(user_id, date, textbook_name, total_count, correct_count, wrong_count, pass_count, fail_count) "
			"VALUES(?, ?, ?, 1, ?, ?, ?, ?);";
		if (sqlite3_prepare_v2(db, insert_daily_sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
			WP_RESULT_LOGW(log_tag_, "save answer stats prepare daily insert failed word_id=%d textbook=%s msg=%s", word_id, textbook_name.c_str(), sqlite3_errmsg(db));
			return false;
		}
		sqlite3_bind_int(stmt, 1, user_id_);
		sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, 4, correct ? 1 : 0);
		sqlite3_bind_int(stmt, 5, (!correct && !skipped) ? 1 : 0);
		sqlite3_bind_int(stmt, 6, pass_flag);
		sqlite3_bind_int(stmt, 7, fail_flag);
		if (!word_practice::db::StepDone(stmt)) {
			WP_RESULT_LOGW(log_tag_, "save answer stats step daily insert failed word_id=%d textbook=%s msg=%s", word_id, textbook_name.c_str(), sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			return false;
		}
		sqlite3_finalize(stmt);
	}

	WP_RESULT_LOGI(log_tag_,
		"save answer stats staged word_id=%d textbook=%s qtype=%d correct=%d skipped=%d round_finished=%d round_passed=%d",
		word_id,
		textbook_name.c_str(),
		question.type,
		correct ? 1 : 0,
		skipped ? 1 : 0,
		round_finished ? 1 : 0,
		round_passed ? 1 : 0);
	return true;
}
}  // namespace word_practice
