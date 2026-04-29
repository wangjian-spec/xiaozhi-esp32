#include "eteacher/apps/word_practice/word_practice_selection_module.h"

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sqlite3.h>

#include "esp_timer.h"
#include "eteacher/apps/word_practice/word_practice_config.h"
#include "eteacher/apps/word_practice/word_practice_db_utils.h"
#include "eteacher/apps/word_practice/word_practice_learning_module.h"
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
#define WP_SELECT_TRACE_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)

namespace word_practice {
namespace {

constexpr int kDefaultUserId = 0;
constexpr const char *kTag = "WordPracticeSelect";

struct SqliteDbCloser {
	void operator()(sqlite3 *db) const {
		if (db != nullptr) {
			for (sqlite3_stmt *stmt = sqlite3_next_stmt(db, nullptr); stmt != nullptr; stmt = sqlite3_next_stmt(db, nullptr)) {
				ESP_LOGW(kTag, "selection close found pending statement, finalizing before close");
				sqlite3_finalize(stmt);
			}
			char *err = nullptr;
			const int detach_rc = sqlite3_exec(db, "DETACH DATABASE dictdb;", nullptr, nullptr, &err);
			if (detach_rc != SQLITE_OK && detach_rc != SQLITE_ERROR) {
				ESP_LOGW(kTag,
					"selection close detach dictdb failed rc=%d msg=%s",
					detach_rc,
					err != nullptr ? err : sqlite3_errmsg(db));
			}
			if (err != nullptr) {
				sqlite3_free(err);
			}
			const int close_rc = sqlite3_close(db);
			if (close_rc != SQLITE_OK) {
				ESP_LOGW(kTag, "selection close db failed rc=%d msg=%s", close_rc, sqlite3_errmsg(db));
			}
		}
	}
};

int64_t NowSec() {
	return CurrentPersistentEpochSeconds();
}

int64_t NowMs() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000ULL);
}

using db::PrepareStatement;
using db::StatementPtr;

bool ExecSql(sqlite3 *db, const char *sql) {
	if (db == nullptr || sql == nullptr) {
		return false;
	}
	char *err = nullptr;
	const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
	if (rc != SQLITE_OK) {
		ESP_LOGW(kTag, "exec sql failed rc=%d msg=%s sql=%s", rc, err != nullptr ? err : "null", sql);
		if (err != nullptr) {
			sqlite3_free(err);
		}
		return false;
	}
	return true;
}

bool TableExists(sqlite3 *db, const char *schema_name, const char *table_name) {
	if (db == nullptr || schema_name == nullptr || table_name == nullptr) {
		return false;
	}

	const std::string sql =
		"SELECT 1 FROM " + std::string(schema_name) + ".sqlite_master WHERE type='table' AND name=? LIMIT 1;";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}

	if (sqlite3_bind_text(stmt.get(), 1, table_name, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		return false;
	}

	return sqlite3_step(stmt.get()) == SQLITE_ROW;
}

bool ColumnExists(sqlite3 *db, const char *schema_name, const char *table_name, const char *column_name) {
	if (db == nullptr || schema_name == nullptr || table_name == nullptr || column_name == nullptr) {
		return false;
	}
	if (!TableExists(db, schema_name, table_name)) {
		return false;
	}

	const std::string sql =
		"SELECT \"" + std::string(column_name) + "\" FROM " + std::string(schema_name) + ".\"" +
		std::string(table_name) + "\" LIMIT 0;";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}
	return true;
}

