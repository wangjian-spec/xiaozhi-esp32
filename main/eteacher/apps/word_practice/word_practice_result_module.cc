#include "eteacher/apps/word_practice/word_practice_result_module.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "esp_timer.h"
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

namespace word_practice {
namespace {

constexpr int kDefaultUserId = 0;

int64_t NowSec() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000000ULL);
}

std::string TodayDate() {
	const int64_t now = NowSec();
	const int day = static_cast<int>((now / 86400) % 3650);
	const int year = 2024 + day / 365;
	const int rem = day % 365;
	const int month = 1 + (rem / 30);
	const int d = 1 + (rem % 30);

	char buf[16] = {0};
	std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, d);
	return buf;
}

bool StepDone(sqlite3_stmt *stmt) {
	const int rc = sqlite3_step(stmt);
	return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

}  // namespace

UserProgressDao::UserProgressDao(const char *log_tag)
	: log_tag_(log_tag) {
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
		"question_index INTEGER NOT NULL,"
		"correct_count INTEGER DEFAULT 0,"
		"wrong_count INTEGER DEFAULT 0,"
		"last_seen_at INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, textbook_name, question_index)"
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
	return true;
}

int UserProgressDao::QueryCurrentLevel() const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return 1;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return 1;
	}
	DB_LOGI(log_tag_, "RESOURCE_OK kind=db scope=user action=open path=%s method=sqlite3_open_v2(READWRITE|CREATE) caller=QueryCurrentLevel", user_db.c_str());

	(void)EnsureStatsTables(db);
	const int level = QueryCurrentLevel(db);
	sqlite3_close(db);
	return level;
}

int UserProgressDao::QueryCurrentLevel(sqlite3 *db) const {
	if (!db) {
		return 1;
	}
	const char *sql = "SELECT level FROM users WHERE id=? LIMIT 1;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return 1;
	}
	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	int level = 1;
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		level = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);
	return (level <= 0) ? 1 : level;
}

LearnedSnapshot UserProgressDao::QueryLearned(int question_id, const std::string &textbook) const {
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
	DB_LOGI(log_tag_, "RESOURCE_OK kind=db scope=user action=open path=%s method=sqlite3_open_v2(READWRITE|CREATE) caller=QueryLearned", user_db.c_str());

	(void)EnsureStatsTables(db);
	const LearnedSnapshot snapshot = QueryLearned(db, question_id, textbook);
	sqlite3_close(db);
	return snapshot;
}

LearnedSnapshot UserProgressDao::QueryLearned(sqlite3 *db, int question_id, const std::string &textbook) const {
	LearnedSnapshot snapshot;
	if (!db) {
		return snapshot;
	}
	const char *sql =
		"SELECT correct_count, wrong_count, last_seen_at FROM learned "
		"WHERE user_id=? AND textbook_name=? AND question_index=? LIMIT 1;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return snapshot;
	}
	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	sqlite3_bind_text(stmt, 2, textbook.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, question_id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		snapshot.correct = sqlite3_column_int(stmt, 0);
		snapshot.wrong = sqlite3_column_int(stmt, 1);
		snapshot.last_seen_at = sqlite3_column_int64(stmt, 2);
	}
	sqlite3_finalize(stmt);
	return snapshot;
}

void UserProgressDao::SaveAnswerStats(const SessionModule &session,
						   const QuestionData &question,
						   const std::string &textbook_name,
						   bool correct) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return;
	}
	DB_LOGI(log_tag_, "RESOURCE_OK kind=db scope=user action=open path=%s method=sqlite3_open_v2(READWRITE|CREATE) caller=SaveAnswerStats", user_db.c_str());

	if (!EnsureStatsTables(db) || !eteacher::database_manager::ConfigureWriteConnection(db, log_tag_)) {
		sqlite3_close(db);
		return;
	}

	if (!eteacher::database_manager::BeginTransaction(db, log_tag_)) {
		sqlite3_close(db);
		return;
	}

	const char *sql_learned =
		"INSERT INTO learned(user_id, textbook_name, question_index, correct_count, wrong_count, last_seen_at) "
		"VALUES(?, ?, ?, ?, ?, ?) "
		"ON CONFLICT(user_id, textbook_name, question_index) DO UPDATE SET "
		"correct_count = correct_count + excluded.correct_count, "
		"wrong_count = wrong_count + excluded.wrong_count, "
		"last_seen_at = excluded.last_seen_at;";

	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql_learned, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
		sqlite3_close(db);
		return;
	}
	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	sqlite3_bind_text(stmt, 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, question.id);
	sqlite3_bind_int(stmt, 4, correct ? 1 : 0);
	sqlite3_bind_int(stmt, 5, correct ? 0 : 1);
	sqlite3_bind_int64(stmt, 6, NowSec());
	if (!StepDone(stmt)) {
		sqlite3_finalize(stmt);
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
		sqlite3_close(db);
		return;
	}
	sqlite3_finalize(stmt);

	const char *sql_daily =
		"INSERT INTO word_practice_stats_daily(user_id, date, textbook_name, total_count, correct_count, wrong_count, pass_count, fail_count) "
		"VALUES(?, ?, ?, 1, ?, ?, ?, ?) "
		"ON CONFLICT(user_id, date, textbook_name) DO UPDATE SET "
		"total_count = total_count + 1, "
		"correct_count = correct_count + excluded.correct_count, "
		"wrong_count = wrong_count + excluded.wrong_count, "
		"pass_count = pass_count + excluded.pass_count, "
		"fail_count = fail_count + excluded.fail_count;";

	if (sqlite3_prepare_v2(db, sql_daily, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
		sqlite3_close(db);
		return;
	}
	const std::string date = TodayDate();
	const bool passed = ResultModule(log_tag_).IsPassed(session);
	const int pass_flag = passed ? 1 : 0;
	const int fail_flag = passed ? 0 : 1;
	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, correct ? 1 : 0);
	sqlite3_bind_int(stmt, 5, correct ? 0 : 1);
	sqlite3_bind_int(stmt, 6, pass_flag);
	sqlite3_bind_int(stmt, 7, fail_flag);
	if (!StepDone(stmt)) {
		sqlite3_finalize(stmt);
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
		sqlite3_close(db);
		return;
	}
	sqlite3_finalize(stmt);

	if (!eteacher::database_manager::CommitTransaction(db, log_tag_)) {
		eteacher::database_manager::RollbackTransaction(db, log_tag_);
	}
	sqlite3_close(db);
}

ResultModule::ResultModule(const char *log_tag)
	: progress_dao_(log_tag) {
}

bool ResultModule::IsPassed(const SessionModule &session) const {
	if (session.TotalAnswered() <= 0) {
		return false;
	}
	const float accuracy = static_cast<float>(session.CorrectCount()) / static_cast<float>(session.TotalAnswered());
	return session.TotalAnswered() >= session.PassTargetQuestions() && accuracy >= 0.8f && session.Score() >= 60;
}

Summary ResultModule::BuildSummary(const SessionModule &session) const {
	Summary summary;
	summary.passed = IsPassed(session);
	summary.summary_text =
		(summary.passed ? "本轮结算 恭喜过关 " : "本轮结算 未过关 ") +
		(std::string("分数:") + std::to_string(session.Score()) +
		" 正确:" + std::to_string(session.CorrectCount()) +
		" 错误:" + std::to_string(session.WrongCount()));
	return summary;
}

const UserProgressDao &ResultModule::ProgressDao() const {
	return progress_dao_;
}

}  // namespace word_practice