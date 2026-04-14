#include "eteacher/apps/word_practice/word_practice_selection_module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <esp_random.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <sqlite3.h>

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

constexpr const char *kTag = "WordPracticeSelect";
constexpr int kDefaultUserId = 0;
constexpr int kMaxQuestionType = 12;

struct SqliteDbCloser {
	void operator()(sqlite3 *db) const {
		if (db != nullptr) {
			sqlite3_close(db);
		}
	}
};

struct SqliteStmtFinalizer {
	void operator()(sqlite3_stmt *stmt) const {
		if (stmt != nullptr) {
			sqlite3_finalize(stmt);
		}
	}
};

bool PrepareStatement(sqlite3 *db,
				  const std::string &sql,
				  std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> *out_stmt);

int64_t NowSec() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000000ULL);
}

int QuerySingleInt(sqlite3 *db, const std::string &sql, int fallback = 0) {
	if (db == nullptr) {
		return fallback;
	}

	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return fallback;
	}

	if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
		return sqlite3_column_int(stmt.get(), 0);
	}
	return fallback;
}

std::string LevelToStage(int current_level) {
	static constexpr std::array<const char *, 5> kStages = {"primary", "middle", "high", "cet4", "cet6"};
	const size_t stage_index = static_cast<size_t>(std::min(5, std::max(1, current_level)) - 1);
	return kStages[stage_index];
}

std::string TextbookNameForQuestion(const QuestionData &question) {
	return question.stage.empty() ? "default" : question.stage;
}

float RandomFloatInRange(float min_value, float max_value) {
	constexpr float kUint32Max = 4294967295.0f;
	const float ratio = static_cast<float>(esp_random()) / kUint32Max;
	return min_value + ((max_value - min_value) * ratio);
}

bool HasQuestionType(const std::vector<QuestionData> &question_pool, int question_type) {
	for (const auto &question : question_pool) {
		if (question.type == question_type) {
			return true;
		}
	}
	return false;
}

size_t PickRandomQuestionIndexByType(const std::vector<QuestionData> &question_pool, int selected_type) {
	size_t chosen_index = 0;
	size_t match_count = 0;
	for (size_t index = 0; index < question_pool.size(); ++index) {
		if (question_pool[index].type != selected_type) {
			continue;
		}
		++match_count;
		if (match_count == 1 || (esp_random() % static_cast<uint32_t>(match_count)) == 0) {
			chosen_index = index;
		}
	}
	return chosen_index;
}

float ComputeLegacyQuestionScore(const QuestionData &question,
				 const std::string &target_stage,
				 int target_difficulty,
				 int recent_type,
				 const LearnedSnapshot &learned) {
	const int total = learned.correct + learned.wrong;
	const float accuracy = total > 0 ? static_cast<float>(learned.correct) / static_cast<float>(total) : 0.0f;
	const float weak_bonus = total > 0 ? (1.0f - accuracy) : 0.8f;

	const int64_t age_sec = std::max<int64_t>(0, NowSec() - learned.last_seen_at);
	const float forgetting_curve = (learned.last_seen_at <= 0)
		? 1.0f
		: std::min(1.5f, static_cast<float>(age_sec) / 3600.0f * 0.2f);

	const float difficulty_match = std::max<float>(0.0f, 1.0f - 0.25f * static_cast<float>(std::abs(question.difficulty - target_difficulty)));
	const float stage_match = (question.stage == target_stage) ? 1.0f : (question.stage.empty() ? 0.4f : -0.4f);
	const float type_cycle = (recent_type == question.type) ? -0.6f : 0.6f;

	return 2.0f * weak_bonus +
		   1.5f * forgetting_curve +
		   1.0f * difficulty_match +
		   0.8f * stage_match +
		   type_cycle +
		   RandomFloatInRange(-0.15f, 0.15f);
}