bool AttachReadonlyDb(sqlite3 *db, const std::string &db_path, const char *alias) {
	if (db == nullptr || db_path.empty() || alias == nullptr || alias[0] == '\0') {
		return false;
	}

	const std::string sql = "ATTACH DATABASE ? AS " + std::string(alias) + ";";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}

	const int bind_rc = sqlite3_bind_text(stmt.get(), 1, db_path.c_str(), -1, SQLITE_TRANSIENT);
	if (bind_rc != SQLITE_OK) {
		ESP_LOGW(kTag, "attach bind failed rc=%d path=%s", bind_rc, db_path.c_str());
		return false;
	}

	const int step_rc = sqlite3_step(stmt.get());
	if (step_rc != SQLITE_DONE) {
		ESP_LOGW(kTag, "attach db failed rc=%d path=%s msg=%s", step_rc, db_path.c_str(), sqlite3_errmsg(db));
		return false;
	}
	DB_LOGI(kTag, "RESOURCE_OK kind=db scope=dictionary action=attach path=%s method=ATTACH DATABASE alias=%s", db_path.c_str(), alias);
	return true;
}

int CountDueReviewWords(sqlite3 *db, int user_id, const std::string &textbook_name, int64_t now_sec) {
	if (db == nullptr) {
		return 0;
	}
	const char *sql =
		"SELECT COUNT(1) "
		"FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND COALESCE(p.mastered, 0) = 0 "
		"AND (p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?);";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return 0;
	}
	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int64(stmt.get(), 3, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK) {
		return 0;
	}
	if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
		return 0;
	}
	return std::max(0, sqlite3_column_int(stmt.get(), 0));
}

int CountProfiles(sqlite3 *db, const std::string &sql, int user_id, const std::string &textbook_name, int64_t now_sec) {
	if (db == nullptr) {
		return 0;
	}
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		ESP_LOGW(kTag, "count profiles prepare failed msg=%s sql=%s", sqlite3_errmsg(db), sql.c_str());
		return 0;
	}
	int bind_index = 1;
	if (sqlite3_bind_int(stmt.get(), bind_index++, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), bind_index++, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		ESP_LOGW(kTag, "count profiles bind failed msg=%s sql=%s", sqlite3_errmsg(db), sql.c_str());
		return 0;
	}
	if (sql.find("next_review_at") != std::string::npos) {
		if (sqlite3_bind_int64(stmt.get(), bind_index++, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK) {
			ESP_LOGW(kTag, "count profiles bind now failed msg=%s sql=%s", sqlite3_errmsg(db), sql.c_str());
			return 0;
		}
	}
	if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
		ESP_LOGW(kTag, "count profiles step failed msg=%s sql=%s", sqlite3_errmsg(db), sql.c_str());
		return 0;
	}
	return std::max(0, sqlite3_column_int(stmt.get(), 0));
}

int CountDictionaryWords(sqlite3 *db) {
	if (db == nullptr) {
		return 0;
	}
	const char *sql =
		"SELECT COUNT(1) FROM dictdb.word WHERE word IS NOT NULL AND TRIM(word) <> '';";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return 0;
	}
	if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
		return 0;
	}
	return std::max(0, sqlite3_column_int(stmt.get(), 0));
}

bool QueryDictionaryIdRange(sqlite3 *db, int *min_word_id, int *max_word_id) {
	if (db == nullptr || min_word_id == nullptr || max_word_id == nullptr) {
		return false;
	}
	*min_word_id = 0;
	*max_word_id = 0;
	const char *sql =
		"SELECT COALESCE(MIN(id), 0), COALESCE(MAX(id), 0) "
		"FROM dictdb.word WHERE word IS NOT NULL AND TRIM(word) <> '';";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}
	if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
		return false;
	}
	*min_word_id = sqlite3_column_int(stmt.get(), 0);
	*max_word_id = sqlite3_column_int(stmt.get(), 1);
	return true;
}

int CountTopUpCandidates(sqlite3 *db, int user_id, const std::string &textbook_name, int start_after_word_id) {
	if (db == nullptr) {
		return 0;
	}
	const char *sql =
		"SELECT COUNT(1) "
		"FROM dictdb.word AS w "
		"LEFT JOIN word_learning_profile AS p "
		"  ON p.user_id = ? AND p.textbook_name = ? AND p.word_id = w.id "
		"WHERE w.word IS NOT NULL AND TRIM(w.word) <> '' "
		"AND w.id > ? "
		"AND p.word_id IS NULL;";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return 0;
	}
	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 3, std::max(0, start_after_word_id)) != SQLITE_OK) {
		return 0;
	}
	if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
		return 0;
	}
	return std::max(0, sqlite3_column_int(stmt.get(), 0));
}

