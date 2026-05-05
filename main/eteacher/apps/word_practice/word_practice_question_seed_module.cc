#include "eteacher/apps/word_practice/word_practice_question_seed_module.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <sqlite3.h>

#include "eteacher/app_ui/common_ui_utils.h"
#include "eteacher/apps/word_practice/word_practice_config.h"
#include "eteacher/apps/word_practice/word_practice_db_utils.h"
#include "eteacher/apps/word_practice/word_practice_time_utils.h"
#include "eteacher/apps/word_practice/word_practice_utils.h"
#include "eteacher/database_manager/database_debug.h"
#include "eteacher/database_manager/sqlite_db_api.h"

#define WP_SEED_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define WP_SEED_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)

namespace {

constexpr const char *kTag = "WordPracticeSeed";
constexpr size_t kImageChoiceOptionCount = 3;
constexpr size_t kStandardChoiceOptionMinCount = 3;

using word_practice::utils::BasenameFromPath;
using word_practice::utils::BuildQuestionAudioPath;
using word_practice::utils::NormalizeAudioEntryName;
using word_practice::utils::NormalizeLettersOnlyLower;
using word_practice::utils::NormalizeSentenceWordsLower;
using word_practice::utils::SplitHintWords;
using word_practice::utils::StageNumberToTag;
using word_practice::utils::Trim;
using namespace word_practice::config;

void CloseSeedDb(sqlite3 *db, const char *scope) {
	if (db == nullptr) {
		return;
	}
	const int rc = sqlite3_close(db);
	if (rc != SQLITE_OK) {
		ESP_LOGW(kTag,
			"close dictionary db failed scope=%s rc=%d msg=%s",
			scope != nullptr ? scope : "unknown",
			rc,
			sqlite3_errmsg(db));
	}
}

void LogResolvedDbAccess(const char *scope, const std::string &path, const char *method) {
	DB_LOGI(kTag, "RESOURCE_OK kind=db scope=%s action=open path=%s method=%s", scope, path.c_str(), method);
}

std::string SanitizeStemPart(const std::string &value) {
	std::string sanitized;
	sanitized.reserve(value.size());
	for (unsigned char ch : value) {
		if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' ||
			ch == '-') {
			sanitized.push_back(static_cast<char>(ch));
		} else {
			sanitized.push_back('_');
		}
	}
	while (!sanitized.empty() && sanitized.front() == '_') {
		sanitized.erase(sanitized.begin());
	}
	while (!sanitized.empty() && sanitized.back() == '_') {
		sanitized.pop_back();
	}
	return sanitized.empty() ? "record" : sanitized;
}

std::string BuildStage1AssetBaseName(int word_id, const std::string &word_text) {
	if (word_id <= 0) {
		return {};
	}
	return std::to_string(word_id) + "_" + SanitizeStemPart(Trim(word_text));
}

std::string BuildStage1ExampleAudioBaseName(int word_id, const std::string &word_text, int example_id) {
	if (word_id <= 0 || example_id <= 0) {
		return {};
	}
	return std::to_string(word_id) + "_" + SanitizeStemPart(Trim(word_text)) + "_" + std::to_string(example_id);
}

bool LooksLikeAssetReference(const std::string &value, const char *expected_extension) {
	const std::string trimmed = Trim(value);
	if (trimmed.empty()) {
		return false;
	}
	if (trimmed.find('/') != std::string::npos || trimmed.find('\\') != std::string::npos) {
		return true;
	}
	auto lower = trimmed;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	if (expected_extension != nullptr) {
		const std::string ext = expected_extension;
		if (lower.size() >= ext.size() && lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0) {
			return true;
		}
	}
	if (!trimmed.empty() && std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0 &&
		trimmed.find('_') != std::string::npos) {
		return true;
	}
	return false;
}

std::string ResolveStage1AudioName(const std::string &audio_filename, int word_id, const std::string &word_text) {
	std::string name;
	if (LooksLikeAssetReference(audio_filename, ".ogg") || LooksLikeAssetReference(audio_filename, ".mp3") ||
		LooksLikeAssetReference(audio_filename, ".opus")) {
		name = NormalizeAudioEntryName(audio_filename);
	}
	if (!name.empty()) {
		return name;
	}
	const std::string base_name = BuildStage1AssetBaseName(word_id, word_text);
	return base_name.empty() ? std::string() : (base_name + ".ogg");
}

std::string ResolveStage1ExampleAudioName(int word_id, const std::string &word_text, int example_id) {
	const std::string base_name = BuildStage1ExampleAudioBaseName(word_id, word_text, example_id);
	return base_name.empty() ? std::string() : (base_name + ".ogg");
}

std::string ResolveStage1ImageName(const std::string &image_name, int word_id, const std::string &word_text) {
	const std::string base_name = BuildStage1AssetBaseName(word_id, word_text);
	return base_name.empty() ? std::string() : (base_name + ".bin");
}

std::string BuildExampleQuestionAudioPath(const std::string &audio_filename) {
	return eteacher::app_ui::BuildExampleAudioPath(audio_filename);
}

std::vector<std::string> ExtractSentenceDisplayTokens(const std::string &value) {
	std::vector<std::string> words;
	std::string current;
	for (unsigned char ch : value) {
		if (std::isalnum(ch) != 0) {
			current.push_back(static_cast<char>(ch));
		} else if (!current.empty()) {
			words.push_back(std::move(current));
			current.clear();
		}
	}
	if (!current.empty()) {
		words.push_back(std::move(current));
	}
	return words;
}

void ShuffleStringVector(std::vector<std::string> *items) {
	if (items == nullptr || items->size() < 2) {
		return;
	}
	for (size_t index = items->size(); index > 1; --index) {
		const size_t swap_index = static_cast<size_t>(esp_random() % index);
		std::swap((*items)[index - 1], (*items)[swap_index]);
	}
}

bool PreservesAnswerTokenOrder(const std::vector<std::string> &items, const std::vector<std::string> &answer_tokens) {
	if (answer_tokens.empty()) {
		return false;
	}

	std::vector<std::string> normalized_items;
	normalized_items.reserve(items.size());
	for (const auto &item : items) {
		normalized_items.push_back(NormalizeLettersOnlyLower(item));
	}

	size_t matched = 0;
	for (const auto &item : normalized_items) {
		if (matched >= answer_tokens.size()) {
			break;
		}
		if (item == NormalizeLettersOnlyLower(answer_tokens[matched])) {
			++matched;
		}
	}
	return matched == answer_tokens.size();
}

int64_t NowMs() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000ULL);
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

using VocabularySeed = word_practice::VocabularySeed;

struct OptionSeed {
	int word_id = 0;
	std::string word;
	std::string meaning_zh;
	std::string meaning_en;
	std::string image;
	std::string audio_path;
};

using word_practice::db::PrepareStatement;
using word_practice::db::StatementPtr;

void ResetStatement(sqlite3_stmt *stmt) {
	if (stmt == nullptr) {
		return;
	}
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);
}

struct SeedQueryContext {
	StatementPtr meaning_by_word_id_stmt;
	StatementPtr example_by_meaning_id_stmt;
};

std::string BuildFirstMeaningBatchSql(size_t word_count) {
	std::string placeholders;
	placeholders.reserve(word_count * 2);
	for (size_t index = 0; index < word_count; ++index) {
		if (index > 0) {
			placeholders += ",";
		}
		placeholders += "?";
	}
	return
		"SELECT wm.word_id, wm.id, COALESCE(wm.meaning_zh, ''), COALESCE(wm.meaning_en, ''), COALESCE(wm.stage, 1) "
		"FROM word_meaning AS wm "
		"INNER JOIN ("
		"SELECT word_id, MIN(id) AS first_id FROM word_meaning WHERE word_id IN (" + placeholders + ") GROUP BY word_id"
		") AS first_meaning ON first_meaning.word_id = wm.word_id AND first_meaning.first_id = wm.id "
		"ORDER BY wm.word_id ASC;";
}

