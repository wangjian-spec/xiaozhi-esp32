#include "eteacher/apps/word_practice/word_practice_result_module.h"

#include <algorithm>
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

	const char *sql_runtime_state =
		"CREATE TABLE IF NOT EXISTS word_practice_runtime_state ("
		"user_id INTEGER NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"completed_rounds INTEGER DEFAULT 0,"
		"last_round_passed INTEGER DEFAULT 0,"
		"last_round_at INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, textbook_name)"
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
	if (sqlite3_exec(db, sql_runtime_state, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(log_tag_, "create word_practice_runtime_state failed: %s", err ? err : "unknown");
		if (err) {
			sqlite3_free(err);
		}
		return false;
	}
	return true;
}

DailyProgressState UserProgressDao::QueryDailyProgress(const std::string &textbook_name) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return {};
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return {};
	}
	(void)EnsureStatsTables(db);
	const DailyProgressState state = QueryDailyProgress(db, textbook_name);
	sqlite3_close(db);
	return state;
}

DailyProgressState UserProgressDao::QueryDailyProgress(sqlite3 *db, const std::string &textbook_name) const {
	DailyProgressState state;
	if (!db) {
		return state;
	}
	const char *sql =
		"SELECT completed_words, target_words, progress_percent FROM word_practice_daily_progress "
		"WHERE user_id=? AND date=? AND textbook_name=? LIMIT 1;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return state;
	}
	const std::string date = TodayDate();
	sqlite3_bind_int(stmt, 1, user_id_);
	sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		state.completed_words = std::max(0, sqlite3_column_int(stmt, 0));
		state.target_words = std::max(0, sqlite3_column_int(stmt, 1));
		state.progress_percent = std::max(0, sqlite3_column_int(stmt, 2));
	}
	sqlite3_finalize(stmt);
	if (state.target_words > 0) {
		state.progress_percent = std::max(state.progress_percent, (state.completed_words * 100) / state.target_words);
	}
	return state;
}

bool UserProgressDao::UpdateDailyProgress(const std::string &textbook_name, int completed_words, int target_words) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		WP_RESULT_LOGW(log_tag_, "update daily progress skipped: user db path missing textbook=%s", textbook_name.c_str());
		return false;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		WP_RESULT_LOGW(log_tag_, "update daily progress open db failed path=%s msg=%s", user_db.c_str(), db ? sqlite3_errmsg(db) : "null");
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}
	if (!EnsureStatsTables(db)) {
		WP_RESULT_LOGW(log_tag_, "update daily progress ensure tables failed path=%s", user_db.c_str());
		sqlite3_close(db);
		return false;
	}
	const bool ok = UpdateDailyProgress(db, textbook_name, completed_words, target_words);
	WP_RESULT_LOGI(log_tag_,
		"update daily progress path=%s textbook=%s completed=%d target=%d ok=%d",
		user_db.c_str(),
		textbook_name.c_str(),
		completed_words,
		target_words,
		ok ? 1 : 0);
	sqlite3_close(db);
	return ok;
}

bool UserProgressDao::UpdateDailyProgress(sqlite3 *db, const std::string &textbook_name, int completed_words, int target_words) const {
	if (!db) {
		return false;
	}
	const std::string date = TodayDate();
	const DailyProgressState existing = QueryDailyProgress(db, textbook_name);
	const int normalized_target = std::max(0, target_words);
	const int normalized_completed = std::max(existing.completed_words, std::max(0, completed_words));
	const int progress_percent = normalized_target <= 0 ? 0 : (normalized_completed * 100) / normalized_target;
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
	sqlite3_bind_int(stmt, 3, progress_percent);
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
	sqlite3_bind_int(stmt, 6, progress_percent);
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
	sqlite3_bind_int(stmt, 2, correct ? 0 : 1);
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
		sqlite3_bind_int(stmt, 5, correct ? 0 : 1);
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
	sqlite3_bind_int(stmt, 2, correct ? 0 : 1);
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
		sqlite3_bind_int(stmt, 5, correct ? 0 : 1);
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
		"save answer stats staged word_id=%d textbook=%s qtype=%d correct=%d round_finished=%d round_passed=%d",
		word_id,
		textbook_name.c_str(),
		question.type,
		correct ? 1 : 0,
		round_finished ? 1 : 0,
		round_passed ? 1 : 0);
	return true;
}

void UserProgressDao::SaveAnswerStats(const SessionModule &session,
				   int word_id,
						   const QuestionData &question,
						   const std::string &textbook_name,
						   bool correct,
						   bool round_finished,
						   bool round_passed) const {
	(void)session;
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		WP_RESULT_LOGW(log_tag_, "save answer stats skipped: user db path missing word_id=%d textbook=%s", word_id, textbook_name.c_str());
		return;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		WP_RESULT_LOGW(log_tag_, "save answer stats open db failed path=%s msg=%s", user_db.c_str(), db ? sqlite3_errmsg(db) : "null");
		if (db) {
			sqlite3_close(db);
		}
		return;
	}
	DB_LOGI(log_tag_, "RESOURCE_OK kind=db scope=user action=open path=%s method=sqlite3_open_v2(READWRITE|CREATE) caller=SaveAnswerStats", user_db.c_str());
	const int total_changes_before = sqlite3_total_changes(db);

	if (!EnsureStatsTables(db) || !eteacher::database_manager::ConfigureWriteConnection(db, log_tag_)) {
		WP_RESULT_LOGW(log_tag_, "save answer stats setup failed path=%s word_id=%d textbook=%s", user_db.c_str(), word_id, textbook_name.c_str());
		sqlite3_close(db);
		return;
	}

	if (!eteacher::database_manager::BeginTransaction(db, log_tag_)) {
		WP_RESULT_LOGW(log_tag_, "save answer stats begin transaction failed path=%s word_id=%d textbook=%s", user_db.c_str(), word_id, textbook_name.c_str());
		sqlite3_close(db);
		return;
	}

	if (!SaveAnswerStats(db, session, word_id, question, textbook_name, correct, round_finished, round_passed)) {
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
		sqlite3_close(db);
		return;
	}

	if (!eteacher::database_manager::CommitTransaction(db, log_tag_)) {
		WP_RESULT_LOGW(log_tag_, "save answer stats commit failed path=%s word_id=%d textbook=%s", user_db.c_str(), word_id, textbook_name.c_str());
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
	} else {
		WP_RESULT_LOGI(log_tag_,
			"save answer stats committed path=%s word_id=%d textbook=%s qtype=%d correct=%d round_finished=%d round_passed=%d total_changes=%d",
			user_db.c_str(),
			word_id,
			textbook_name.c_str(),
			question.type,
			correct ? 1 : 0,
			round_finished ? 1 : 0,
			round_passed ? 1 : 0,
			sqlite3_total_changes(db) - total_changes_before);
	}
	sqlite3_close(db);
}

ResultModule::ResultModule(const char *log_tag, int user_id)
	: progress_dao_(log_tag, user_id) {
}

void ResultModule::SetUserId(int user_id) {
	progress_dao_.SetUserId(user_id);
}

const UserProgressDao &ResultModule::ProgressDao() const {
	return progress_dao_;
}

}  // namespace word_practice