std::string SampleTopUpCandidateIds(
	sqlite3 *db,
	int user_id,
	const std::string &textbook_name,
	int start_after_word_id,
	size_t limit) {
	if (db == nullptr || limit == 0) {
		return {};
	}
	const char *sql =
		"SELECT w.id "
		"FROM dictdb.word AS w "
		"LEFT JOIN word_learning_profile AS p "
		"  ON p.user_id = ? AND p.textbook_name = ? AND p.word_id = w.id "
		"WHERE w.word IS NOT NULL AND TRIM(w.word) <> '' "
		"AND w.id > ? "
		"AND p.word_id IS NULL "
		"ORDER BY w.id ASC "
		"LIMIT ?;";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return {};
	}
	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 3, std::max(0, start_after_word_id)) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 4, static_cast<int>(limit)) != SQLITE_OK) {
		return {};
	}
	std::ostringstream sample;
	bool first = true;
	for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
		const int word_id = sqlite3_column_int(stmt.get(), 0);
		if (!first) {
			sample << ',';
		}
		first = false;
		sample << word_id;
	}
	return sample.str();
}

void LogSelectionProfileInventory(sqlite3 *db, int user_id, const std::string &textbook_name, int64_t now_sec) {
	if (db == nullptr) {
		return;
	}
	const std::string total_sql =
		"SELECT COUNT(1) FROM word_learning_profile WHERE user_id=? AND textbook_name=?;";
	const std::string selectable_due_sql =
		"SELECT COUNT(1) FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND COALESCE(p.mastered, 0) = 0 "
		"AND COALESCE(p.last_practiced_at, 0) > 0 "
		"AND (p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?);";
	const std::string selectable_fresh_sql =
		"SELECT COUNT(1) FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND COALESCE(p.mastered, 0) = 0 "
		"AND COALESCE(p.last_practiced_at, 0) = 0;";
	const std::string selectable_backlog_sql =
		"SELECT COUNT(1) FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND COALESCE(p.mastered, 0) = 0 "
		"AND COALESCE(p.last_practiced_at, 0) > 0 "
		"AND COALESCE(p.next_review_at, 0) > ?;";
	const std::string mastered_sql =
		"SELECT COUNT(1) FROM word_learning_profile WHERE user_id=? AND textbook_name=? AND COALESCE(mastered, 0) <> 0;";
	WP_SELECT_TRACE_LOGW(
		kTag,
		"selection profile inventory textbook=%s total=%d due=%d fresh=%d backlog=%d mastered=%d",
		textbook_name.c_str(),
		CountProfiles(db, total_sql, user_id, textbook_name, now_sec),
		CountProfiles(db, selectable_due_sql, user_id, textbook_name, now_sec),
		CountProfiles(db, selectable_fresh_sql, user_id, textbook_name, now_sec),
		CountProfiles(db, selectable_backlog_sql, user_id, textbook_name, now_sec),
		CountProfiles(db, mastered_sql, user_id, textbook_name, now_sec));
}

bool InsertProfileSeed(sqlite3 *db, int user_id, int word_id, const std::string &textbook_name) {
	if (db == nullptr || word_id <= 0 || textbook_name.empty()) {
		return false;
	}
	const char *sql =
		"INSERT OR IGNORE INTO word_learning_profile("
		"user_id, word_id, textbook_name, stage, strength, recall_score, output_score, next_review_at, lapse_count, last_practiced_at, last_decay_at, last_reviewed_at, last_response_time_ms, persistent_boost, mastered"
		") VALUES(?, ?, ?, 1, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}
	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 2, word_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		return false;
	}
	const int step_rc = sqlite3_step(stmt.get());
	if (step_rc != SQLITE_DONE) {
		ESP_LOGW(kTag,
			"insert profile seed failed rc=%d word_id=%d textbook=%s msg=%s",
			step_rc,
			word_id,
			textbook_name.c_str(),
			sqlite3_errmsg(db));
		return false;
	}
	const int changed_rows = sqlite3_changes(db);
	if (changed_rows <= 0) {
		ESP_LOGW(kTag,
			"insert profile seed ignored word_id=%d textbook=%s user_id=%d",
			word_id,
			textbook_name.c_str(),
			user_id);
		return false;
	}
	return true;
}