std::string BuildFirstExampleBatchSql(size_t meaning_count) {
	std::string placeholders;
	placeholders.reserve(meaning_count * 2);
	for (size_t index = 0; index < meaning_count; ++index) {
		if (index > 0) {
			placeholders += ",";
		}
		placeholders += "?";
	}
	return
		"SELECT we.meaning_id, we.id, COALESCE(we.example_en, ''), COALESCE(we.example_zh, ''), COALESCE(we.selection_zh, ''), COALESCE(we.selection_en, '') "
		"FROM word_example AS we "
		"INNER JOIN ("
		"SELECT meaning_id, MIN(id) AS first_id FROM word_example WHERE meaning_id IN (" + placeholders + ") GROUP BY meaning_id"
		") AS first_example ON first_example.meaning_id = we.meaning_id AND first_example.first_id = we.id "
		"ORDER BY we.meaning_id ASC;";
}

bool BindIdList(sqlite3_stmt *stmt, const std::vector<int> &ids) {
	if (stmt == nullptr || ids.empty()) {
		return false;
	}
	for (size_t index = 0; index < ids.size(); ++index) {
		if (sqlite3_bind_int(stmt, static_cast<int>(index) + 1, ids[index]) != SQLITE_OK) {
			return false;
		}
	}
	return true;
}

void LoadFirstMeaningsForSeeds(sqlite3 *db,
				       const std::vector<int> &word_ids,
				       std::unordered_map<int, VocabularySeed> *seed_by_word_id,
				       std::vector<int> *meaning_ids) {
	if (db == nullptr || seed_by_word_id == nullptr || meaning_ids == nullptr || word_ids.empty()) {
		return;
	}
	StatementPtr stmt;
	const std::string sql = BuildFirstMeaningBatchSql(word_ids.size());
	if (!PrepareStatement(db, sql, &stmt)) {
		ESP_LOGW(kTag, "prepare batch meaning lookup failed msg=%s", sqlite3_errmsg(db));
		return;
	}
	if (!BindIdList(stmt.get(), word_ids)) {
		ESP_LOGW(kTag, "bind batch meaning ids failed msg=%s", sqlite3_errmsg(db));
		return;
	}
	for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
		const int word_id = sqlite3_column_int(stmt.get(), 0);
		auto seed_it = seed_by_word_id->find(word_id);
		if (seed_it == seed_by_word_id->end()) {
			continue;
		}
		VocabularySeed &seed = seed_it->second;
		seed.meaning_id = sqlite3_column_int(stmt.get(), 1);
		const unsigned char *meaning_zh_text = sqlite3_column_text(stmt.get(), 2);
		const unsigned char *meaning_en_text = sqlite3_column_text(stmt.get(), 3);
		seed.meaning_zh = meaning_zh_text != nullptr ? reinterpret_cast<const char *>(meaning_zh_text) : "";
		seed.meaning_en = meaning_en_text != nullptr ? reinterpret_cast<const char *>(meaning_en_text) : "";
		seed.stage = sqlite3_column_int(stmt.get(), 4);
		if (seed.meaning_id > 0) {
			meaning_ids->push_back(seed.meaning_id);
		}
	}
}

void LoadFirstExamplesForSeeds(sqlite3 *db,
				       const std::vector<int> &meaning_ids,
				       std::unordered_map<int, VocabularySeed> *seed_by_word_id) {
	if (db == nullptr || seed_by_word_id == nullptr || meaning_ids.empty()) {
		return;
	}
	std::unordered_map<int, VocabularySeed *> seed_by_meaning_id;
	seed_by_meaning_id.reserve(seed_by_word_id->size());
	for (auto &entry : *seed_by_word_id) {
		if (entry.second.meaning_id > 0) {
			seed_by_meaning_id[entry.second.meaning_id] = &entry.second;
		}
	}
	StatementPtr stmt;
	const std::string sql = BuildFirstExampleBatchSql(meaning_ids.size());
	if (!PrepareStatement(db, sql, &stmt)) {
		ESP_LOGW(kTag, "prepare batch example lookup failed msg=%s", sqlite3_errmsg(db));
		return;
	}
	if (!BindIdList(stmt.get(), meaning_ids)) {
		ESP_LOGW(kTag, "bind batch example ids failed msg=%s", sqlite3_errmsg(db));
		return;
	}
	for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
		const int meaning_id = sqlite3_column_int(stmt.get(), 0);
		auto seed_it = seed_by_meaning_id.find(meaning_id);
		if (seed_it == seed_by_meaning_id.end()) {
			continue;
		}
		VocabularySeed *seed = seed_it->second;
		seed->example_id = sqlite3_column_int(stmt.get(), 1);
		const unsigned char *example_en_text = sqlite3_column_text(stmt.get(), 2);
		const unsigned char *example_zh_text = sqlite3_column_text(stmt.get(), 3);
		const unsigned char *selection_zh_text = sqlite3_column_text(stmt.get(), 4);
		const unsigned char *selection_en_text = sqlite3_column_text(stmt.get(), 5);
		seed->example_en = example_en_text != nullptr ? reinterpret_cast<const char *>(example_en_text) : "";
		seed->example_zh = example_zh_text != nullptr ? reinterpret_cast<const char *>(example_zh_text) : "";
		seed->selection_zh = selection_zh_text != nullptr ? reinterpret_cast<const char *>(selection_zh_text) : "";
		seed->selection_en = selection_en_text != nullptr ? reinterpret_cast<const char *>(selection_en_text) : "";
	}
}

bool PrepareSeedQueryContext(sqlite3 *db, SeedQueryContext *context) {
	if (db == nullptr || context == nullptr) {
		return false;
	}
	if (!PrepareStatement(db,
			"SELECT id, COALESCE(meaning_zh, ''), COALESCE(meaning_en, ''), COALESCE(stage, 1) FROM word_meaning WHERE word_id = ? ORDER BY id LIMIT 1;",
			&context->meaning_by_word_id_stmt)) {
		ESP_LOGW(kTag, "prepare first meaning lookup failed msg=%s", sqlite3_errmsg(db));
		return false;
	}
	if (!PrepareStatement(db,
			"SELECT id, COALESCE(example_en, ''), COALESCE(example_zh, ''), COALESCE(selection_zh, ''), COALESCE(selection_en, '') FROM word_example WHERE meaning_id = ? ORDER BY id LIMIT 1;",
			&context->example_by_meaning_id_stmt)) {
		ESP_LOGW(kTag, "prepare first example lookup failed msg=%s", sqlite3_errmsg(db));
		return false;
	}
	return true;
}

void ClearVocabularyExample(VocabularySeed *seed) {
	if (seed == nullptr) {
		return;
	}
	seed->example_id = 0;
		seed->example_en.clear();
	seed->example_zh.clear();
	seed->selection_zh.clear();
	seed->selection_en.clear();
}

bool OpenSeedDictionaryDb(int stage_index, sqlite3 **out_db, std::string *opened_db_path) {
	if (out_db == nullptr || opened_db_path == nullptr) {
		return false;
	}
	*out_db = nullptr;
	opened_db_path->clear();
	const std::string db_path = DiscoverCachedDictionaryDbPath(stage_index);
	if (db_path.empty()) {
		ESP_LOGW(kTag, "dictionary db path not found while loading vocabulary seeds");
		return false;
	}
	if (!eteacher::database_manager::OpenReadonlyDbFile(db_path, out_db, opened_db_path, kTag, "word_practice_dictionary") ||
		*out_db == nullptr) {
		ESP_LOGW(kTag, "open dictionary db failed via sqlite_db_api path=%s", db_path.c_str());
		return false;
	}
	LogResolvedDbAccess("word_practice_dictionary", *opened_db_path, "sqlite_db_api::OpenReadonlyDbFile");
	return true;
}

