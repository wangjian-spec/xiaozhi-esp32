#include "eteacher/apps/word_practice/word_practice_selection_module.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <sqlite3.h>

#include "esp_timer.h"
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
#define WP_SELECT_TRACE_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)

namespace word_practice {
namespace {

constexpr int kDefaultUserId = 0;
constexpr int kMinimumActiveProfileWords = 20;
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

bool EnsureWordLearningProfileTables(sqlite3 *db) {
	if (db == nullptr) {
		return false;
	}
	const char *sql_profile =
		"CREATE TABLE IF NOT EXISTS word_learning_profile ("
		"user_id INTEGER NOT NULL,"
		"word_id INTEGER NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"stage INTEGER DEFAULT 0,"
		"familiarity INTEGER DEFAULT 0,"
		"stability INTEGER DEFAULT 0,"
		"recognition_score INTEGER DEFAULT 0,"
		"recall_score INTEGER DEFAULT 0,"
		"output_score INTEGER DEFAULT 0,"
		"next_review_at INTEGER DEFAULT 0,"
		"lapse_count INTEGER DEFAULT 0,"
		"last_practiced_at INTEGER DEFAULT 0,"
		"last_decay_at INTEGER DEFAULT 0,"
		"last_error_at INTEGER DEFAULT 0,"
		"consecutive_correct INTEGER DEFAULT 0,"
		"consecutive_wrong INTEGER DEFAULT 0,"
		"consecutive_recall_correct INTEGER DEFAULT 0,"
		"recent_review_failed INTEGER DEFAULT 0,"
		"last_response_time_ms INTEGER DEFAULT 0,"
		"mastered INTEGER DEFAULT 0,"
		"downgraded_from_stage INTEGER DEFAULT -1,"
		"PRIMARY KEY(user_id, word_id, textbook_name)"
		");";
	const char *sql_profile_index =
		"CREATE INDEX IF NOT EXISTS idx_word_learning_profile_user_next_review "
		"ON word_learning_profile(user_id, next_review_at);";
	const char *sql_pick_candidate_index =
		"CREATE INDEX IF NOT EXISTS idx_word_learning_profile_pick_candidate "
		"ON word_learning_profile(user_id, textbook_name, mastered, stage, familiarity, stability, recall_score, output_score, next_review_at, word_id) "
		"WHERE mastered = 0;";
	return ExecSql(db, sql_profile) && ExecSql(db, sql_profile_index) && ExecSql(db, sql_pick_candidate_index);
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

bool InsertProfileSeed(sqlite3 *db, int user_id, int word_id, const std::string &textbook_name) {
	if (db == nullptr || word_id <= 0 || textbook_name.empty()) {
		return false;
	}
	const char *sql =
		"INSERT OR IGNORE INTO word_learning_profile("
		"user_id, word_id, textbook_name, stage, familiarity, stability, recognition_score, recall_score, output_score, next_review_at, lapse_count, last_practiced_at, last_decay_at, last_error_at, consecutive_correct, consecutive_wrong, consecutive_recall_correct, recent_review_failed, last_response_time_ms, mastered, downgraded_from_stage"
		") VALUES(?, ?, ?, 1, 8, 6, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}
	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 2, word_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
		return false;
	}
	return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

int AppendProfilesFromStageCursor(sqlite3 *db,
					  int user_id,
					  const std::string &textbook_name,
					  int start_after_word_id,
					  size_t limit,
					  int *next_new_word_id) {
	if (db == nullptr || limit == 0 || next_new_word_id == nullptr) {
		return 0;
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
		return 0;
	}
	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_text(stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 3, std::max(0, start_after_word_id)) != SQLITE_OK ||
		sqlite3_bind_int(stmt.get(), 4, static_cast<int>(limit)) != SQLITE_OK) {
		return 0;
	}

	int inserted_count = 0;
	int latest_word_id = std::max(0, start_after_word_id);
	while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
		const int word_id = sqlite3_column_int(stmt.get(), 0);
		if (word_id <= 0) {
			continue;
		}
		if (!InsertProfileSeed(db, user_id, word_id, textbook_name)) {
			continue;
		}
		latest_word_id = word_id;
		++inserted_count;
	}
	*next_new_word_id = latest_word_id;
	return inserted_count;
}

int TopUpProfilesFromStage(sqlite3 *db,
				   int user_id,
				   const std::string &textbook_name,
				   int start_after_word_id,
				   size_t limit,
				   int *next_new_word_id) {
	if (db == nullptr || next_new_word_id == nullptr || limit == 0) {
		return 0;
	}
	int inserted_count = AppendProfilesFromStageCursor(
		db,
		user_id,
		textbook_name,
		start_after_word_id,
		limit,
		next_new_word_id);
	if (static_cast<size_t>(inserted_count) >= limit) {
		return inserted_count;
	}
	const size_t remaining = limit - static_cast<size_t>(inserted_count);
	if (start_after_word_id > 0 && remaining > 0) {
		int wrapped_cursor = *next_new_word_id;
		inserted_count += AppendProfilesFromStageCursor(
			db,
			user_id,
			textbook_name,
			0,
			remaining,
			&wrapped_cursor);
		*next_new_word_id = wrapped_cursor;
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

void AppendSelectedProfileWords(sqlite3 *db,
					  int user_id,
					  const std::string &textbook_name,
					  size_t limit,
					  std::vector<SelectedWord> *selected_words) {
	if (db == nullptr || selected_words == nullptr || limit == 0) {
		return;
	}
	const bool has_word_image = ColumnExists(db, "dictdb", "word", "image");
	const std::string profile_sql =
		"SELECT p.word_id, CASE WHEN COALESCE(p.last_practiced_at, 0) > 0 THEN 1 ELSE 0 END "
		"FROM word_learning_profile AS p "
		"WHERE p.user_id = ? "
		"AND p.textbook_name = ? "
		"AND COALESCE(p.mastered, 0) = 0 "
		"ORDER BY COALESCE(p.stage, 0) ASC, COALESCE(p.familiarity, 0) ASC, COALESCE(p.stability, 0) ASC, COALESCE(p.recall_score, 0) ASC, COALESCE(p.output_score, 0) ASC, COALESCE(p.next_review_at, 0) ASC, p.word_id ASC "
		"LIMIT ?;";
	StatementPtr profile_stmt;
	if (!PrepareStatement(db, profile_sql, &profile_stmt)) {
		WP_SELECT_TRACE_LOGW(kTag,
			"append selected profiles prepare failed textbook=%s msg=%s",
			textbook_name.c_str(),
			sqlite3_errmsg(db));
		return;
	}
	if (sqlite3_bind_int(profile_stmt.get(), 1, user_id) != SQLITE_OK ||
		sqlite3_bind_text(profile_stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
		sqlite3_bind_int(profile_stmt.get(), 3, static_cast<int>(limit)) != SQLITE_OK) {
		return;
	}
	std::vector<std::pair<int, bool>> selected_profile_ids;
	selected_profile_ids.reserve(limit);
	int step_rc = SQLITE_ROW;
	while ((step_rc = sqlite3_step(profile_stmt.get())) == SQLITE_ROW) {
		const int word_id = sqlite3_column_int(profile_stmt.get(), 0);
		const bool is_review = sqlite3_column_int(profile_stmt.get(), 1) != 0;
		if (word_id <= 0) {
			continue;
		}
		selected_profile_ids.emplace_back(word_id, is_review);
	}
	if (step_rc != SQLITE_DONE) {
		WP_SELECT_TRACE_LOGW(kTag,
			"append selected profiles step failed textbook=%s rc=%d msg=%s selected=%d",
			textbook_name.c_str(),
			step_rc,
			sqlite3_errmsg(db),
			static_cast<int>(selected_words->size()));
		return;
	}
	if (selected_profile_ids.empty()) {
		return;
	}
	const std::string dict_sql = BuildDictionaryWordLookupSql(selected_profile_ids.size(), has_word_image);
	StatementPtr dict_stmt;
	if (!PrepareStatement(db, dict_sql, &dict_stmt)) {
		WP_SELECT_TRACE_LOGW(kTag,
			"append selected profiles dict prepare failed textbook=%s has_word_image=%d msg=%s",
			textbook_name.c_str(),
			has_word_image ? 1 : 0,
			sqlite3_errmsg(db));
		return;
	}
	for (size_t index = 0; index < selected_profile_ids.size(); ++index) {
		if (sqlite3_bind_int(dict_stmt.get(), static_cast<int>(index) + 1, selected_profile_ids[index].first) != SQLITE_OK) {
			return;
		}
	}
	std::unordered_map<int, SelectedWord> dictionary_words;
	dictionary_words.reserve(selected_profile_ids.size());
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
		WP_SELECT_TRACE_LOGW(kTag,
			"append selected profiles dict step failed textbook=%s rc=%d msg=%s selected=%d",
			textbook_name.c_str(),
			step_rc,
			sqlite3_errmsg(db),
			static_cast<int>(dictionary_words.size()));
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

}  // namespace

void SelectionModule::ResetProgress() {
}

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
		config.TotalCount());
	if (next_new_word_id != nullptr) {
		*next_new_word_id = resolved_next_new_word_id;
	}
	const int64_t select_start_ms = NowMs();
	const int total_count = std::max(0, config.TotalCount());
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
	if (!EnsureWordLearningProfileTables(db.get())) {
		WP_SELECT_TRACE_LOGW(kTag, "selection abort ensure word_learning_profile failed");
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
	const int64_t topup_start_ms = NowMs();
	const int due_review_count = CountDueReviewWords(db.get(), normalized_user_id, textbook_name, now_sec);
	int inserted_profiles = 0;
	if (due_review_count < kMinimumActiveProfileWords) {
		const int missing_count = kMinimumActiveProfileWords - due_review_count;
		if (ExecSql(db.get(), "BEGIN IMMEDIATE TRANSACTION;")) {
			inserted_profiles = TopUpProfilesFromStage(
				db.get(),
				normalized_user_id,
				textbook_name,
				resolved_next_new_word_id,
				static_cast<size_t>(missing_count),
				&resolved_next_new_word_id);
			if (!ExecSql(db.get(), "COMMIT;")) {
				(void)ExecSql(db.get(), "ROLLBACK;");
				inserted_profiles = 0;
			}
		}
	}
	const int64_t topup_ms = NowMs() - topup_start_ms;

	const int64_t select_profile_start_ms = NowMs();
	AppendSelectedProfileWords(
		db.get(),
		normalized_user_id,
		textbook_name,
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