std::vector<int> CollectTopUpCandidateWordIds(sqlite3 *db,
						      int user_id,
						      const std::string &textbook_name,
						      int start_after_word_id,
						      size_t limit,
						      int *next_new_word_id) {
	std::vector<int> word_ids;
	if (db == nullptr || next_new_word_id == nullptr || limit == 0) {
		return word_ids;
	}

	auto append_candidates = [&](int cursor_start, size_t remaining, bool allow_wrap) {
		const char *sql =
			"SELECT w.id "
			"FROM dictdb.word AS w "
			"LEFT JOIN word_learning_profile AS p "
			"  ON p.user_id = ? AND p.textbook_name = ? AND p.word_id = w.id "
			"WHERE w.word IS NOT NULL AND TRIM(w.word) <> '' "
			"AND w.id > ? "
			"AND p.word_id IS NULL "
			"ORDER BY w.id ASC "
			"LIMIT ?;";
		StatementPtr stmt;
		if (!PrepareStatement(db, sql, &stmt)) {
			return;
		}
		if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
			sqlite3_bind_text(stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
			sqlite3_bind_int(stmt.get(), 3, std::max(0, cursor_start)) != SQLITE_OK ||
			sqlite3_bind_int(stmt.get(), 4, static_cast<int>(remaining)) != SQLITE_OK) {
			return;
		}
		for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
			const int word_id = sqlite3_column_int(stmt.get(), 0);
			if (word_id <= 0) {
				continue;
			}
			word_ids.push_back(word_id);
			*next_new_word_id = word_id;
			if (word_ids.size() >= limit) {
				break;
			}
		}
		if (!allow_wrap && word_ids.empty()) {
			*next_new_word_id = std::max(0, cursor_start);
		}
	};

	*next_new_word_id = std::max(0, start_after_word_id);
	append_candidates(start_after_word_id, limit, false);
	if (start_after_word_id > 0 && word_ids.size() < limit) {
		append_candidates(0, limit - word_ids.size(), true);
	}
	return word_ids;
}

int InsertProfileSeeds(sqlite3 *db, int user_id, const std::vector<int> &word_ids, const std::string &textbook_name) {
	if (db == nullptr || word_ids.empty() || textbook_name.empty()) {
		return 0;
	}
	int inserted_count = 0;
	for (const int word_id : word_ids) {
		if (InsertProfileSeed(db, user_id, word_id, textbook_name)) {
			++inserted_count;
		}
	}
	return inserted_count;
}

std::string BuildDictionaryWordLookupSql(size_t word_count, bool has_word_image) {
	std::string sql =
		std::string("SELECT w.id, w.word, ") + (has_word_image ? "COALESCE(w.image, '')" : "''") +
		" FROM dictdb.word AS w WHERE w.id IN (";
	for (size_t index = 0; index < word_count; ++index) {
		if (index > 0) {
			sql += ",";
		}
		sql += "?";
	}
	sql += ") AND w.word IS NOT NULL AND TRIM(w.word) <> '';";
	return sql;
}