bool PopulateMeaningForWord(sqlite3_stmt *stmt, int word_id, VocabularySeed *seed) {
	if (stmt == nullptr || seed == nullptr || word_id <= 0) {
		return false;
	}
	ResetStatement(stmt);
	if (sqlite3_bind_int(stmt, 1, word_id) != SQLITE_OK) {
		return false;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW) {
		return false;
	}
	seed->meaning_id = sqlite3_column_int(stmt, 0);
	const unsigned char *meaning_zh_text = sqlite3_column_text(stmt, 1);
	const unsigned char *meaning_en_text = sqlite3_column_text(stmt, 2);
	seed->meaning_zh = meaning_zh_text != nullptr ? reinterpret_cast<const char *>(meaning_zh_text) : "";
	seed->meaning_en = meaning_en_text != nullptr ? reinterpret_cast<const char *>(meaning_en_text) : "";
	seed->stage = sqlite3_column_int(stmt, 3);
	return seed->meaning_id > 0;
}

bool PopulateExampleForMeaning(sqlite3_stmt *stmt, int meaning_id, VocabularySeed *seed) {
	if (stmt == nullptr || seed == nullptr || meaning_id <= 0) {
		return false;
	}
	ResetStatement(stmt);
	if (sqlite3_bind_int(stmt, 1, meaning_id) != SQLITE_OK) {
		return false;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW) {
		return false;
	}
	seed->example_id = sqlite3_column_int(stmt, 0);
	const unsigned char *example_en_text = sqlite3_column_text(stmt, 1);
	const unsigned char *example_zh_text = sqlite3_column_text(stmt, 2);
	const unsigned char *selection_zh_text = sqlite3_column_text(stmt, 3);
	const unsigned char *selection_en_text = sqlite3_column_text(stmt, 4);
	seed->example_en = example_en_text != nullptr ? reinterpret_cast<const char *>(example_en_text) : "";
	seed->example_zh = example_zh_text != nullptr ? reinterpret_cast<const char *>(example_zh_text) : "";
	seed->selection_zh = selection_zh_text != nullptr ? reinterpret_cast<const char *>(selection_zh_text) : "";
	seed->selection_en = selection_en_text != nullptr ? reinterpret_cast<const char *>(selection_en_text) : "";
	return seed->example_id > 0;
}

std::string BestMeaningText(const VocabularySeed &seed) {
	const std::string meaning_zh = Trim(seed.meaning_zh);
	if (!meaning_zh.empty()) {
		return meaning_zh;
	}
	return Trim(seed.meaning_en);
}

OptionSeed ToOptionSeed(const VocabularySeed &seed) {
	return OptionSeed{seed.word_id, seed.word, seed.meaning_zh, seed.meaning_en, seed.image, ""};
}

constexpr std::array<const char *, 4> kQuestionOptionTokens = {"A", "B", "C", "D"};

std::vector<std::string> BuildHintTokens(const std::string &text);
std::string BuildChoiceQuestionJson(const VocabularySeed &seed,
					const std::vector<OptionSeed> &options,
					const std::string &textbook_name,
					const std::string &prompt_text,
					const std::string &audio_path);
void ShuffleOptionVector(std::vector<OptionSeed> *options);
std::string BuildPairQuestionJson(const std::array<OptionSeed, 4> &left_options,
				  const std::array<OptionSeed, 4> &right_options,
				  const std::string &textbook_name);
std::string BuildSentenceQuestionJson(const VocabularySeed &seed,
				      const std::string &prompt_text,
				      const std::vector<std::string> &hints,
				      const std::string &textbook_name,
				      const std::string &audio_path);

int ComposeQuestionId(int word_id, int question_type) {
	return word_id > 0 ? ((word_id << 8) | (question_type & 0xFF)) : (question_type & 0xFF);
}

void ShuffleOptionArray(std::array<OptionSeed, 4> *options) {
	if (options == nullptr) {
		return;
	}
	for (size_t index = options->size(); index > 1; --index) {
		const size_t swap_index = static_cast<size_t>(esp_random() % index);
		std::swap((*options)[index - 1], (*options)[swap_index]);
	}
}

bool AppendChoiceQuestion(std::vector<word_practice::QuestionData> *question_pool,
					  int *question_built_count,
					  const VocabularySeed &seed,
					  const std::string &textbook_name,
					  int question_type,
					  const std::vector<OptionSeed> &candidate_options,
					  const std::string &prompt_text,
					  const std::string &audio_path,
					  size_t option_count = 4) {
	if (question_pool == nullptr ||
		question_built_count == nullptr ||
		option_count < 2 ||
		option_count > kQuestionOptionTokens.size() ||
		candidate_options.size() < option_count) {
		return false;
	}

	std::vector<OptionSeed> options(candidate_options.begin(), candidate_options.begin() + static_cast<std::ptrdiff_t>(option_count));
	ShuffleOptionVector(&options);

	size_t answer_index = 0;
	for (size_t index = 0; index < options.size(); ++index) {
		if (options[index].word_id == seed.word_id) {
			answer_index = index;
			break;
		}
	}

	word_practice::QuestionData question;
	question.id = ComposeQuestionId(seed.word_id, question_type);
	question.type = question_type;
	question.stage = textbook_name;
	question.difficulty = seed.is_review ? 1 : 2;
	question.answer = kQuestionOptionTokens[answer_index];
	question.content_json = BuildChoiceQuestionJson(seed, options, textbook_name, prompt_text, audio_path);
	if (question.content_json.empty()) {
		return false;
	}

	question_pool->push_back(std::move(question));
	++(*question_built_count);
	return true;
}

bool AppendPairQuestion(std::vector<word_practice::QuestionData> *question_pool,
				int *question_built_count,
				const VocabularySeed &seed,
				const std::string &textbook_name,
				const std::vector<OptionSeed> &standard_options) {
	if (question_pool == nullptr || question_built_count == nullptr || standard_options.size() < 4) {
		return false;
	}

	std::array<OptionSeed, 4> left_options = {
		standard_options[0], standard_options[1], standard_options[2], standard_options[3]};
	ShuffleOptionArray(&left_options);
	std::array<OptionSeed, 4> right_options = left_options;
	ShuffleOptionArray(&right_options);

	std::string expected_pairs;
	for (const auto &left : left_options) {
		for (const auto &right : right_options) {
			if (left.word_id != right.word_id) {
				continue;
			}
			if (!expected_pairs.empty()) {
				expected_pairs += ";";
			}
			expected_pairs += Trim(left.word) + "-" + Trim(right.meaning_zh);
			break;
		}
	}

	word_practice::QuestionData question;
	question.id = ComposeQuestionId(seed.word_id, 4);
	question.type = 4;
	question.stage = textbook_name;
	question.difficulty = seed.is_review ? 1 : 2;
	question.answer = std::move(expected_pairs);
	question.content_json = BuildPairQuestionJson(left_options, right_options, textbook_name);
	if (question.content_json.empty()) {
		return false;
	}

	question_pool->push_back(std::move(question));
	++(*question_built_count);
	return true;
}

