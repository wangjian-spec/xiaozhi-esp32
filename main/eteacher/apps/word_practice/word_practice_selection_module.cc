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
constexpr size_t kBacklogReserveMinSlots = 3;
constexpr bool kEnableSelectionInventoryLogging = false;
constexpr bool kEnablePrioritySelectionQuery = false;
constexpr bool kEnableSelectionTopupPlanLogging = false;

struct SqliteDbCloser {
	void operator()(sqlite3 *db) const {
		if (db != nullptr) {
			for (sqlite3_stmt *stmt = sqlite3_next_stmt(db, nullptr); stmt != nullptr; stmt = sqlite3_next_stmt(db, nullptr)) {
				ESP_LOGW(kTag, "selection close found pending statement, finalizing before close");
				sqlite3_finalize(stmt);
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

std::string DiscoverCachedUserDbPath() {
	static std::string cached_user_db_path;
	if (!cached_user_db_path.empty()) {
		return cached_user_db_path;
	}
	cached_user_db_path = eteacher::database_manager::DiscoverUserDataDbPath(kTag, nullptr);
	return cached_user_db_path;
}

std::string DiscoverCachedDictionaryDbPath(int stage_index) {
	static int cached_stage_index = 0;
	static std::string cached_dictionary_db_path;
	if (stage_index > 0 && cached_stage_index == stage_index && !cached_dictionary_db_path.empty()) {
		return cached_dictionary_db_path;
	}
	const std::string discovered_path = eteacher::database_manager::DiscoverDictionaryDbPath(kTag, stage_index);
	if (!discovered_path.empty()) {
		cached_stage_index = stage_index;
		cached_dictionary_db_path = discovered_path;
	}
	return discovered_path;
}

using db::PrepareStatement;
using db::StatementPtr;

bool TableExists(sqlite3 *db, const char *schema_name, const char *table_name) {
	if (db == nullptr || schema_name == nullptr || table_name == nullptr) {
		return false;
	}

	const std::string schema_prefix = (schema_name[0] != '\0') ? std::string(schema_name) + "." : std::string();
	const std::string sql =
		"SELECT 1 FROM " + schema_prefix + "sqlite_master WHERE type='table' AND name=? LIMIT 1;";
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

	const std::string schema_prefix = (schema_name[0] != '\0') ? std::string(schema_name) + "." : std::string();
	const std::string sql =
		"SELECT \"" + std::string(column_name) + "\" FROM " + schema_prefix + "\"" +
		std::string(table_name) + "\" LIMIT 0;";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}
	return true;
}

bool ValidateCachedDictionaryWordSchema(sqlite3 *db, const std::string &db_path, bool *has_word_table, bool *has_word_id, bool *has_word_word) {
	if (db == nullptr || has_word_table == nullptr || has_word_id == nullptr || has_word_word == nullptr) {
		return false;
	}
	static std::string cached_schema_db_path;
	static bool cached_has_word_table = false;
	static bool cached_has_word_id = false;
	static bool cached_has_word_word = false;
	if (!db_path.empty() && db_path == cached_schema_db_path) {
		*has_word_table = cached_has_word_table;
		*has_word_id = cached_has_word_id;
		*has_word_word = cached_has_word_word;
		return true;
	}
	*has_word_table = TableExists(db, "main", "word");
	*has_word_id = *has_word_table && ColumnExists(db, "main", "word", "id");
	*has_word_word = *has_word_table && ColumnExists(db, "main", "word", "word");
	if (!db_path.empty()) {
		cached_schema_db_path = db_path;
		cached_has_word_table = *has_word_table;
		cached_has_word_id = *has_word_id;
		cached_has_word_word = *has_word_word;
	}
	return true;
}

std::string MasteredProfileSqlCondition(const char *alias) {
	const std::string prefix = (alias != nullptr && alias[0] != '\0') ? std::string(alias) + "." : std::string();
	const std::string mastered_column = prefix + "mastered";
	return "((" + mastered_column + " = " + std::to_string(static_cast<int>(MasteredState::UserMastered)) + ")"
		" OR ((" + mastered_column + " IS NULL OR " + mastered_column + " <> " + std::to_string(static_cast<int>(MasteredState::Suppressed)) + ")"
		" AND " + prefix + "recall_score >= 3"
		" AND " + prefix + "output_score >= 3"
		" AND " + prefix + "strength >= 60"
		" AND " + prefix + "lapse_count <= 3))";
}

std::string NotMasteredProfileSqlCondition(const char *alias) {
	const std::string prefix = (alias != nullptr && alias[0] != '\0') ? std::string(alias) + "." : std::string();
	const std::string mastered_column = prefix + "mastered";
	return "((" + mastered_column + " IS NULL OR (" + mastered_column + " <> " + std::to_string(static_cast<int>(MasteredState::UserMastered)) +
		" AND " + mastered_column + " <> " + std::to_string(static_cast<int>(MasteredState::Suppressed)) + "))"
		" AND (" + prefix + "recall_score < 3"
		" OR " + prefix + "output_score < 3"
		" OR " + prefix + "strength < 60"
		" OR " + prefix + "lapse_count > 3))";
}

int CountActiveProfileWords(sqlite3 *db, int user_id, const std::string &textbook_name, int64_t now_sec) {
	if (db == nullptr) {
		return 0;
	}
	const std::string sql =
		"SELECT COUNT(1) "
		"FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND " + NotMasteredProfileSqlCondition("p") + " "
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

bool HasMinimumActiveProfileWords(sqlite3 *db,
				      int user_id,
				      const std::string &textbook_name,
				      int64_t now_sec,
				      int minimum_count) {
	if (db == nullptr || minimum_count <= 0) {
		return false;
	}
	const std::string sql =
		"SELECT 1 "
		"FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND " + NotMasteredProfileSqlCondition("p") + " "
		"AND (p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?) "
		"LIMIT ? OFFSET ?;";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}
	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int64(stmt.get(), 3, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 4, 1) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 5, minimum_count - 1) != SQLITE_OK) {
		return false;
	}
	return sqlite3_step(stmt.get()) == SQLITE_ROW;
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

size_t ComputeNoDueBacklogReserve(size_t limit) {
	if (limit == 0) {
		return 0;
	}
	const size_t max_reserve = std::max<size_t>(1, limit / 3);
	return std::max(std::min(limit, max_reserve), std::min(limit, kBacklogReserveMinSlots));
}

int CountDictionaryWords(sqlite3 *db) {
	if (db == nullptr) {
		return 0;
	}
	const char *sql =
		"SELECT COUNT(1) FROM word WHERE word IS NOT NULL AND TRIM(word) <> '';";
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
		"FROM word WHERE word IS NOT NULL AND TRIM(word) <> '';";
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

std::unordered_set<int> LoadExistingProfileWordIds(sqlite3 *db, int user_id, const std::string &textbook_name) {
	std::unordered_set<int> word_ids;
	if (db == nullptr) {
		return word_ids;
	}
	const char *sql =
		"SELECT word_id FROM word_learning_profile WHERE user_id=? AND textbook_name=?;";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return word_ids;
	}
	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		return word_ids;
	}
	for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
		const int word_id = sqlite3_column_int(stmt.get(), 0);
		if (word_id > 0) {
			word_ids.insert(word_id);
		}
	}
	return word_ids;
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
		"AND " + NotMasteredProfileSqlCondition("p") + " "
		"AND COALESCE(p.last_practiced_at, 0) > 0 "
		"AND (p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?);";
	const std::string selectable_fresh_sql =
		"SELECT COUNT(1) FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND " + NotMasteredProfileSqlCondition("p") + " "
		"AND COALESCE(p.last_practiced_at, 0) = 0;";
	const std::string selectable_backlog_sql =
		"SELECT COUNT(1) FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND " + NotMasteredProfileSqlCondition("p") + " "
		"AND COALESCE(p.last_practiced_at, 0) > 0 "
		"AND COALESCE(p.next_review_at, 0) > ?;";
	const std::string mastered_sql =
		"SELECT COUNT(1) FROM word_learning_profile AS p WHERE p.user_id=? AND p.textbook_name=? AND " + MasteredProfileSqlCondition("p") + ";";
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

std::vector<int> CollectTopUpCandidateWordIds(sqlite3 *dict_db,
					      const std::unordered_set<int> &existing_profile_word_ids,
						      int start_after_word_id,
						      size_t limit,
						      int *next_new_word_id) {
	std::vector<int> word_ids;
	if (dict_db == nullptr || next_new_word_id == nullptr || limit == 0) {
		return word_ids;
	}

	auto append_candidates = [&](int cursor_start, size_t remaining, bool allow_wrap) {
		const char *sql =
			"SELECT w.id "
			"FROM word AS w "
			"WHERE w.word IS NOT NULL AND TRIM(w.word) <> '' "
			"AND w.id > ? "
			"ORDER BY w.id ASC "
			"LIMIT ?;";
		StatementPtr stmt;
		if (!PrepareStatement(dict_db, sql, &stmt)) {
			return;
		}
		if (sqlite3_bind_int(stmt.get(), 1, std::max(0, cursor_start)) != SQLITE_OK ||
			sqlite3_bind_int(stmt.get(), 2, static_cast<int>(remaining * 4)) != SQLITE_OK) {
			return;
		}
		for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
			const int word_id = sqlite3_column_int(stmt.get(), 0);
			if (word_id <= 0) {
				continue;
			}
			if (existing_profile_word_ids.find(word_id) != existing_profile_word_ids.end()) {
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
	const char *sql =
		"INSERT OR IGNORE INTO word_learning_profile("
		"user_id, word_id, textbook_name, stage, strength, recall_score, output_score, next_review_at, lapse_count, last_practiced_at, last_decay_at, last_reviewed_at, last_response_time_ms, persistent_boost, mastered"
		") VALUES(?, ?, ?, 1, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return 0;
	}
	int inserted_count = 0;
	for (const int word_id : word_ids) {
		if (sqlite3_reset(stmt.get()) != SQLITE_OK || sqlite3_clear_bindings(stmt.get()) != SQLITE_OK) {
			return inserted_count;
		}
		if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
			sqlite3_bind_int(stmt.get(), 2, word_id) != SQLITE_OK ||
			sqlite3_bind_text(stmt.get(), 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
			continue;
		}
		const int step_rc = sqlite3_step(stmt.get());
		if (step_rc != SQLITE_DONE) {
			ESP_LOGW(kTag,
				"insert profile seed failed rc=%d word_id=%d textbook=%s msg=%s",
				step_rc,
				word_id,
				textbook_name.c_str(),
				sqlite3_errmsg(db));
			continue;
		}
		if (sqlite3_changes(db) > 0) {
			++inserted_count;
		}
	}
	return inserted_count;
}

std::string BuildDictionaryWordLookupSql(size_t word_count, bool has_word_image) {
	std::string sql =
		std::string("SELECT w.id, w.word, ") + (has_word_image ? "COALESCE(w.image, '')" : "''") +
		" FROM word AS w WHERE w.id IN (";
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
	const bool has_word_image = ColumnExists(db, "main", "word", "image");
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
		ESP_LOGW(kTag, "append profile ids prepare failed msg=%s", sqlite3_errmsg(db));
		return;
	}
	int bind_index = 1;
	if (sqlite3_bind_int(stmt.get(), bind_index++, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), bind_index++, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		ESP_LOGW(kTag, "append profile ids bind user/textbook failed msg=%s", sqlite3_errmsg(db));
		return;
	}
	if (sql.find("next_review_at") != std::string::npos) {
		if (sqlite3_bind_int64(stmt.get(), bind_index++, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK) {
			ESP_LOGW(kTag, "append profile ids bind now failed msg=%s", sqlite3_errmsg(db));
			return;
		}
	}
	if (sqlite3_bind_int(stmt.get(), bind_index, static_cast<int>(limit)) != SQLITE_OK) {
		ESP_LOGW(kTag, "append profile ids bind limit failed msg=%s", sqlite3_errmsg(db));
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

std::string BuildPrioritySelectedProfileSql() {
	const std::string not_mastered = NotMasteredProfileSqlCondition("p");
	return
		"WITH candidates AS ("
		"SELECT p.word_id AS word_id, "
		"COALESCE(p.next_review_at, 0) AS next_review_at, "
		"CASE "
		"WHEN COALESCE(p.last_practiced_at, 0) > 0 AND (p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?) THEN 0 "
		"WHEN COALESCE(p.last_practiced_at, 0) = 0 THEN 1 "
		"WHEN COALESCE(p.last_practiced_at, 0) > 0 AND COALESCE(p.next_review_at, 0) > ? THEN 2 "
		"ELSE 3 END AS category "
		"FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND " + not_mastered + " "
		"AND ("
		"COALESCE(p.last_practiced_at, 0) = 0 "
		"OR (COALESCE(p.last_practiced_at, 0) > 0 AND ((p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?) OR COALESCE(p.next_review_at, 0) > ?))"
		")"
		"), stats AS ("
		"SELECT MAX(CASE WHEN category = 0 THEN 1 ELSE 0 END) AS has_due FROM candidates"
		"), ranked AS ("
		"SELECT c.word_id AS word_id, c.category AS category, c.next_review_at AS next_review_at, "
		"CASE WHEN c.category = 2 THEN ROW_NUMBER() OVER (PARTITION BY c.category ORDER BY c.next_review_at ASC, c.word_id ASC) ELSE 0 END AS backlog_rank "
		"FROM candidates AS c"
		") "
		"SELECT ranked.word_id AS word_id, ranked.category AS category, CASE WHEN ranked.category = 1 THEN 0 ELSE 1 END AS is_review "
		"FROM ranked CROSS JOIN stats "
		"ORDER BY "
		"CASE "
		"WHEN ranked.category = 0 THEN 0 "
		"WHEN stats.has_due = 0 AND ranked.category = 2 AND ranked.backlog_rank <= ? THEN 1 "
		"WHEN ranked.category = 1 THEN 2 "
		"WHEN ranked.category = 2 THEN 3 "
		"ELSE 4 END ASC, "
		"CASE WHEN ranked.category IN (0, 2) THEN ranked.next_review_at ELSE 0 END ASC, "
		"ranked.word_id ASC "
		"LIMIT ?;";
}

bool AppendPrioritySelectedProfileIds(sqlite3 *db,
				      int user_id,
				      const std::string &textbook_name,
				      int64_t now_sec,
				      size_t backlog_reserve_requested,
				      size_t limit,
				      std::vector<std::pair<int, bool>> *selected_profile_ids,
				      size_t *due_selected_count,
				      size_t *backlog_selected_count) {
	if (db == nullptr || selected_profile_ids == nullptr || limit == 0) {
		return false;
	}
	StatementPtr stmt;
	const std::string sql = BuildPrioritySelectedProfileSql();
	if (!PrepareStatement(db, sql, &stmt)) {
		ESP_LOGW(kTag, "priority selection prepare failed msg=%s", sqlite3_errmsg(db));
		return false;
	}
	int bind_index = 1;
	if (sqlite3_bind_int64(stmt.get(), bind_index++, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK ||
		sqlite3_bind_int64(stmt.get(), bind_index++, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), bind_index++, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), bind_index++, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int64(stmt.get(), bind_index++, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK ||
		sqlite3_bind_int64(stmt.get(), bind_index++, static_cast<sqlite3_int64>(now_sec)) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), bind_index++, static_cast<int>(backlog_reserve_requested)) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), bind_index, static_cast<int>(limit)) != SQLITE_OK) {
		ESP_LOGW(kTag, "priority selection bind failed msg=%s", sqlite3_errmsg(db));
		return false;
	}
	int rc = sqlite3_step(stmt.get());
	for (; rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
		const int word_id = sqlite3_column_int(stmt.get(), 0);
		const int category = sqlite3_column_int(stmt.get(), 1);
		const bool is_review = sqlite3_column_int(stmt.get(), 2) != 0;
		if (word_id <= 0) {
			continue;
		}
		selected_profile_ids->emplace_back(word_id, is_review);
		if (category == 0) {
			++(*due_selected_count);
		} else if (category == 2) {
			++(*backlog_selected_count);
		}
	}
	if (rc != SQLITE_DONE) {
		ESP_LOGW(kTag, "priority selection step failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		return false;
	}
	return true;
}

void AppendSelectedProfileWords(sqlite3 *user_db,
				  sqlite3 *dict_db,
				  int user_id,
				  const std::string &textbook_name,
				  int64_t now_sec,
				  size_t limit,
				  std::vector<SelectedWord> *selected_words) {
	if (user_db == nullptr || dict_db == nullptr || selected_words == nullptr || limit == 0) {
		return;
	}
	const int64_t select_start_ms = NowMs();
	std::vector<std::pair<int, bool>> selected_profile_ids;
	selected_profile_ids.reserve(limit);
	std::unordered_set<int> seen_word_ids;
	seen_word_ids.reserve(limit);
	const int64_t count_ms = 0;
	const size_t backlog_reserve_requested = ComputeNoDueBacklogReserve(limit);
	size_t backlog_selected_count = 0;
	size_t due_selected_count = 0;
	bool priority_query_ok = false;
	int64_t priority_query_ms = 0;
	if (kEnablePrioritySelectionQuery) {
		const int64_t priority_query_start_ms = NowMs();
		priority_query_ok = AppendPrioritySelectedProfileIds(
			user_db,
			user_id,
			textbook_name,
			now_sec,
			backlog_reserve_requested,
			limit,
			&selected_profile_ids,
			&due_selected_count,
			&backlog_selected_count);
		priority_query_ms = NowMs() - priority_query_start_ms;
	}
	if (priority_query_ok) {
		for (const auto &[word_id, _] : selected_profile_ids) {
			if (word_id > 0) {
				seen_word_ids.insert(word_id);
			}
		}
	}
	if (selected_profile_ids.empty()) {
		const std::string due_sql =
			"SELECT p.word_id "
			"FROM word_learning_profile AS p "
			"WHERE p.user_id = ? "
			"AND p.textbook_name = ? "
			"AND " + NotMasteredProfileSqlCondition("p") + " "
			"AND COALESCE(p.last_practiced_at, 0) > 0 "
			"AND (p.next_review_at IS NULL OR p.next_review_at = 0 OR p.next_review_at <= ?) "
			"ORDER BY COALESCE(p.next_review_at, 0) ASC, p.word_id ASC "
			"LIMIT ?;";
		AppendProfileIds(user_db, user_id, textbook_name, due_sql, now_sec, limit, true, &seen_word_ids, &selected_profile_ids);
		due_selected_count = selected_profile_ids.size();
		if (selected_profile_ids.size() < limit && due_selected_count == 0) {
			const std::string backlog_sql =
				"SELECT p.word_id "
				"FROM word_learning_profile AS p "
				"WHERE p.user_id = ? "
				"AND p.textbook_name = ? "
				"AND " + NotMasteredProfileSqlCondition("p") + " "
				"AND COALESCE(p.last_practiced_at, 0) > 0 "
				"AND COALESCE(p.next_review_at, 0) > ? "
				"ORDER BY COALESCE(p.next_review_at, 0) ASC, p.word_id ASC "
				"LIMIT ?;";
			const size_t backlog_before = selected_profile_ids.size();
			AppendProfileIds(
				user_db,
				user_id,
				textbook_name,
				backlog_sql,
				now_sec,
				std::min(backlog_reserve_requested, limit - selected_profile_ids.size()),
				true,
				&seen_word_ids,
				&selected_profile_ids);
			backlog_selected_count += selected_profile_ids.size() - backlog_before;
		}
		if (selected_profile_ids.size() < limit) {
			const std::string fresh_sql =
				"SELECT p.word_id "
				"FROM word_learning_profile AS p "
				"WHERE p.user_id = ? "
				"AND p.textbook_name = ? "
				"AND " + NotMasteredProfileSqlCondition("p") + " "
				"AND COALESCE(p.last_practiced_at, 0) = 0 "
				"ORDER BY p.word_id ASC "
				"LIMIT ?;";
			AppendProfileIds(
				user_db,
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
				"AND " + NotMasteredProfileSqlCondition("p") + " "
				"AND COALESCE(p.last_practiced_at, 0) > 0 "
				"AND COALESCE(p.next_review_at, 0) > ? "
				"ORDER BY COALESCE(p.next_review_at, 0) ASC, p.word_id ASC "
				"LIMIT ?;";
			const size_t backlog_before = selected_profile_ids.size();
			AppendProfileIds(
				user_db,
				user_id,
				textbook_name,
				backlog_sql,
				now_sec,
				limit - selected_profile_ids.size(),
				true,
				&seen_word_ids,
				&selected_profile_ids);
			backlog_selected_count += selected_profile_ids.size() - backlog_before;
		}
		WP_SELECT_TRACE_LOGW(kTag,
			"priority selection fallback used ok=%d selected_ids=%d due_selected=%d backlog_selected=%d",
			priority_query_ok ? 1 : 0,
			static_cast<int>(selected_profile_ids.size()),
			static_cast<int>(due_selected_count),
			static_cast<int>(backlog_selected_count));
	}

	const int64_t dict_lookup_start_ms = NowMs();
	AppendDictionaryWordsForProfileIds(dict_db, selected_profile_ids, selected_words);
	const int64_t dict_lookup_ms = NowMs() - dict_lookup_start_ms;
	WP_SELECT_TRACE_LOGW(kTag,
		"selection append summary total_ms=%d count_ms=%d priority_query_ms=%d dict_lookup_ms=%d due_selected=%d backlog_selected=%d reserve_requested=%d selected_ids=%d selected_words=%d",
		static_cast<int>(NowMs() - select_start_ms),
		static_cast<int>(count_ms),
		static_cast<int>(priority_query_ms),
		static_cast<int>(dict_lookup_ms),
		static_cast<int>(due_selected_count),
		static_cast<int>(backlog_selected_count),
		static_cast<int>(backlog_reserve_requested),
		static_cast<int>(selected_profile_ids.size()),
		static_cast<int>(selected_words->size()));
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

	const std::string user_db_path = DiscoverCachedUserDbPath();
	const std::string words_db_path = DiscoverCachedDictionaryDbPath(stage_index);
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
	sqlite3 *raw_dict_db = nullptr;
	std::string opened_dict_db_path;
	const int64_t open_dict_db_start_ms = NowMs();
	if (!eteacher::database_manager::OpenReadonlyDbFile(
			words_db_path,
			&raw_dict_db,
			&opened_dict_db_path,
			kTag,
			"word_practice_dictionary") || raw_dict_db == nullptr) {
		WP_SELECT_TRACE_LOGW(kTag, "selection abort open words db failed path=%s", words_db_path.c_str());
		return selected_words;
	}
	std::unique_ptr<sqlite3, SqliteDbCloser> dict_db(raw_dict_db);
	const int64_t attach_dict_ms = NowMs() - open_dict_db_start_ms;
	if (!eteacher::database_manager::ConfigureWriteConnection(db.get(), kTag)) {
		WP_SELECT_TRACE_LOGW(kTag, "selection abort configure write connection failed msg=%s", sqlite3_errmsg(db.get()));
		return selected_words;
	}
	word_practice::WordMasteryDao mastery_dao(kTag, normalized_user_id);
	if (!mastery_dao.EnsureTables(db.get())) {
		WP_SELECT_TRACE_LOGW(kTag,
			"selection abort ensure word_learning_profile failed msg=%s",
			sqlite3_errmsg(db.get()));
		return selected_words;
	}

	bool has_word_table = false;
	bool has_word_id = false;
	bool has_word_word = false;
	(void)ValidateCachedDictionaryWordSchema(dict_db.get(), opened_dict_db_path, &has_word_table, &has_word_id, &has_word_word);
	if (!has_word_table || !has_word_id || !has_word_word) {
		WP_SELECT_TRACE_LOGW(kTag,
			"selection abort schema mismatch dict.word=%d dict.word.id=%d dict.word.word=%d",
			has_word_table,
			has_word_id,
			has_word_word);
		return selected_words;
	}
	const int64_t now_sec = NowSec();
	int64_t inventory_before_ms = 0;
	if (kEnableSelectionInventoryLogging) {
		const int64_t inventory_before_start_ms = NowMs();
		LogSelectionProfileInventory(db.get(), normalized_user_id, textbook_name, now_sec);
		inventory_before_ms = NowMs() - inventory_before_start_ms;
	}
	const int64_t topup_start_ms = NowMs();
	const int64_t active_profile_start_ms = NowMs();
	int active_profile_count = 0;
	const bool has_minimum_active_profiles = HasMinimumActiveProfileWords(
		db.get(),
		normalized_user_id,
		textbook_name,
		now_sec,
		config::kMinimumActiveProfileWords);
	if (has_minimum_active_profiles) {
		active_profile_count = config::kMinimumActiveProfileWords;
	} else {
		active_profile_count = CountActiveProfileWords(db.get(), normalized_user_id, textbook_name, now_sec);
	}
	const int64_t active_profile_ms = NowMs() - active_profile_start_ms;
	int64_t existing_profile_ms = 0;
	int inserted_profiles = 0;
	if (active_profile_count < config::kMinimumActiveProfileWords) {
		const int64_t existing_profile_start_ms = NowMs();
		const std::unordered_set<int> existing_profile_word_ids = LoadExistingProfileWordIds(db.get(), normalized_user_id, textbook_name);
		existing_profile_ms = NowMs() - existing_profile_start_ms;
		const int missing_count = config::kMinimumActiveProfileWords - active_profile_count;
		const int cursor_before_topup = resolved_next_new_word_id;
		int topup_cursor_after_candidates = resolved_next_new_word_id;
		const std::vector<int> topup_word_ids = CollectTopUpCandidateWordIds(
			dict_db.get(),
			existing_profile_word_ids,
			resolved_next_new_word_id,
			static_cast<size_t>(missing_count),
			&topup_cursor_after_candidates);
		if (kEnableSelectionTopupPlanLogging) {
			std::ostringstream topup_candidate_sample;
			for (size_t index = 0; index < topup_word_ids.size() && index < 5; ++index) {
				if (index > 0) {
					topup_candidate_sample << ',';
				}
				topup_candidate_sample << topup_word_ids[index];
			}
			WP_SELECT_TRACE_LOGW(kTag,
				"selection topup plan active_profiles=%d missing=%d cursor=%d candidates=%d sample_ids=%s",
				active_profile_count,
				missing_count,
				resolved_next_new_word_id,
				static_cast<int>(topup_word_ids.size()),
				topup_candidate_sample.str().empty() ? "(none)" : topup_candidate_sample.str().c_str());
		}
		if (topup_word_ids.empty()) {
			int min_word_id = 0;
			int max_word_id = 0;
			const bool has_id_range = QueryDictionaryIdRange(dict_db.get(), &min_word_id, &max_word_id);
			WP_SELECT_TRACE_LOGW(kTag,
				"selection topup source empty dict_words=%d dict_min_id=%d dict_max_id=%d cursor=%d textbook=%s has_id_range=%d",
				CountDictionaryWords(dict_db.get()),
				has_id_range ? min_word_id : 0,
				has_id_range ? max_word_id : 0,
				resolved_next_new_word_id,
				textbook_name.c_str(),
				has_id_range ? 1 : 0);
		}
		if (topup_word_ids.empty()) {
			resolved_next_new_word_id = cursor_before_topup;
		} else if (!eteacher::database_manager::BeginTransaction(db.get(), kTag)) {
			ESP_LOGW(kTag,
				"selection topup begin transaction failed cursor=%d",
				resolved_next_new_word_id);
		} else {
			resolved_next_new_word_id = topup_cursor_after_candidates;
			inserted_profiles = InsertProfileSeeds(db.get(), normalized_user_id, topup_word_ids, textbook_name);
			if (!eteacher::database_manager::CommitTransaction(db.get(), kTag)) {
				ESP_LOGW(kTag,
					"selection topup commit failed cursor=%d inserted=%d",
					resolved_next_new_word_id,
					inserted_profiles);
				eteacher::database_manager::RollbackTransaction(db.get(), kTag);
				inserted_profiles = 0;
				resolved_next_new_word_id = cursor_before_topup;
			}
		}
		if (inserted_profiles == 0 && !topup_word_ids.empty()) {
			WP_SELECT_TRACE_LOGW(kTag,
				"selection topup inserted nothing despite candidates cursor=%d candidates=%d textbook=%s",
				cursor_before_topup,
				static_cast<int>(topup_word_ids.size()),
				textbook_name.c_str());
		}
	}
	const int64_t topup_ms = NowMs() - topup_start_ms;
	int64_t inventory_after_ms = 0;
	if (kEnableSelectionInventoryLogging) {
		const int64_t inventory_after_start_ms = NowMs();
		LogSelectionProfileInventory(db.get(), normalized_user_id, textbook_name, now_sec);
		inventory_after_ms = NowMs() - inventory_after_start_ms;
	}

	const int64_t select_profile_start_ms = NowMs();
	AppendSelectedProfileWords(
		db.get(),
		dict_db.get(),
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
		"selected words summary total=%d active_profile_count=%d inserted_profiles=%d cursor_after=%d actual_review=%d actual_new=%d",
		static_cast<int>(selected_words.size()),
		active_profile_count,
		inserted_profiles,
		resolved_next_new_word_id,
		static_cast<int>(std::count_if(selected_words.begin(), selected_words.end(), [](const SelectedWord &word) {
			return word.is_review;
		})),
		static_cast<int>(std::count_if(selected_words.begin(), selected_words.end(), [](const SelectedWord &word) {
			return !word.is_review;
		})));

	WP_SELECT_TRACE_LOGW(kTag,
		"selection timing total_ms=%d discover_db_ms=%d open_user_db_ms=%d attach_dict_ms=%d inventory_before_ms=%d active_profile_ms=%d existing_profile_ms=%d topup_ms=%d inventory_after_ms=%d select_profile_ms=%d selected=%d",
		static_cast<int>(NowMs() - select_start_ms),
		static_cast<int>(discover_db_ms - select_start_ms),
		static_cast<int>(open_user_db_ms),
		static_cast<int>(attach_dict_ms),
		static_cast<int>(inventory_before_ms),
		static_cast<int>(active_profile_ms),
		static_cast<int>(existing_profile_ms),
		static_cast<int>(topup_ms),
		static_cast<int>(inventory_after_ms),
		static_cast<int>(select_profile_ms),
		static_cast<int>(selected_words.size()));

	return selected_words;
}
}  // namespace word_practice