void AppendDictionaryWordsForProfileIds(sqlite3 *db,
					 const std::vector<std::pair<int, bool>> &selected_profile_ids,
					 std::vector<SelectedWord> *selected_words) {
	if (db == nullptr || selected_words == nullptr || selected_profile_ids.empty()) {
		return;
	}
	const bool has_word_image = ColumnExists(db, "dictdb", "word", "image");
	const std::string dict_sql = BuildDictionaryWordLookupSql(selected_profile_ids.size(), has_word_image);
	StatementPtr dict_stmt;
	if (!PrepareStatement(db, dict_sql, &dict_stmt)) {
		return;
	}
	for (size_t index = 0; index < selected_profile_ids.size(); ++index) {
		if (sqlite3_bind_int(dict_stmt.get(), static_cast<int>(index) + 1, selected_profile_ids[index].first) != SQLITE_OK) {
			return;
		}
	}
	std::unordered_map<int, SelectedWord> dictionary_words;
	dictionary_words.reserve(selected_profile_ids.size());
	int step_rc = SQLITE_ROW;
	while ((step_rc = sqlite3_step(dict_stmt.get())) == SQLITE_ROW) {
		SelectedWord selected;
		selected.word_id = sqlite3_column_int(dict_stmt.get(), 0);
		const unsigned char *word_text = sqlite3_column_text(dict_stmt.get(), 1);
		const unsigned char *image_text = sqlite3_column_text(dict_stmt.get(), 2);
		if (selected.word_id <= 0 || word_text == nullptr) {
			continue;
		}
		selected.word = reinterpret_cast<const char *>(word_text);
		selected.image = image_text != nullptr ? reinterpret_cast<const char *>(image_text) : "";
		dictionary_words[selected.word_id] = std::move(selected);
	}
	if (step_rc != SQLITE_DONE) {
		return;
	}
	selected_words->reserve(selected_words->size() + selected_profile_ids.size());
	for (const auto &[word_id, is_review] : selected_profile_ids) {
		auto it = dictionary_words.find(word_id);
		if (it == dictionary_words.end()) {
			continue;
		}
		it->second.is_review = is_review;
		selected_words->push_back(std::move(it->second));
	}
}

void AppendProfileIds(sqlite3 *db,
			 int user_id,
			 const std::string &textbook_name,
			 const std::string &sql,
			 int64_t now_sec,
			 size_t limit,
			 bool is_review,
			 std::unordered_set<int> *seen_word_ids,
			 std::vector<std::pair<int, bool>> *selected_profile_ids) {
	if (db == nullptr || seen_word_ids == nullptr || selected_profile_ids == nullptr || limit == 0) {
		return;
	}
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return;
	}
	int bind_index = 1;
	if (sqlite3_bind_int(stmt.get(), bind_index++, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), bind_index++, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		return;
	}
	if (sql.find("next_review_at") != std::string::npos) {
		if (sqlite3_bind_int64(stmt.get(), bind_index++, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK) {
			return;
		}
	}
	if (sqlite3_bind_int(stmt.get(), bind_index, static_cast<int>(limit)) != SQLITE_OK) {
		return;
	}
	size_t appended_count = 0;
	for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
		const int word_id = sqlite3_column_int(stmt.get(), 0);
		if (word_id <= 0 || !seen_word_ids->insert(word_id).second) {
			continue;
		}
		selected_profile_ids->emplace_back(word_id, is_review);
		++appended_count;
		if (appended_count >= limit) {
			break;
		}
	}
}