bool AppendSentenceQuestion(std::vector<word_practice::QuestionData> *question_pool,
				    int *question_built_count,
				    const VocabularySeed &seed,
				    const std::string &textbook_name,
				    int question_type,
				    const std::string &prompt_text,
				    const std::string &answer_text,
				    const std::vector<std::string> &hints,
				    const std::string &audio_path) {
	if (question_pool == nullptr || question_built_count == nullptr) {
		return false;
	}
	if (Trim(prompt_text).empty() || Trim(answer_text).empty()) {
		return false;
	}

	word_practice::QuestionData question;
	question.id = ComposeQuestionId(seed.word_id, question_type);
	question.type = question_type;
	question.stage = textbook_name;
	question.difficulty = seed.is_review ? 1 : 2;
	question.answer = answer_text;
	question.content_json = BuildSentenceQuestionJson(
		seed,
		prompt_text,
		hints,
		textbook_name,
		audio_path);
	if (question.content_json.empty()) {
		return false;
	}

	question_pool->push_back(std::move(question));
	++(*question_built_count);
	return true;
}

void AddNestedWordPayload(cJSON *parent, const char *key, const std::string &word) {
	if (!parent || !key || word.empty()) {
		return;
	}
	cJSON *word_obj = cJSON_CreateObject();
	if (!word_obj) {
		return;
	}
	cJSON_AddStringToObject(word_obj, "word", word.c_str());
	cJSON_AddItemToObject(parent, key, word_obj);
}

void AddNestedMeaningPayload(cJSON *parent, const char *key, const std::string &meaning_zh, const std::string &meaning_en) {
	if (!parent || !key || (meaning_zh.empty() && meaning_en.empty())) {
		return;
	}
	cJSON *meaning_obj = cJSON_CreateObject();
	if (!meaning_obj) {
		return;
	}
	if (!meaning_zh.empty()) {
		cJSON_AddStringToObject(meaning_obj, "meaning_zh", meaning_zh.c_str());
	}
	if (!meaning_en.empty()) {
		cJSON_AddStringToObject(meaning_obj, "meaning_en", meaning_en.c_str());
	}
	cJSON_AddItemToObject(parent, key, meaning_obj);
}

void AddOptionPayload(cJSON *options_obj, const char *token, const OptionSeed &option) {
	if (!options_obj || !token) {
		return;
	}
	cJSON *option_obj = cJSON_CreateObject();
	if (!option_obj) {
		return;
	}
	AddNestedWordPayload(option_obj, "word", Trim(option.word));
	AddNestedMeaningPayload(option_obj, "word_meaning", Trim(option.meaning_zh), Trim(option.meaning_en));
	if (option.word_id > 0) {
		cJSON_AddNumberToObject(option_obj, "word_id", option.word_id);
	}
	const std::string image_name = ResolveStage1ImageName(option.image, option.word_id, option.word);
	if (!image_name.empty()) {
		cJSON_AddStringToObject(option_obj, "image", image_name.c_str());
	}
	cJSON_AddItemToObject(options_obj, token, option_obj);
}

std::vector<std::string> BuildHintTokens(const std::string &text) {
	std::vector<std::string> hints = SplitHintWords(text);
	if (!hints.empty()) {
		return hints;
	}
	std::vector<std::string> words = NormalizeSentenceWordsLower(text);
	if (!words.empty()) {
		return words;
	}
	const std::string trimmed = Trim(text);
	if (!trimmed.empty()) {
		hints.push_back(trimmed);
	}
	return hints;
}

std::vector<std::string> BuildQuestionHints(const std::string &answer_text, const std::string &selection_text) {
	std::vector<std::string> answer_tokens = ExtractSentenceDisplayTokens(answer_text);
	std::vector<std::string> hints = answer_tokens;
	std::vector<std::string> selection_tokens = ExtractSentenceDisplayTokens(selection_text);
	hints.insert(hints.end(), selection_tokens.begin(), selection_tokens.end());

	if (hints.empty()) {
		return BuildHintTokens(answer_text);
	}
	if (hints.size() <= 1 || answer_tokens.size() <= 1) {
		return hints;
	}

	std::vector<std::string> shuffled = hints;
	for (int attempt = 0; attempt < 32; ++attempt) {
		ShuffleStringVector(&shuffled);
		if (!PreservesAnswerTokenOrder(shuffled, answer_tokens)) {
			return shuffled;
		}
	}

	shuffled.clear();
	for (auto it = answer_tokens.rbegin(); it != answer_tokens.rend(); ++it) {
		shuffled.push_back(*it);
	}
	shuffled.insert(shuffled.end(), selection_tokens.begin(), selection_tokens.end());
	if (!PreservesAnswerTokenOrder(shuffled, answer_tokens)) {
		return shuffled;
	}

	return hints;
}

std::vector<OptionSeed> BuildOptionCandidates(const VocabularySeed &seed, const std::vector<OptionSeed> &distractors, bool require_image) {
	std::vector<OptionSeed> options;
	options.reserve(kQuestionOptionTokens.size());
	const OptionSeed correct = ToOptionSeed(seed);
	if (!require_image || !Trim(correct.image).empty()) {
		options.push_back(correct);
	}
	for (const auto &candidate : distractors) {
		if (options.size() >= kQuestionOptionTokens.size()) {
			break;
		}
		if (candidate.word_id == seed.word_id) {
			continue;
		}
		if (Trim(candidate.word).empty()) {
			continue;
		}
		if (require_image && Trim(candidate.image).empty()) {
			continue;
		}
		options.push_back(candidate);
	}
	return options;
}

void ShuffleOptionVector(std::vector<OptionSeed> *options) {
	if (options == nullptr || options->size() < 2) {
		return;
	}
	for (size_t index = options->size(); index > 1; --index) {
		const size_t swap_index = static_cast<size_t>(esp_random() % index);
		std::swap((*options)[index - 1], (*options)[swap_index]);
	}
}

std::vector<OptionSeed> BuildDistractorOptionsFromSeeds(const VocabularySeed &seed,
										const std::vector<VocabularySeed> &loaded_seeds,
										size_t limit) {
	std::vector<OptionSeed> distractors;
	if (limit == 0) {
		return distractors;
	}

	distractors.reserve(std::min(limit, loaded_seeds.size()));
	for (const auto &candidate_seed : loaded_seeds) {
		if (candidate_seed.word_id == seed.word_id) {
			continue;
		}
		const OptionSeed candidate = ToOptionSeed(candidate_seed);
		if (Trim(candidate.word).empty()) {
			continue;
		}
		distractors.push_back(candidate);
	}

	ShuffleOptionVector(&distractors);
	if (distractors.size() > limit) {
		distractors.resize(limit);
	}
	return distractors;
}