bool PrepareStatement(sqlite3 *db,
				  const std::string &sql,
				  std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> *out_stmt) {
	if (db == nullptr || out_stmt == nullptr) {
		return false;
	}
	sqlite3_stmt *raw_stmt = nullptr;
	const int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &raw_stmt, nullptr);
	if (rc != SQLITE_OK || raw_stmt == nullptr) {
		ESP_LOGW(kTag, "prepare failed rc=%d msg=%s sql=%s", rc, sqlite3_errmsg(db), sql.c_str());
		if (raw_stmt != nullptr) {
			sqlite3_finalize(raw_stmt);
		}
		return false;
	}
	out_stmt->reset(raw_stmt);
	return true;
}

bool TableExists(sqlite3 *db, const char *schema_name, const char *table_name) {
	if (db == nullptr || schema_name == nullptr || table_name == nullptr) {
		return false;
	}

	const std::string sql =
		"SELECT 1 FROM " + std::string(schema_name) + ".sqlite_master WHERE type='table' AND name=? LIMIT 1;";
	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
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
	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
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
	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
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
	esp_log_write(ESP_LOG_WARN, kTag, "RESOURCE_OK kind=db scope=dictionary action=attach path=%s method=ATTACH DATABASE alias=%s", db_path.c_str(), alias);
	return true;
}

bool ContainsWordId(const std::vector<SelectedWord> &selected_words, int word_id) {
	for (const auto &selected : selected_words) {
		if (selected.word_id == word_id) {
			return true;
		}
	}
	return false;
}

std::unordered_set<int> LoadKnownWordIds(sqlite3 *db, int user_id, bool can_join_through_vocab_items) {
	std::unordered_set<int> word_ids;
	if (db == nullptr) {
		return word_ids;
	}

	const char *sql = can_join_through_vocab_items
		? "SELECT word_id FROM vocab_items WHERE user_id = ? AND word_id IS NOT NULL AND (is_deleted = 0 OR is_deleted IS NULL);"
		: "SELECT vocab_id FROM vocab_learning_state WHERE user_id = ? AND vocab_id IS NOT NULL;";

	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return word_ids;
	}

	if (sqlite3_bind_int(stmt.get(), 1, user_id) != SQLITE_OK) {
		ESP_LOGW(kTag, "bind known-word query failed user_id=%d", user_id);
		return word_ids;
	}

	while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
		const int word_id = sqlite3_column_int(stmt.get(), 0);
		if (word_id > 0) {
			word_ids.insert(word_id);
		}
	}

	return word_ids;
}

int ResolveSelectionUserId(sqlite3 *db, int requested_user_id) {
	if (db == nullptr) {
		return requested_user_id;
	}

	const char *sql =
		"SELECT user_id FROM vocab_learning_state WHERE user_id IS NOT NULL ORDER BY user_id ASC LIMIT 1;";
	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return requested_user_id;
	}

	if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
		const int discovered_user_id = sqlite3_column_int(stmt.get(), 0);
		if (discovered_user_id >= 0) {
			return discovered_user_id;
		}
	}
	return requested_user_id;
}