void AppendSelectedProfileWords(sqlite3 *db,
				  int user_id,
				  const std::string &textbook_name,
				  int64_t now_sec,
				  size_t limit,
				  std::vector<SelectedWord> *selected_words) {
	if (db == nullptr || selected_words == nullptr || limit == 0) {
		return;
	}
	std::vector<std::pair<int, bool>> selected_profile_ids;
	selected_profile_ids.reserve(limit);
	std::unordered_set<int> seen_word_ids;
	seen_word_ids.reserve(limit);

	const std::string due_sql =
		"SELECT p.word_id "
		"FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND COALESCE(p.mastered, 0) = 0 "
		"AND COALESCE(p.last_practiced_at, 0) > 0 "
		"AND (p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?) "
		"ORDER BY COALESCE(p.next_review_at, 0) ASC, p.word_id ASC "
		"LIMIT ?;";
	AppendProfileIds(db, user_id, textbook_name, due_sql, now_sec, limit, true, &seen_word_ids, &selected_profile_ids);

	if (selected_profile_ids.size() < limit) {
		const std::string fresh_sql =
			"SELECT p.word_id "
			"FROM word_learning_profile AS p "
			"WHERE p.user_id = ? "
			"AND p.textbook_name = ? "
			"AND COALESCE(p.mastered, 0) = 0 "
			"AND COALESCE(p.last_practiced_at, 0) = 0 "
			"ORDER BY p.word_id ASC "
			"LIMIT ?;";
		AppendProfileIds(
			db,
			user_id,
			textbook_name,
			fresh_sql,
			now_sec,
			limit - selected_profile_ids.size(),
			false,
			&seen_word_ids,
			&selected_profile_ids);
	}

	if (selected_profile_ids.size() < limit) {
		const std::string backlog_sql =
			"SELECT p.word_id "
			"FROM word_learning_profile AS p "
			"WHERE p.user_id = ? "
			"AND p.textbook_name = ? "
			"AND COALESCE(p.mastered, 0) = 0 "
			"AND COALESCE(p.last_practiced_at, 0) > 0 "
			"AND COALESCE(p.next_review_at, 0) > ? "
			"ORDER BY COALESCE(p.next_review_at, 0) ASC, p.word_id ASC "
			"LIMIT ?;";
		AppendProfileIds(
			db,
			user_id,
			textbook_name,
			backlog_sql,
			now_sec,
			limit - selected_profile_ids.size(),
			true,
			&seen_word_ids,
			&selected_profile_ids);
	}

	AppendDictionaryWordsForProfileIds(db, selected_profile_ids, selected_words);
}

}  // namespace
std::vector<SelectedWord> SelectionModule::SelectWordsFromVocabulary(const WordSelectionConfig &config,
						 int user_id,
						 int stage_index,
						 const std::string &textbook_name,
						 int last_new_word_id,
						 int *next_new_word_id) const {
	std::vector<SelectedWord> selected_words;
	int resolved_next_new_word_id = std::max(0, last_new_word_id);
	WP_SELECT_TRACE_LOGW(
		kTag,
		"selection begin user_id=%d stage_index=%d textbook=%s cursor_before=%d requested_total=%d",
		user_id,
		stage_index,
		textbook_name.c_str(),
		resolved_next_new_word_id,
		config.total_word_count);
	if (next_new_word_id != nullptr) {
		*next_new_word_id = resolved_next_new_word_id;
	}
	const int64_t select_start_ms = NowMs();
	const int total_count = std::max(0, config.total_word_count);
	if (total_count <= 0) {
		ESP_LOGW(kTag, "skip vocabulary selection: invalid total_count=%d", total_count);
		return selected_words;
	}

	const int normalized_user_id = user_id < 0 ? kDefaultUserId : user_id;

	if (!eteacher::database_manager::EnsureSqliteRuntimeReady(kTag) ||
		!eteacher::database_manager::EnsureSqliteSdMounted(kTag)) {
		ESP_LOGW(kTag, "vocabulary selection aborted: sqlite runtime or sd mount not ready");
		return selected_words;
	}

	const std::string user_db_path = eteacher::database_manager::DiscoverUserDataDbPath(kTag, nullptr);
	const std::string words_db_path = eteacher::database_manager::DiscoverDictionaryDbPath(kTag, stage_index);
	const int64_t discover_db_ms = NowMs();
	WP_SELECT_TRACE_LOGW(kTag, "selection db paths user=%s words=%s",
		user_db_path.empty() ? "(missing)" : user_db_path.c_str(),
		words_db_path.empty() ? "(missing)" : words_db_path.c_str());
	if (user_db_path.empty() || words_db_path.empty()) {
		WP_SELECT_TRACE_LOGW(kTag, "selection abort missing db user_db=%s words_db=%s",
			user_db_path.empty() ? "missing" : user_db_path.c_str(),
			words_db_path.empty() ? "missing" : words_db_path.c_str());
		return selected_words;
	}

	sqlite3 *raw_db = nullptr;
	const int64_t open_user_db_start_ms = NowMs();
	const int open_rc = sqlite3_open_v2(user_db_path.c_str(), &raw_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
	if (open_rc != SQLITE_OK || raw_db == nullptr) {
		WP_SELECT_TRACE_LOGW(kTag, "selection abort open user db failed rc=%d msg=%s", open_rc, raw_db ? sqlite3_errmsg(raw_db) : "null");
		if (raw_db != nullptr) {
			sqlite3_close(raw_db);
		}
		return selected_words;
	}
	DB_LOGI(kTag, "RESOURCE_OK kind=db scope=user action=open path=%s method=sqlite3_open_v2(READWRITE|CREATE) caller=SelectWordsFromVocabulary", user_db_path.c_str());
	std::unique_ptr<sqlite3, SqliteDbCloser> db(raw_db);
	const int64_t open_user_db_ms = NowMs() - open_user_db_start_ms;
	word_practice::WordMasteryDao mastery_dao(kTag, normalized_user_id);
	if (!mastery_dao.EnsureTables(db.get())) {
		WP_SELECT_TRACE_LOGW(kTag,
			"selection abort ensure word_learning_profile failed msg=%s",
			sqlite3_errmsg(db.get()));
		return selected_words;
	}

	const int64_t attach_dict_start_ms = NowMs();
	if (!AttachReadonlyDb(db.get(), words_db_path, "dictdb")) {
		WP_SELECT_TRACE_LOGW(kTag, "selection abort attach words db failed path=%s", words_db_path.c_str());
		return selected_words;
	}
	const int64_t attach_dict_ms = NowMs() - attach_dict_start_ms;

	const bool has_word_table = TableExists(db.get(), "dictdb", "word");
	const bool has_word_id = ColumnExists(db.get(), "dictdb", "word", "id");
	const bool has_word_word = ColumnExists(db.get(), "dictdb", "word", "word");
	if (!has_word_table || !has_word_id || !has_word_word) {
		WP_SELECT_TRACE_LOGW(kTag,
			"selection abort schema mismatch dict.word=%d dict.word.id=%d dict.word.word=%d",
			has_word_table,
			has_word_id,
			has_word_word);
		return selected_words;
	}
	const int64_t now_sec = NowSec();
	LogSelectionProfileInventory(db.get(), normalized_user_id, textbook_name, now_sec);
	const int64_t topup_start_ms = NowMs();
	const int due_review_count = CountDueReviewWords(db.get(), normalized_user_id, textbook_name, now_sec);
	int inserted_profiles = 0;
	if (due_review_count < config::kMinimumActiveProfileWords) {
		const int missing_count = config::kMinimumActiveProfileWords - due_review_count;
		const int cursor_before_topup = resolved_next_new_word_id;
		const int topup_candidate_count =
			CountTopUpCandidates(db.get(), normalized_user_id, textbook_name, resolved_next_new_word_id);
		const std::string topup_candidate_sample =
			SampleTopUpCandidateIds(db.get(), normalized_user_id, textbook_name, resolved_next_new_word_id, 5);
		int topup_cursor_after_candidates = resolved_next_new_word_id;
		const std::vector<int> topup_word_ids = CollectTopUpCandidateWordIds(
			db.get(),
			normalized_user_id,
			textbook_name,
			resolved_next_new_word_id,
			static_cast<size_t>(missing_count),
			&topup_cursor_after_candidates);
		WP_SELECT_TRACE_LOGW(kTag,
			"selection topup plan due_review=%d missing=%d cursor=%d candidates=%d sample_ids=%s",
			due_review_count,
			missing_count,
			resolved_next_new_word_id,
			topup_candidate_count,
			topup_candidate_sample.empty() ? "(none)" : topup_candidate_sample.c_str());
		if (topup_candidate_count == 0) {
			int min_word_id = 0;
			int max_word_id = 0;
			const bool has_id_range = QueryDictionaryIdRange(db.get(), &min_word_id, &max_word_id);
			WP_SELECT_TRACE_LOGW(kTag,
				"selection topup source empty dict_words=%d dict_min_id=%d dict_max_id=%d cursor=%d textbook=%s has_id_range=%d",
				CountDictionaryWords(db.get()),
				has_id_range ? min_word_id : 0,
				has_id_range ? max_word_id : 0,
				resolved_next_new_word_id,
				textbook_name.c_str(),
				has_id_range ? 1 : 0);
		}
			if (topup_word_ids.empty()) {
				resolved_next_new_word_id = cursor_before_topup;
			} else if (!ExecSql(db.get(), "DETACH DATABASE dictdb;")) {
				ESP_LOGW(kTag, "selection topup detach dictdb failed cursor=%d", resolved_next_new_word_id);
				resolved_next_new_word_id = cursor_before_topup;
			} else if (!ExecSql(db.get(), "BEGIN IMMEDIATE TRANSACTION;")) {
			ESP_LOGW(kTag,
				"selection topup begin transaction failed cursor=%d",
				resolved_next_new_word_id);
		} else {
				resolved_next_new_word_id = topup_cursor_after_candidates;
				inserted_profiles = InsertProfileSeeds(db.get(), normalized_user_id, topup_word_ids, textbook_name);
			if (!ExecSql(db.get(), "COMMIT;")) {
				ESP_LOGW(kTag,
					"selection topup commit failed cursor=%d inserted=%d",
					resolved_next_new_word_id,
					inserted_profiles);
				(void)ExecSql(db.get(), "ROLLBACK;");
				inserted_profiles = 0;
				resolved_next_new_word_id = cursor_before_topup;
			}
			if (!AttachReadonlyDb(db.get(), words_db_path, "dictdb")) {
				WP_SELECT_TRACE_LOGW(kTag, "selection abort reattach words db failed path=%s", words_db_path.c_str());
				return selected_words;
			}
		}
		if (inserted_profiles == 0 && topup_candidate_count > 0) {
			WP_SELECT_TRACE_LOGW(kTag,
				"selection topup inserted nothing despite candidates cursor=%d candidates=%d textbook=%s",
				cursor_before_topup,
				topup_candidate_count,
				textbook_name.c_str());
		}
	}
	const int64_t topup_ms = NowMs() - topup_start_ms;
	LogSelectionProfileInventory(db.get(), normalized_user_id, textbook_name, now_sec);

	const int64_t select_profile_start_ms = NowMs();
	AppendSelectedProfileWords(
		db.get(),
		normalized_user_id,
		textbook_name,
		now_sec,
		static_cast<size_t>(total_count),
		&selected_words);
	const int64_t select_profile_ms = NowMs() - select_profile_start_ms;
	if (next_new_word_id != nullptr) {
		*next_new_word_id = resolved_next_new_word_id;
	}
	WP_SELECT_TRACE_LOGW(kTag,
		"selected words summary total=%d due_review_count=%d inserted_profiles=%d cursor_after=%d actual_review=%d actual_new=%d",
		static_cast<int>(selected_words.size()),
		due_review_count,
		inserted_profiles,
		resolved_next_new_word_id,
		static_cast<int>(std::count_if(selected_words.begin(), selected_words.end(), [](const SelectedWord &word) {
			return word.is_review;
		})),
		static_cast<int>(std::count_if(selected_words.begin(), selected_words.end(), [](const SelectedWord &word) {
			return !word.is_review;
		})));

	WP_SELECT_TRACE_LOGW(kTag,
		"selection timing total_ms=%d discover_db_ms=%d open_user_db_ms=%d attach_dict_ms=%d topup_ms=%d select_profile_ms=%d selected=%d",
		static_cast<int>(NowMs() - select_start_ms),
		static_cast<int>(discover_db_ms - select_start_ms),
		static_cast<int>(open_user_db_ms),
		static_cast<int>(attach_dict_ms),
		static_cast<int>(topup_ms),
		static_cast<int>(select_profile_ms),
		static_cast<int>(selected_words.size()));

	return selected_words;
}
}  // namespace word_practice