std::vector<OptionSeed> LoadRandomImageDistractorsFromStage(const VocabularySeed &seed,
										const std::unordered_set<int> &excluded_word_ids,
										size_t limit) {
	std::vector<OptionSeed> distractors;
	if (limit == 0 || seed.stage <= 0 || Trim(seed.image).empty()) {
		return distractors;
	}
	const int64_t load_start_ms = NowMs();
	sqlite3 *db = nullptr;
	std::string opened_db_path;
	if (!OpenSeedDictionaryDb(seed.stage, &db, &opened_db_path) || db == nullptr) {
		return distractors;
	}
	{
		StatementPtr stmt;
		const char *sql =
			"SELECT id, COALESCE(word, ''), COALESCE(image, '') "
			"FROM word "
			"WHERE id <> ? "
			"AND word IS NOT NULL AND TRIM(word) <> '' "
			"AND image IS NOT NULL AND TRIM(image) <> '' "
			"ORDER BY RANDOM() LIMIT ?;";
		if (!PrepareStatement(db, sql, &stmt)) {
			CloseSeedDb(db, "LoadRandomImageDistractorsFromStage.prepare_failed");
			return distractors;
		}
		const int sample_limit = static_cast<int>(std::max<size_t>(limit * 6, 24));
		if (sqlite3_bind_int(stmt.get(), 1, seed.word_id) != SQLITE_OK ||
			sqlite3_bind_int(stmt.get(), 2, sample_limit) != SQLITE_OK) {
			CloseSeedDb(db, "LoadRandomImageDistractorsFromStage.bind_failed");
			return distractors;
		}
		const std::string excluded_image = Trim(seed.image);
		while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
			OptionSeed candidate;
			candidate.word_id = sqlite3_column_int(stmt.get(), 0);
			const unsigned char *word_text = sqlite3_column_text(stmt.get(), 1);
			const unsigned char *image_text = sqlite3_column_text(stmt.get(), 2);
			candidate.word = word_text != nullptr ? reinterpret_cast<const char *>(word_text) : "";
			candidate.image = image_text != nullptr ? reinterpret_cast<const char *>(image_text) : "";
			const std::string candidate_image = Trim(candidate.image);
			if (candidate.word_id <= 0 || Trim(candidate.word).empty() || candidate_image.empty()) {
				continue;
			}
			if (excluded_word_ids.find(candidate.word_id) != excluded_word_ids.end()) {
				continue;
			}
			if (!excluded_image.empty() && candidate_image == excluded_image) {
				continue;
			}
			distractors.push_back(std::move(candidate));
			if (distractors.size() >= limit) {
				break;
			}
		}
	}
	CloseSeedDb(db, "LoadRandomImageDistractorsFromStage");
	WP_SEED_LOGW(kTag,
		"image distractor fallback word_id=%d loaded=%d excluded=%d limit=%d total_ms=%d",
		seed.word_id,
		static_cast<int>(distractors.size()),
		static_cast<int>(excluded_word_ids.size()),
		static_cast<int>(limit),
		static_cast<int>(NowMs() - load_start_ms));
	return distractors;
}

std::vector<OptionSeed> BuildImageOptionCandidates(const VocabularySeed &seed,
									   const std::vector<VocabularySeed> &loaded_seeds) {
	std::vector<OptionSeed> image_distractors;
	image_distractors.reserve(kQuestionOptionTokens.size());
	std::unordered_set<int> used_word_ids;
	used_word_ids.insert(seed.word_id);
	std::unordered_set<std::string> used_images;
	const std::string seed_image = Trim(seed.image);
	if (!seed_image.empty()) {
		used_images.insert(seed_image);
	}
	const std::vector<OptionSeed> distractors = BuildDistractorOptionsFromSeeds(seed, loaded_seeds, 12);
	for (const auto &candidate : distractors) {
		const std::string candidate_image = Trim(candidate.image);
		if (candidate.word_id <= 0 || Trim(candidate.word).empty() || candidate_image.empty()) {
			continue;
		}
		if (!used_word_ids.insert(candidate.word_id).second) {
			continue;
		}
		if (!used_images.insert(candidate_image).second) {
			continue;
		}
		image_distractors.push_back(candidate);
		if (image_distractors.size() + 1 >= kImageChoiceOptionCount) {
			break;
		}
	}
	if (image_distractors.size() + 1 < kImageChoiceOptionCount) {
		const size_t needed = (kImageChoiceOptionCount - 1) - image_distractors.size();
		const std::vector<OptionSeed> stage_distractors = LoadRandomImageDistractorsFromStage(seed, used_word_ids, needed);
		for (const auto &candidate : stage_distractors) {
			const std::string candidate_image = Trim(candidate.image);
			if (candidate.word_id <= 0 || Trim(candidate.word).empty() || candidate_image.empty()) {
				continue;
			}
			if (!used_word_ids.insert(candidate.word_id).second) {
				continue;
			}
			if (!used_images.insert(candidate_image).second) {
				continue;
			}
			image_distractors.push_back(candidate);
			if (image_distractors.size() + 1 >= kImageChoiceOptionCount) {
				break;
			}
		}
	}
	return BuildOptionCandidates(seed, image_distractors, true);
}

std::string ResolveQuestionAudioField(const std::string &audio_path, int word_id, const std::string &word_text) {
	const std::string trimmed = Trim(audio_path);
	if (!trimmed.empty() && trimmed.front() == '/') {
		return trimmed;
	}
	return ResolveStage1AudioName(trimmed, word_id, word_text);
}

std::string BuildChoiceQuestionJson(const VocabularySeed &seed,
					const std::vector<OptionSeed> &options,
						const std::string &textbook_name,
						const std::string &prompt_text,
						const std::string &audio_path) {
	cJSON *root = cJSON_CreateObject();
	if (root == nullptr) {
		return {};
	}
	if (!prompt_text.empty()) {
		cJSON_AddStringToObject(root, "question", prompt_text.c_str());
	}
	if (!textbook_name.empty()) {
		cJSON_AddStringToObject(root, "textbook", textbook_name.c_str());
	}
	if (seed.word_id > 0) {
		cJSON_AddNumberToObject(root, "word_id", seed.word_id);
	}
	const std::string resolved_audio = ResolveQuestionAudioField(audio_path, seed.word_id, seed.word);
	if (!resolved_audio.empty()) {
		cJSON_AddStringToObject(root, "audio", resolved_audio.c_str());
	}
	AddNestedWordPayload(root, "word", Trim(seed.word));
	AddNestedMeaningPayload(root, "word_meaning", Trim(seed.meaning_zh), Trim(seed.meaning_en));

	cJSON *options_obj = cJSON_CreateObject();
	if (options_obj != nullptr) {
		for (size_t index = 0; index < options.size() && index < kQuestionOptionTokens.size(); ++index) {
			AddOptionPayload(options_obj, kQuestionOptionTokens[index], options[index]);
		}
		cJSON_AddItemToObject(root, "options", options_obj);
	}

	char *json = cJSON_PrintUnformatted(root);
	std::string content = json != nullptr ? json : "";
	if (json != nullptr) {
		cJSON_free(json);
	}
	cJSON_Delete(root);
	return content;
}

std::string BuildPairQuestionJson(const std::array<OptionSeed, 4> &left_options,
					   const std::array<OptionSeed, 4> &right_options,
					   const std::string &textbook_name) {
	cJSON *root = cJSON_CreateObject();
	if (root == nullptr) {
		return {};
	}
	cJSON_AddStringToObject(root, "question", "单词配对");
	if (!textbook_name.empty()) {
		cJSON_AddStringToObject(root, "textbook", textbook_name.c_str());
	}
	cJSON *left = cJSON_CreateArray();
	cJSON *right = cJSON_CreateArray();
	if (left != nullptr && right != nullptr) {
		for (const auto &option : left_options) {
			cJSON *left_item = cJSON_CreateObject();
			if (left_item == nullptr) {
				continue;
			}
			const std::string left_word = Trim(option.word);
			if (!left_word.empty()) {
				cJSON_AddStringToObject(left_item, "word", left_word.c_str());
			}
			const std::string resolved_audio = ResolveStage1AudioName(option.audio_path, option.word_id, option.word);
			if (!resolved_audio.empty()) {
				cJSON_AddStringToObject(left_item, "audio", resolved_audio.c_str());
			}
			cJSON_AddItemToArray(left, left_item);
		}
		for (const auto &option : right_options) {
			cJSON_AddItemToArray(right, cJSON_CreateString(Trim(option.meaning_zh).c_str()));
		}
		cJSON_AddItemToObject(root, "left", left);
		cJSON_AddItemToObject(root, "right", right);
	} else {
		if (left) cJSON_Delete(left);
		if (right) cJSON_Delete(right);
	}

	char *json = cJSON_PrintUnformatted(root);
	std::string content = json != nullptr ? json : "";
	if (json != nullptr) {
		cJSON_free(json);
	}
	cJSON_Delete(root);
	return content;
}