void AppendRandomNewWords(sqlite3 *db,
					 int user_id,
					 bool can_join_through_vocab_items,
					 size_t limit,
					 std::vector<SelectedWord> *selected_words) {
	if (db == nullptr || selected_words == nullptr || limit == 0) {
		return;
	}

	std::unordered_set<int> excluded_word_ids = LoadKnownWordIds(db, user_id, can_join_through_vocab_items);
	for (const auto &selected : *selected_words) {
		if (selected.word_id > 0) {
			excluded_word_ids.insert(selected.word_id);
		}
	}
	ESP_LOGW(kTag,
		"new-word selection start user_id=%d excluded=%d limit=%d",
		user_id,
		static_cast<int>(excluded_word_ids.size()),
		static_cast<int>(limit));

	const char *sql =
		"SELECT w.id, w.word, COALESCE(w.image, '') "
		"FROM dictdb.word AS w "
		"WHERE w.word IS NOT NULL AND w.word <> '' "
		"ORDER BY w.id ASC;";

	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return;
	}

	const size_t initial_size = selected_words->size();
	int scanned_rows = 0;
	int appended_rows = 0;
	int final_rc = SQLITE_DONE;
	while ((final_rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
		++scanned_rows;
		if (selected_words->size() - initial_size >= limit) {
			break;
		}

		const int word_id = sqlite3_column_int(stmt.get(), 0);
		const unsigned char *word_text = sqlite3_column_text(stmt.get(), 1);
		const unsigned char *image_text = sqlite3_column_text(stmt.get(), 2);
		if (word_id <= 0 || word_text == nullptr) {
			continue;
		}
		if (excluded_word_ids.find(word_id) != excluded_word_ids.end()) {
			continue;
		}

		SelectedWord selected;
		selected.word_id = word_id;
		selected.word = reinterpret_cast<const char *>(word_text);
		selected.image = image_text != nullptr ? reinterpret_cast<const char *>(image_text) : "";
		selected.is_review = false;
		selected_words->push_back(std::move(selected));
		excluded_word_ids.insert(word_id);
		++appended_rows;
	}
	ESP_LOGW(kTag,
		"new-word selection finished scanned=%d appended=%d final_rc=%d total_after=%d",
		scanned_rows,
		appended_rows,
		final_rc,
		static_cast<int>(selected_words->size()));
}

void AppendSelectedWords(sqlite3 *db,
				   const std::string &sql,
				   int user_id,
				   int64_t now_sec,
				   size_t limit,
				   bool is_review,
				   std::vector<SelectedWord> *selected_words) {
	if (db == nullptr || selected_words == nullptr || limit == 0) {
		return;
	}
	const size_t initial_size = selected_words->size();

	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return;
	}

	int param_index = 1;
	sqlite3_bind_int(stmt.get(), param_index++, user_id);
	if (sql.find("next_review_at") != std::string::npos) {
		sqlite3_bind_int64(stmt.get(), param_index++, static_cast<sqlite3_int64>(now_sec));
	}
	sqlite3_bind_int(stmt.get(), param_index, static_cast<int>(limit));

	while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
		if (selected_words->size() - initial_size >= limit) {
			break;
		}
		const int word_id = sqlite3_column_int(stmt.get(), 0);
		const unsigned char *word_text = sqlite3_column_text(stmt.get(), 1);
		const unsigned char *image_text = sqlite3_column_text(stmt.get(), 2);
		if (word_id <= 0 || word_text == nullptr) {
			continue;
		}
		if (ContainsWordId(*selected_words, word_id)) {
			continue;
		}

		SelectedWord selected;
		selected.word_id = word_id;
		selected.word = reinterpret_cast<const char *>(word_text);
		selected.image = image_text != nullptr ? reinterpret_cast<const char *>(image_text) : "";
		selected.is_review = is_review;
		selected_words->push_back(std::move(selected));
	}
}

}  // namespace

void SelectionModule::LoadQuestionPool(std::vector<QuestionData> pool) {
	question_pool_ = std::move(pool);
	ResetProgress();
}

void SelectionModule::ResetProgress() {
	recent_types_.clear();
	next_question_type_cursor_ = 1;
}

std::vector<SelectedWord> SelectionModule::SelectWordsFromVocabulary(const WordSelectionConfig &config,
							 int user_id) const {
	std::vector<SelectedWord> selected_words;
	const int total_count = std::max(0, config.TotalCount());
	if (total_count <= 0) {
		ESP_LOGW(kTag, "skip vocabulary selection: invalid total_count=%d", total_count);
		return selected_words;
	}

	const int normalized_user_id = user_id < 0 ? kDefaultUserId : user_id;
	const int review_target = std::max(0, std::min(total_count, config.review_word_count));
	const int new_target = std::max(0, std::min(total_count - review_target, config.new_word_count));

	if (!eteacher::database_manager::EnsureSqliteRuntimeReady(kTag) ||
		!eteacher::database_manager::EnsureSqliteSdMounted(kTag)) {
		ESP_LOGW(kTag, "vocabulary selection aborted: sqlite runtime or sd mount not ready");
		return selected_words;
	}

	const std::string user_db_path = eteacher::database_manager::DiscoverUserDataDbPath(kTag, "vocab_learning_state");
	const std::string words_db_path = eteacher::database_manager::DiscoverDictionaryDbPath(kTag);
	ESP_LOGW(kTag, "selection db paths user=%s words=%s",
		user_db_path.empty() ? "(missing)" : user_db_path.c_str(),
		words_db_path.empty() ? "(missing)" : words_db_path.c_str());
	if (user_db_path.empty() || words_db_path.empty()) {
		ESP_LOGW(kTag, "vocabulary selection aborted: user_db=%s words_db=%s",
			user_db_path.empty() ? "missing" : user_db_path.c_str(),
			words_db_path.empty() ? "missing" : words_db_path.c_str());
		return selected_words;
	}

	sqlite3 *raw_db = nullptr;
	const int open_rc = sqlite3_open_v2(user_db_path.c_str(), &raw_db, SQLITE_OPEN_READONLY, nullptr);
	if (open_rc != SQLITE_OK || raw_db == nullptr) {
		ESP_LOGW(kTag, "open user db failed rc=%d msg=%s", open_rc, raw_db ? sqlite3_errmsg(raw_db) : "null");
		if (raw_db != nullptr) {
			sqlite3_close(raw_db);
		}
		return selected_words;
	}
	esp_log_write(ESP_LOG_WARN, kTag, "RESOURCE_OK kind=db scope=user action=open path=%s method=sqlite3_open_v2(READONLY) caller=SelectWordsFromVocabulary", user_db_path.c_str());
	std::unique_ptr<sqlite3, SqliteDbCloser> db(raw_db);

	if (!AttachReadonlyDb(db.get(), words_db_path, "dictdb")) {
		ESP_LOGW(kTag, "attach words db failed: %s", words_db_path.c_str());
		return selected_words;
	}

	const bool has_vocab_learning_state = TableExists(db.get(), "main", "vocab_learning_state");
	const bool has_vocab_items = TableExists(db.get(), "main", "vocab_items");
	const bool has_word_table = TableExists(db.get(), "dictdb", "word");
	const bool has_vocab_items_id = ColumnExists(db.get(), "main", "vocab_items", "id");
	const bool has_vocab_word_id = ColumnExists(db.get(), "main", "vocab_items", "word_id");
	const bool has_vocab_entry_id = ColumnExists(db.get(), "main", "vocab_items", "entry_id");
	const bool has_word_id = ColumnExists(db.get(), "dictdb", "word", "id");
	const bool has_word_word = ColumnExists(db.get(), "dictdb", "word", "word");
	if (!has_vocab_learning_state || !has_word_table || !has_word_id || !has_word_word) {
		ESP_LOGW(kTag,
			"vocabulary selection aborted: schema mismatch learning_state=%d vocab_items=%d vocab_items.id=%d word_id=%d entry_id=%d dict.word=%d dict.word.id=%d dict.word.word=%d",
			has_vocab_learning_state,
			has_vocab_items,
			has_vocab_items_id,
			has_vocab_word_id,
			has_vocab_entry_id,
			has_word_table,
			has_word_id,
			has_word_word);
		return selected_words;
	}

	const int total_words = QuerySingleInt(db.get(), "SELECT COUNT(*) FROM dictdb.word;", -1);
	const int total_learning_states = QuerySingleInt(db.get(), "SELECT COUNT(*) FROM vocab_learning_state;", -1);
	const int total_vocab_items = has_vocab_items ? QuerySingleInt(db.get(), "SELECT COUNT(*) FROM vocab_items;", -1) : -1;
	const bool can_join_through_vocab_items =
		has_vocab_items &&
		has_vocab_items_id &&
		(has_vocab_word_id || has_vocab_entry_id) &&
		total_vocab_items > 0;
	const char *word_ref_column = has_vocab_word_id ? "word_id" : "entry_id";
	const int requested_learning_states = QuerySingleInt(
		db.get(),
		"SELECT COUNT(*) FROM vocab_learning_state WHERE user_id = " + std::to_string(normalized_user_id) + ";",
		-1);
	int effective_user_id = normalized_user_id;
	if (requested_learning_states == 0) {
		effective_user_id = ResolveSelectionUserId(db.get(), normalized_user_id);
	}
	const int effective_learning_states = QuerySingleInt(
		db.get(),
		"SELECT COUNT(*) FROM vocab_learning_state WHERE user_id = " + std::to_string(effective_user_id) + ";",
		-1);
	ESP_LOGW(kTag,
		"selection db stats words=%d learning_state=%d vocab_items=%d requested_user=%d requested_user_rows=%d effective_user=%d effective_user_rows=%d can_join_vocab_items=%d",
		total_words,
		total_learning_states,
		total_vocab_items,
		normalized_user_id,
		requested_learning_states,
		effective_user_id,
		effective_learning_states,
		can_join_through_vocab_items);
	const int64_t now_sec = NowSec();
	if (review_target > 0) {
		const std::string review_sql = can_join_through_vocab_items
			? (
				"SELECT w.id, w.word, COALESCE(w.image, '') "
				"FROM vocab_learning_state AS s "
				"INNER JOIN vocab_items AS vi ON vi.id = s.vocab_id "
				"INNER JOIN dictdb.word AS w ON w.id = vi." + std::string(word_ref_column) + " "
				"WHERE s.user_id = ? "
				"AND (vi.is_deleted = 0 OR vi.is_deleted IS NULL) "
				"AND (s.next_review_at IS NULL OR s.next_review_at <= ?) "
				"AND w.word IS NOT NULL AND TRIM(w.word) <> '' "
				"ORDER BY COALESCE(s.next_review_at, 0) ASC, COALESCE(s.familiarity, 0) ASC, COALESCE(s.repetition, 0) ASC "
				"LIMIT ?;")
			: (
				"SELECT w.id, w.word, COALESCE(w.image, '') "
				"FROM vocab_learning_state AS s "
				"INNER JOIN dictdb.word AS w ON w.id = s.vocab_id "
				"WHERE s.user_id = ? "
				"AND (s.next_review_at IS NULL OR s.next_review_at <= ?) "
				"AND w.word IS NOT NULL AND TRIM(w.word) <> '' "
				"ORDER BY COALESCE(s.next_review_at, 0) ASC, COALESCE(s.familiarity, 0) ASC, COALESCE(s.repetition, 0) ASC "
				"LIMIT ?;");
		AppendSelectedWords(db.get(), review_sql, effective_user_id, now_sec, static_cast<size_t>(review_target), true, &selected_words);
	}

	const int remaining = std::min(new_target, total_count - static_cast<int>(selected_words.size()));
	if (remaining > 0) {
		AppendRandomNewWords(
			db.get(),
			effective_user_id,
			can_join_through_vocab_items,
			static_cast<size_t>(remaining),
			&selected_words);
	}

	ESP_LOGW(kTag,
		"selected words summary total=%d requested_review=%d actual_review=%d actual_new=%d",
		static_cast<int>(selected_words.size()),
		review_target,
		static_cast<int>(std::count_if(selected_words.begin(), selected_words.end(), [](const SelectedWord &word) {
			return word.is_review;
		})),
		static_cast<int>(std::count_if(selected_words.begin(), selected_words.end(), [](const SelectedWord &word) {
			return !word.is_review;
		})));

	for (const auto &selected : selected_words) {
		ESP_LOGW(kTag,
			"selected %s word_id=%d word=%s",
			selected.is_review ? "review" : "new",
			selected.word_id,
			selected.word.c_str());
	}

	return selected_words;
}