std::string BuildSentenceQuestionJson(const VocabularySeed &seed,
					  const std::string &prompt_text,
					  const std::vector<std::string> &hints,
					  const std::string &textbook_name,
					  const std::string &audio_path) {
	cJSON *root = cJSON_CreateObject();
	if (root == nullptr) {
		return {};
	}
	if (!prompt_text.empty()) {
		cJSON_AddStringToObject(root, "question", prompt_text.c_str());
		cJSON_AddStringToObject(root, "prompt", prompt_text.c_str());
	}
	if (!textbook_name.empty()) {
		cJSON_AddStringToObject(root, "textbook", textbook_name.c_str());
	}
	if (seed.word_id > 0) {
		cJSON_AddNumberToObject(root, "word_id", seed.word_id);
	}
	const std::string resolved_audio = ResolveQuestionAudioField(audio_path, seed.word_id, seed.word);
	if (!resolved_audio.empty()) {
		cJSON_AddStringToObject(root, "audio", resolved_audio.c_str());
	}
	AddNestedWordPayload(root, "word", Trim(seed.word));
	AddNestedMeaningPayload(root, "word_meaning", Trim(seed.meaning_zh), Trim(seed.meaning_en));
	if (!hints.empty()) {
		cJSON *hints_obj = cJSON_CreateArray();
		if (hints_obj != nullptr) {
			for (const auto &hint : hints) {
				cJSON_AddItemToArray(hints_obj, cJSON_CreateString(Trim(hint).c_str()));
			}
			cJSON_AddItemToObject(root, "hints", hints_obj);
		}
	}

	char *json = cJSON_PrintUnformatted(root);
	std::string content = json != nullptr ? json : "";
	if (json != nullptr) {
		cJSON_free(json);
	}
	cJSON_Delete(root);
	return content;
}

bool LoadVocabularySeed(sqlite3 *db,
			    const SeedQueryContext &query_context,
			    const word_practice::SelectedWord &selected_word,
			    VocabularySeed *seed) {
	if (db == nullptr || seed == nullptr || selected_word.word_id <= 0 || selected_word.word.empty()) {
		return false;
	}
	const int64_t seed_start_ms = NowMs();

	seed->word_id = selected_word.word_id;
	seed->word = Trim(selected_word.word);
	seed->image = Trim(selected_word.image);
	seed->stage = 1;
	seed->is_review = selected_word.is_review;
	ClearVocabularyExample(seed);
	if (seed->word.empty()) {
		return false;
	}
	if (selected_word.word_id <= 0) {
		return false;
	}
	const int resolved_word_id = selected_word.word_id;
	seed->word_id = resolved_word_id;
	const bool loaded = PopulateMeaningForWord(query_context.meaning_by_word_id_stmt.get(), resolved_word_id, seed);
	if (!loaded) {
		WP_SEED_LOGW(kTag,
			"seed load aborted due to missing meaning by word_id=%d word=%s",
			selected_word.word_id,
			seed->word.c_str());
	}
	const bool has_example = loaded && PopulateExampleForMeaning(query_context.example_by_meaning_id_stmt.get(), seed->meaning_id, seed);
	const bool loaded_with_example = loaded && has_example;
	const int64_t seed_total_ms = NowMs() - seed_start_ms;
	if (seed_total_ms >= 160 || !loaded || !loaded_with_example || !has_example) {
		WP_SEED_LOGW(kTag,
			"seed load timing word_id=%d word=%s total_ms=%d meaning_id=%d example_id=%d has_example=%d loaded_by_id=%d loaded=%d preferred_example=%d",
			seed->word_id,
			seed->word.c_str(),
			static_cast<int>(seed_total_ms),
			seed->meaning_id,
			seed->example_id,
			has_example ? 1 : 0,
			loaded ? 1 : 0,
			loaded ? 1 : 0,
			loaded_with_example ? 1 : 0);
	}

	return loaded;
}

const word_practice::WordMasteryProfile *FindQuestionBuildProfile(
	const std::vector<word_practice::WordMasteryProfile> &profiles,
	int word_id) {
	for (const auto &profile : profiles) {
		if (profile.word_id == word_id) {
			return &profile;
		}
	}
	return nullptr;
}

struct QuestionBuildPolicy {
	bool recognition = true;
	bool recall = true;
	bool output = false;
	bool speak = false;
	bool advanced_speak = false;
};

QuestionBuildPolicy BuildQuestionBuildPolicy(const VocabularySeed &seed,
						   const word_practice::WordMasteryProfile *profile,
						   bool include_speak_questions,
					   word_practice::LearningMode learning_mode) {
	QuestionBuildPolicy policy;
	if (!seed.is_review) {
		policy.recognition = true;
		policy.recall = true;
		policy.output = false;
		policy.speak = false;
		policy.advanced_speak = false;
		return policy;
	}

	const int recall_score = profile != nullptr ? profile->recall_score : 0;
	const int output_score = profile != nullptr ? profile->output_score : 0;
	policy.recognition = learning_mode == word_practice::LearningMode::ColdStart && seed.is_review;
	policy.recall = profile == nullptr || recall_score < kRecallToOutputThreshold;
	policy.output = profile != nullptr && recall_score >= kRecallToOutputThreshold;
	policy.speak = include_speak_questions && policy.output;
	policy.advanced_speak = policy.speak && output_score >= kOutputToAdvancedSpeakThreshold;

	if (!policy.recognition && !policy.recall && !policy.output) {
		policy.recall = true;
	}
	return policy;
}


const VocabularySeed *FindVocabularySeed(const std::vector<VocabularySeed> &loaded_seeds, int word_id) {
	for (const auto &seed : loaded_seeds) {
		if (seed.word_id == word_id) {
			return &seed;
		}
	}
	return nullptr;
}


std::vector<int> BuildAvailableQuestionTypesForSeed(const VocabularySeed &seed,
						    const std::vector<VocabularySeed> &loaded_seeds,
						    const word_practice::WordMasteryProfile *profile,
						    bool include_speak_questions,
					    word_practice::LearningMode learning_mode) {
	const QuestionBuildPolicy policy = BuildQuestionBuildPolicy(seed, profile, include_speak_questions, learning_mode);
	const std::vector<OptionSeed> distractors = BuildDistractorOptionsFromSeeds(seed, loaded_seeds, 12);
	const std::vector<OptionSeed> standard_options = BuildOptionCandidates(seed, distractors, false);
	const std::vector<OptionSeed> image_options = BuildImageOptionCandidates(seed, loaded_seeds);
	const bool has_image = !Trim(seed.image).empty();
	const bool has_example_sentence = !Trim(seed.example_en).empty() && !Trim(seed.example_zh).empty();

	std::vector<int> available_types;
	available_types.reserve(12);
	if (policy.recognition && has_image && image_options.size() >= kImageChoiceOptionCount) {
		available_types.push_back(kQuestionTypeImageChoice);
	}
	if (policy.recognition && standard_options.size() >= kStandardChoiceOptionMinCount) {
		available_types.push_back(kQuestionTypeMeaningChoice);
		available_types.push_back(kQuestionTypeAudioWordChoice);
		available_types.push_back(kQuestionTypeAudioMeaningChoice);
	}
	if (policy.recall && standard_options.size() >= kStandardChoiceOptionMinCount) {
		available_types.push_back(kQuestionTypeWordToMeaning);
	}
	if (policy.recall && standard_options.size() >= 4) {
		available_types.push_back(kQuestionTypePairMatch);
	}
	if (policy.output && has_example_sentence) {
		available_types.push_back(kQuestionTypeSentenceFillZh);
		available_types.push_back(kQuestionTypeSentenceBuildEn);
	}
	if (policy.speak && !Trim(seed.word).empty()) {
		available_types.push_back(kQuestionTypeSpeakWord);
	}
	if (include_speak_questions && policy.recall && !Trim(seed.word).empty()) {
		available_types.push_back(kQuestionTypeSpeakMeaning);
	}
	if (policy.advanced_speak && has_example_sentence) {
		available_types.push_back(kQuestionTypeSpeakSentence);
		available_types.push_back(kQuestionTypeSpeakTranslate);
	}
	return available_types;
}