const std::vector<QuestionData> &SelectionModule::QuestionPool() const {
	return question_pool_;
}

const QuestionData *SelectionModule::GetQuestion(size_t index) const {
	if (index >= question_pool_.size()) {
		return nullptr;
	}
	return &question_pool_[index];
}

bool SelectionModule::Empty() const {
	return question_pool_.empty();
}

SelectionResult SelectionModule::SelectNext(int total_answered,
							int current_level,
							QuestionSelectionStrategy strategy,
							const LearnedSnapshotProvider &provider) {
	if (question_pool_.empty()) {
		return {};
	}

	SelectionResult result;
	if (strategy == QuestionSelectionStrategy::LegacyAdaptive) {
		result = SelectByLegacyAdaptive(total_answered, current_level, provider);
		if (!result.has_value) {
			result = SelectByTypeCycleRandom();
			if (result.has_value) {
				result.used_strategy = QuestionSelectionStrategy::TypeCycleRandom;
			}
		}
	} else {
		result = SelectByTypeCycleRandom();
		if (!result.has_value) {
			result = SelectByLegacyAdaptive(total_answered, current_level, provider);
			if (result.has_value) {
				result.used_strategy = QuestionSelectionStrategy::LegacyAdaptive;
			}
		}
	}

	if (result.has_value) {
		RecordSelectedType(question_pool_[result.selected_index].type);
	}
	return result;
}

SelectionResult SelectionModule::SelectByLegacyAdaptive(int total_answered,
								int current_level,
								const LearnedSnapshotProvider &provider) const {
	SelectionResult result;
	if (question_pool_.empty()) {
		return result;
	}

	const std::string target_stage = LevelToStage(current_level);
	const int target_difficulty = std::min(6, 1 + total_answered / 2);
	const int recent_type = recent_types_.empty() ? -1 : recent_types_.back();
	const int expected_type = 1 + (total_answered % kMaxQuestionType);

	const bool has_expected_type = HasQuestionType(question_pool_, expected_type);

	float best_score = -10000.0f;
	size_t best_index = 0;
	for (size_t index = 0; index < question_pool_.size(); ++index) {
		const auto &question = question_pool_[index];
		if (has_expected_type && question.type != expected_type) {
			continue;
		}

		const LearnedSnapshot learned = provider ? provider(question, TextbookNameForQuestion(question)) : LearnedSnapshot{};
		const float score = ComputeLegacyQuestionScore(question, target_stage, target_difficulty, recent_type, learned);

		if (score > best_score) {
			best_score = score;
			best_index = index;
		}
	}

	result.has_value = true;
	result.selected_index = best_index;
	result.used_strategy = QuestionSelectionStrategy::LegacyAdaptive;
	return result;
}

SelectionResult SelectionModule::SelectByTypeCycleRandom() const {
	SelectionResult result;
	if (question_pool_.empty()) {
		return result;
	}

	int selected_type = -1;
	for (int offset = 0; offset < kMaxQuestionType; ++offset) {
		const int candidate_type = ((next_question_type_cursor_ - 1 + offset) % kMaxQuestionType) + 1;
		bool exists = false;
		for (const auto &question : question_pool_) {
			if (question.type == candidate_type) {
				exists = true;
				break;
			}
		}
		if (exists) {
			selected_type = candidate_type;
			break;
		}
	}

	if (selected_type < 1) {
		return result;
	}
	if (!HasQuestionType(question_pool_, selected_type)) {
		return result;
	}

	result.has_value = true;
	result.selected_index = PickRandomQuestionIndexByType(question_pool_, selected_type);
	result.used_strategy = QuestionSelectionStrategy::TypeCycleRandom;
	return result;
}

void SelectionModule::RecordSelectedType(int question_type) {
	recent_types_.push_back(question_type);
	if (recent_types_.size() > 6) {
		recent_types_.erase(recent_types_.begin());
	}
	next_question_type_cursor_ = (question_type % kMaxQuestionType) + 1;
}

}  // namespace word_practice