bool AppendGeneratedQuestionByType(std::vector<word_practice::QuestionData> *question_pool,
					   const VocabularySeed &seed,
					   const std::vector<VocabularySeed> &loaded_seeds,
					   const word_practice::WordMasteryProfile *profile,
					   int question_type,
					   bool include_speak_questions,
					   word_practice::LearningMode learning_mode) {
	if (question_pool == nullptr) {
		return false;
	}
	const QuestionBuildPolicy policy = BuildQuestionBuildPolicy(seed, profile, include_speak_questions, learning_mode);
	const std::vector<OptionSeed> distractors = BuildDistractorOptionsFromSeeds(seed, loaded_seeds, 12);
	const std::vector<OptionSeed> standard_options = BuildOptionCandidates(seed, distractors, false);
	const std::vector<OptionSeed> image_options = BuildImageOptionCandidates(seed, loaded_seeds);
	const size_t standard_choice_option_count = std::min(standard_options.size(), kQuestionOptionTokens.size());
	const std::string word_audio = ResolveStage1AudioName("", seed.word_id, seed.word);
	const std::string example_audio = ResolveStage1ExampleAudioName(seed.word_id, seed.word, seed.example_id);
	const std::string example_en = Trim(seed.example_en);
	const std::string example_zh = Trim(seed.example_zh);
	const bool has_example_sentence = !example_en.empty() && !example_zh.empty();
	const std::string stage_tag = StageNumberToTag(seed.stage);
	const std::string textbook_name = stage_tag.empty() ? "vocab" : stage_tag;
	int question_built_count = 0;

	switch (question_type) {
		case 1:
			return policy.recognition && !Trim(seed.image).empty() && image_options.size() >= kImageChoiceOptionCount &&
				AppendChoiceQuestion(question_pool, &question_built_count, seed, textbook_name, 1, image_options, Trim(seed.word), "", kImageChoiceOptionCount);
		case 2:
			return policy.recognition && standard_choice_option_count >= kStandardChoiceOptionMinCount &&
				AppendChoiceQuestion(question_pool, &question_built_count, seed, textbook_name, 2, standard_options, BestMeaningText(seed), "", standard_choice_option_count);
		case 3:
			return policy.recall && standard_choice_option_count >= kStandardChoiceOptionMinCount &&
				AppendChoiceQuestion(question_pool, &question_built_count, seed, textbook_name, 3, standard_options, Trim(seed.word), "", standard_choice_option_count);
		case 4:
			return policy.recall && AppendPairQuestion(question_pool, &question_built_count, seed, textbook_name, standard_options);
		case 5:
			return policy.output && has_example_sentence &&
				AppendSentenceQuestion(question_pool, &question_built_count, seed, textbook_name, 5, example_zh, example_en,
					BuildQuestionHints(example_en, seed.selection_en), BuildExampleQuestionAudioPath(example_audio));
		case 6:
			return policy.output && has_example_sentence &&
				AppendSentenceQuestion(question_pool, &question_built_count, seed, textbook_name, 6, example_en, example_en,
					BuildQuestionHints(example_en, seed.selection_en), BuildExampleQuestionAudioPath(example_audio));
		case 7:
			return policy.speak &&
				AppendSentenceQuestion(question_pool, &question_built_count, seed, textbook_name, 7, Trim(seed.word), Trim(seed.word),
					BuildHintTokens(Trim(seed.word)), BuildQuestionAudioPath(word_audio));
		case 8:
			return include_speak_questions && policy.recall &&
				AppendSentenceQuestion(question_pool, &question_built_count, seed, textbook_name, 8, BestMeaningText(seed), Trim(seed.word),
					BuildHintTokens(Trim(seed.word)), BuildQuestionAudioPath(word_audio));
		case 9:
			return policy.advanced_speak && has_example_sentence &&
				AppendSentenceQuestion(question_pool, &question_built_count, seed, textbook_name, 9, example_en, example_en,
					BuildQuestionHints(example_en, seed.selection_en), BuildExampleQuestionAudioPath(example_audio));
		case 10:
			return policy.advanced_speak && has_example_sentence &&
				AppendSentenceQuestion(question_pool, &question_built_count, seed, textbook_name, 10, example_zh, example_en,
					BuildQuestionHints(example_en, seed.selection_en), BuildExampleQuestionAudioPath(example_audio));
		case 11:
			return policy.recognition && standard_choice_option_count >= kStandardChoiceOptionMinCount &&
				AppendChoiceQuestion(question_pool, &question_built_count, seed, textbook_name, 11, standard_options,
					word_audio.empty() ? Trim(seed.word) : "按Start播放音频", word_audio, standard_choice_option_count);
		case 12:
			return policy.recognition && standard_choice_option_count >= kStandardChoiceOptionMinCount &&
				AppendChoiceQuestion(question_pool, &question_built_count, seed, textbook_name, 12, standard_options,
					word_audio.empty() ? BestMeaningText(seed) : "按Start播放音频", word_audio, standard_choice_option_count);
		default:
			return false;
	}
}


}  // namespace

word_practice::LearningMode word_practice::QuestionSeedModule::DetermineLearningMode(
		const std::vector<word_practice::SelectedWord> &selected_words,
		const std::vector<word_practice::WordMasteryProfile> &profiles) const {
	if (selected_words.empty()) {
		return word_practice::LearningMode::Normal;
	}
	int real_new_word_count = 0;
	int profile_count = 0;
	int weak_or_due_count = 0;
	const int64_t now_sec = word_practice::CurrentPersistentEpochSeconds();
	for (const auto &selected_word : selected_words) {
		if (const auto *profile = FindQuestionBuildProfile(profiles, selected_word.word_id)) {
			++profile_count;
			if (profile->last_practiced_at <= 0) {
				++real_new_word_count;
			}
			if (profile->persistent_boost > 0 || profile->lapse_count >= 3 ||
				(profile->next_review_at > 0 && profile->next_review_at <= now_sec)) {
				++weak_or_due_count;
			}
		} else {
			++real_new_word_count;
		}
	}
	const float selected_count = static_cast<float>(std::max(1, static_cast<int>(selected_words.size())));
	if (profile_count == 0 || (static_cast<float>(real_new_word_count) / selected_count) > kColdStartNewWordRatioThreshold) {
		return word_practice::LearningMode::ColdStart;
	}
	if ((static_cast<float>(weak_or_due_count) / selected_count) > kIntensiveReviewWeakOrDueRatioThreshold) {
		return word_practice::LearningMode::IntensiveReview;
	}
	return word_practice::LearningMode::Normal;
}

word_practice::QuestionSeedModule::~QuestionSeedModule() {
	CloseCachedDictionaryDb();
}

bool word_practice::QuestionSeedModule::EnsureCachedDictionaryDb(int stage_index, sqlite3 **out_db) const {
	if (out_db == nullptr) {
		return false;
	}
	*out_db = nullptr;
	if (cached_dictionary_db_ != nullptr && cached_dictionary_stage_index_ == stage_index) {
		*out_db = cached_dictionary_db_;
		return true;
	}
	CloseCachedDictionaryDb();
	std::string opened_db_path;
	if (!OpenSeedDictionaryDb(stage_index, &cached_dictionary_db_, &opened_db_path) || cached_dictionary_db_ == nullptr) {
		cached_dictionary_stage_index_ = 0;
		cached_dictionary_db_path_.clear();
		return false;
	}
	cached_dictionary_stage_index_ = stage_index;
	cached_dictionary_db_path_ = opened_db_path;
	*out_db = cached_dictionary_db_;
	return true;
}

void word_practice::QuestionSeedModule::CloseCachedDictionaryDb() const {
	if (cached_dictionary_db_ != nullptr) {
		CloseSeedDb(cached_dictionary_db_, "QuestionSeedModule.cached_dictionary_db");
		cached_dictionary_db_ = nullptr;
	}
	cached_dictionary_stage_index_ = 0;
	cached_dictionary_db_path_.clear();
}

bool word_practice::QuestionSeedModule::LoadVocabularySeedForWord(
		const word_practice::SelectedWord &selected_word,
		int stage_index,
		word_practice::VocabularySeed *out_seed) const {
	if (out_seed == nullptr) {
		return false;
	}
	const int64_t load_start_ms = NowMs();
	sqlite3 *db = nullptr;
	if (!EnsureCachedDictionaryDb(stage_index, &db) || db == nullptr) {
		return false;
	}
	bool loaded = false;
	{
		SeedQueryContext query_context;
		if (!PrepareSeedQueryContext(db, &query_context)) {
			ESP_LOGW(kTag, "prepare single seed query context failed path=%s", cached_dictionary_db_path_.c_str());
			return false;
		}
		loaded = LoadVocabularySeed(db, query_context, selected_word, out_seed);
	}
	WP_SEED_LOGW(kTag,
		"single seed load word_id=%d word=%s ok=%d total_ms=%d",
		selected_word.word_id,
		selected_word.word.c_str(),
		loaded ? 1 : 0,
		static_cast<int>(NowMs() - load_start_ms));
	return loaded;
}

std::vector<word_practice::VocabularySeed> word_practice::QuestionSeedModule::LoadVocabularySeedsForWords(
		const std::vector<word_practice::SelectedWord> &selected_words,
		int stage_index) const {
	std::vector<word_practice::VocabularySeed> loaded_seeds;
	if (selected_words.empty()) {
		return loaded_seeds;
	}
	const int64_t load_start_ms = NowMs();
	sqlite3 *db = nullptr;
	if (!EnsureCachedDictionaryDb(stage_index, &db) || db == nullptr) {
		return loaded_seeds;
	}
	std::unordered_map<int, VocabularySeed> seed_by_word_id;
	seed_by_word_id.reserve(selected_words.size());
	std::vector<int> word_ids;
	word_ids.reserve(selected_words.size());
	for (const auto &selected_word : selected_words) {
		if (selected_word.word_id <= 0 || Trim(selected_word.word).empty()) {
			continue;
		}
		VocabularySeed seed;
		seed.word_id = selected_word.word_id;
		seed.word = Trim(selected_word.word);
		seed.image = Trim(selected_word.image);
		seed.stage = 1;
		seed.is_review = selected_word.is_review;
		ClearVocabularyExample(&seed);
		seed_by_word_id[selected_word.word_id] = std::move(seed);
		word_ids.push_back(selected_word.word_id);
	}
	std::vector<int> meaning_ids;
	meaning_ids.reserve(word_ids.size());
	LoadFirstMeaningsForSeeds(db, word_ids, &seed_by_word_id, &meaning_ids);
	LoadFirstExamplesForSeeds(db, meaning_ids, &seed_by_word_id);
	loaded_seeds.reserve(seed_by_word_id.size());
	for (const auto &selected_word : selected_words) {
		auto it = seed_by_word_id.find(selected_word.word_id);
		if (it == seed_by_word_id.end()) {
			continue;
		}
		if (it->second.meaning_id <= 0) {
			WP_SEED_LOGW(kTag,
				"seed load aborted due to missing meaning by word_id=%d word=%s",
				selected_word.word_id,
				it->second.word.c_str());
			continue;
		}
		loaded_seeds.push_back(std::move(it->second));
	}
	WP_SEED_LOGW(kTag,
		"batch seed load count=%d loaded=%d total_ms=%d",
		static_cast<int>(selected_words.size()),
		static_cast<int>(loaded_seeds.size()),
		static_cast<int>(NowMs() - load_start_ms));
	return loaded_seeds;
}

std::unordered_map<int, std::vector<int>> word_practice::QuestionSeedModule::BuildAvailableQuestionTypesByWord(
		const std::vector<word_practice::VocabularySeed> &loaded_seeds,
		const std::vector<word_practice::WordMasteryProfile> &profiles,
		bool include_speak_questions,
		word_practice::LearningMode learning_mode) const {
	const int64_t build_start_ms = NowMs();
	std::unordered_map<int, std::vector<int>> available_question_types_by_word;
	for (const auto &seed : loaded_seeds) {
		const word_practice::WordMasteryProfile *profile = FindQuestionBuildProfile(profiles, seed.word_id);
		auto available_types = BuildAvailableQuestionTypesForSeed(
			seed,
			loaded_seeds,
			profile,
			include_speak_questions,
			learning_mode);
		if (!available_types.empty()) {
			available_question_types_by_word.emplace(seed.word_id, std::move(available_types));
		}
	}
	WP_SEED_LOGW(kTag,
		"build available question types seeds=%d profiles=%d available_words=%d total_ms=%d",
		static_cast<int>(loaded_seeds.size()),
		static_cast<int>(profiles.size()),
		static_cast<int>(available_question_types_by_word.size()),
		static_cast<int>(NowMs() - build_start_ms));
	return available_question_types_by_word;
}

bool word_practice::QuestionSeedModule::GenerateQuestionOnDemand(
		const std::vector<word_practice::VocabularySeed> &loaded_seeds,
		const std::vector<word_practice::WordMasteryProfile> &profiles,
		int word_id,
		int question_type,
		bool include_speak_questions,
		word_practice::LearningMode learning_mode,
		word_practice::QuestionData *out_question) const {
	if (out_question == nullptr) {
		return false;
	}
	const int64_t generate_start_ms = NowMs();
	const VocabularySeed *seed = FindVocabularySeed(loaded_seeds, word_id);
	if (seed == nullptr) {
		return false;
	}
	const word_practice::WordMasteryProfile *profile = FindQuestionBuildProfile(profiles, word_id);
	std::vector<word_practice::QuestionData> generated_questions;
	generated_questions.reserve(1);
	if (!AppendGeneratedQuestionByType(
			&generated_questions,
			*seed,
			loaded_seeds,
			profile,
			question_type,
			include_speak_questions,
			learning_mode) || generated_questions.empty()) {
		if ((NowMs() - generate_start_ms) >= 20) {
			WP_SEED_LOGW(kTag,
				"generate question on demand word_id=%d type=%d ok=0 total_ms=%d seeds=%d profiles=%d built=%d",
				word_id,
				question_type,
				static_cast<int>(NowMs() - generate_start_ms),
				static_cast<int>(loaded_seeds.size()),
				static_cast<int>(profiles.size()),
				static_cast<int>(generated_questions.size()));
		}
		return false;
	}
	*out_question = std::move(generated_questions.front());
	WP_SEED_LOGW(kTag,
		"generate question on demand word_id=%d type=%d ok=1 total_ms=%d seeds=%d profiles=%d built=%d",
		word_id,
		question_type,
		static_cast<int>(NowMs() - generate_start_ms),
		static_cast<int>(loaded_seeds.size()),
		static_cast<int>(profiles.size()),
		static_cast<int>(generated_questions.size()));
	return true;
}
