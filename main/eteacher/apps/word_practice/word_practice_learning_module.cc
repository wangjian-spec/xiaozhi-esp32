#include "eteacher/apps/word_practice/word_practice_learning_module.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <esp_timer.h>

#include "eteacher/apps/word_practice/word_practice_config.h"
#include "eteacher/apps/word_practice/word_practice_db_utils.h"
#include "eteacher/apps/word_practice/word_practice_review_model.h"
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

#define WP_LEARNING_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define WP_LEARNING_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)

namespace word_practice {
namespace {

using namespace word_practice::config;

constexpr const char *kLearningLogTag = "WordPracticeLearning";

constexpr int kRecentWordWindowSize = 5;
constexpr int kRecentWordMaxRounds = 2;

int64_t NowSec() {
	return CurrentPersistentEpochSeconds();
}

int DayIndexFromSec(int64_t value) {
	if (value <= 0) {
		return -1;
	}
	return static_cast<int>(value / 86400);
}

int ClampScore(int value) {
	return value < 0 ? 0 : (value > config::kScoreMax ? config::kScoreMax : value);
}

int ClampEvidence(int value) {
	return value < 0 ? 0 : (value > config::kEvidenceMax ? config::kEvidenceMax : value);
}

template <typename T>
T MaxValue(T lhs, T rhs) {
	return lhs > rhs ? lhs : rhs;
}

template <typename T>
T MinValue(T lhs, T rhs) {
	return lhs < rhs ? lhs : rhs;
}

double SkillWeight(TrainingSkill skill) {
	switch (skill) {
		case TrainingSkill::Recognition:
			return config::kRecognitionSkillWeight;
		case TrainingSkill::Recall:
			return config::kRecallSkillWeight;
		case TrainingSkill::Output:
		case TrainingSkill::AdvancedSpeak:
			return skill == TrainingSkill::Output ? config::kOutputSkillWeight : config::kAdvancedSpeakSkillWeight;
		default:
			return config::kRecognitionSkillWeight;
	}
}

double ConfidenceFactor(int response_time_ms) {
	if (response_time_ms <= 0) {
		return config::kMediumResponseConfidence;
	}
	if (response_time_ms <= config::kFastResponseThresholdMs) {
		return config::kFastResponseConfidence;
	}
	if (response_time_ms <= config::kMediumResponseThresholdMs) {
		return config::kMediumResponseConfidence;
	}
	if (response_time_ms <= config::kSlowResponseThresholdMs) {
		return config::kSlowResponseConfidence;
	}
	return config::kVerySlowResponseConfidence;
}

double ErrorSeverityFactor(TrainingSkill skill, int response_time_ms) {
	double factor = 1.0;
	if (skill == TrainingSkill::Output || skill == TrainingSkill::AdvancedSpeak) {
		factor += config::kOutputErrorSeverityBonus;
	} else if (skill == TrainingSkill::Recall) {
		factor += config::kRecallErrorSeverityBonus;
	}
	if (response_time_ms > config::kSlowErrorSeverityThresholdMs) {
		factor += config::kSlowErrorSeverityBonus;
	}
	return factor;
}

CompletionRule CompletionRuleForKind(BatchWordKind kind) {
	switch (kind) {
		case BatchWordKind::NewWord:
			return CompletionRule{config::kNewWordRequiredShown,
				config::kNewWordRequiredAnyCorrect,
				config::kNewWordRequiredRecognitionCorrect,
				config::kNewWordRequiredRecallCorrect,
				config::kNewWordRequiredOutputCorrect};
		case BatchWordKind::ReviewWord:
			return CompletionRule{config::kReviewWordRequiredShown,
				config::kReviewWordRequiredAnyCorrect,
				config::kReviewWordRequiredRecognitionCorrect,
				config::kReviewWordRequiredRecallCorrect,
				config::kReviewWordRequiredOutputCorrect};
		case BatchWordKind::WeakWord:
			return CompletionRule{config::kWeakWordRequiredShown,
				config::kWeakWordRequiredAnyCorrect,
				config::kWeakWordRequiredRecognitionCorrect,
				config::kWeakWordRequiredRecallCorrect,
				config::kWeakWordRequiredOutputCorrect};
	}
	return CompletionRule{};
}

bool IsProgressComplete(const CompletionRule &rule,
			int shown_count,
			int any_correct_count,
			int recognition_count,
			int recall_count,
			int output_count) {
	return shown_count >= rule.required_shown &&
		any_correct_count >= rule.required_any_correct &&
		recognition_count >= rule.required_recognition &&
		recall_count >= rule.required_recall &&
		output_count >= rule.required_output;
}

using db::PrepareStatement;
using db::StatementPtr;

bool HasColumn(sqlite3 *db, const char *table_name, const char *column_name) {
	if (db == nullptr || table_name == nullptr || column_name == nullptr) {
		return false;
	}
	return sqlite3_table_column_metadata(db, nullptr, table_name, column_name, nullptr, nullptr, nullptr, nullptr, nullptr) == SQLITE_OK;
}

std::string NormalizeSqlDefinition(const std::string &sql) {
	std::string normalized;
	normalized.reserve(sql.size());
	for (const unsigned char ch : sql) {
		if (std::isspace(ch) != 0) {
			continue;
		}
		normalized.push_back(static_cast<char>(std::tolower(ch)));
	}
	return normalized;
}

bool HasExpectedWordLearningProfilePrimaryKeyFromSql(sqlite3 *db) {
	if (db == nullptr) {
		return false;
	}
	StatementPtr stmt;
	if (!PrepareStatement(
			db,
			"SELECT sql FROM sqlite_master WHERE type='table' AND name='word_learning_profile' LIMIT 1;",
			&stmt)) {
		return false;
	}
	if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
		return false;
	}
	const unsigned char *sql_text = sqlite3_column_text(stmt.get(), 0);
	if (sql_text == nullptr) {
		return false;
	}
	const std::string normalized_sql = NormalizeSqlDefinition(reinterpret_cast<const char *>(sql_text));
	return normalized_sql.find("primarykey(user_id,word_id,textbook_name)") != std::string::npos;
}

bool HasExpectedWordLearningProfilePrimaryKey(sqlite3 *db) {
	if (db == nullptr) {
		return false;
	}
	if (HasExpectedWordLearningProfilePrimaryKeyFromSql(db)) {
		return true;
	}
	StatementPtr stmt;
	if (!PrepareStatement(db, "PRAGMA table_info(word_learning_profile);", &stmt)) {
		WP_LEARNING_LOGW(kLearningLogTag, "prepare table_info(word_learning_profile) failed msg=%s", sqlite3_errmsg(db));
		return false;
	}
	std::vector<std::pair<int, std::string>> pk_columns;
	for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
		const int pk_order = sqlite3_column_int(stmt.get(), 5);
		if (pk_order <= 0) {
			continue;
		}
		const unsigned char *column_name = sqlite3_column_text(stmt.get(), 1);
		pk_columns.emplace_back(
			pk_order,
			column_name != nullptr ? reinterpret_cast<const char *>(column_name) : std::string());
	}
	std::sort(pk_columns.begin(), pk_columns.end(), [](const auto &lhs, const auto &rhs) {
		return lhs.first < rhs.first;
	});
	return pk_columns.size() == 3 && pk_columns[0].second == "user_id" && pk_columns[1].second == "word_id" &&
		pk_columns[2].second == "textbook_name";
}

bool ValidateWordLearningProfileSchema(sqlite3 *db) {
	if (db == nullptr) {
		return false;
	}
	constexpr std::array<const char *, 13> kRequiredColumns = {
		"textbook_name",
		"stage",
		"strength",
		"recall_score",
		"output_score",
		"next_review_at",
		"lapse_count",
		"last_practiced_at",
		"last_decay_at",
		"last_reviewed_at",
		"last_response_time_ms",
		"persistent_boost",
		"mastered",
	};
	for (const char *column_name : kRequiredColumns) {
		if (!HasColumn(db, "word_learning_profile", column_name)) {
			WP_LEARNING_LOGW(kLearningLogTag,
				"word_learning_profile schema mismatch missing column=%s; recreate user.db with DatabaseCreate.py",
				column_name);
			return false;
		}
	}
	if (!HasExpectedWordLearningProfilePrimaryKey(db)) {
		WP_LEARNING_LOGW(
			kLearningLogTag,
			"word_learning_profile schema mismatch unexpected primary key; recreate user.db with DatabaseCreate.py");
		return false;
	}
	return true;
}

int FindColumnIndex(sqlite3_stmt *stmt, const char *column_name) {
	if (stmt == nullptr || column_name == nullptr) {
		return -1;
	}
	const int column_count = sqlite3_column_count(stmt);
	for (int index = 0; index < column_count; ++index) {
		const char *resolved_name = sqlite3_column_name(stmt, index);
		if (resolved_name != nullptr && std::strcmp(resolved_name, column_name) == 0) {
			return index;
		}
	}
	return -1;
}

int GetColumnInt(sqlite3_stmt *stmt, const char *column_name, int fallback = 0) {
	const int index = FindColumnIndex(stmt, column_name);
	return index >= 0 ? sqlite3_column_int(stmt, index) : fallback;
}

int64_t GetColumnInt64(sqlite3_stmt *stmt, const char *column_name, int64_t fallback = 0) {
	const int index = FindColumnIndex(stmt, column_name);
	return index >= 0 ? sqlite3_column_int64(stmt, index) : fallback;
}

bool ExecSql(sqlite3 *db, const char *sql) {
	if (db == nullptr || sql == nullptr) {
		return false;
	}
	char *err = nullptr;
	const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
	if (rc != SQLITE_OK) {
		WP_LEARNING_LOGW(kLearningLogTag, "exec sql failed rc=%d msg=%s sql=%s", rc, err != nullptr ? err : "null", sql);
		if (err != nullptr) {
			sqlite3_free(err);
		}
		return false;
	}
	return true;
}

const char *ReasonLabel(QuestionReasonType reason_type) {
	switch (reason_type) {
		case QuestionReasonType::NewWord:
			return "new_word";
		case QuestionReasonType::ReviewDue:
			return "review_due";
		case QuestionReasonType::MistakeFollowup:
			return "mistake_followup";
		case QuestionReasonType::WeakReinforce:
			return "weak_reinforce";
		case QuestionReasonType::BatchTarget:
			return "batch_target";
		default:
			return "unknown";
	}
}

std::string BuildReasonJsonValue(QuestionReasonType reason_type,
					const WordMasteryProfile *profile,
					TrainingSkill target_skill) {
	char buffer[160] = {0};
	std::snprintf(buffer,
		      sizeof(buffer),
		      "{\"type\":\"%s\",\"stage\":%d,\"target_skill\":\"%s\"}",
		      ReasonLabel(reason_type),
		      profile ? profile->stage : 0,
		      ToString(target_skill));
	return buffer;
}

MasteredState NormalizeMasteredState(int value) {
	switch (value) {
		case static_cast<int>(MasteredState::AutoMastered):
			return MasteredState::AutoMastered;
		case static_cast<int>(MasteredState::UserMastered):
			return MasteredState::UserMastered;
		case static_cast<int>(MasteredState::Suppressed):
			return MasteredState::Suppressed;
		default:
			return MasteredState::Active;
	}
}

bool IsDisplayedAsMasteredState(MasteredState state) {
	return state == MasteredState::AutoMastered || state == MasteredState::UserMastered;
}

int DeriveUiStage(const WordMasteryProfile &profile) {
	int stage = config::kUiStageMin + (profile.strength / config::kUiStageStep);
	stage = MaxValue(config::kUiStageMin, MinValue(config::kUiStageMax, stage));
	if (IsDisplayedAsMasteredState(profile.mastered) && stage < config::kMasteredUiStageFloor) {
		stage = config::kMasteredUiStageFloor;
	}
	return stage;
}

bool IsMasteredProfileEvidence(const WordMasteryProfile &profile) {
	return profile.recall_score >= config::kMasteredMinRecallScore &&
		profile.output_score >= config::kMasteredMinOutputScore &&
		profile.strength >= config::kMasteredMinStrength &&
		profile.lapse_count <= config::kMasteredMaxLapseCount;
}

void SyncDerivedProfileState(WordMasteryProfile *profile) {
	if (profile == nullptr) {
		return;
	}
	profile->strength = ClampScore(profile->strength);
	profile->recall_score = ClampEvidence(profile->recall_score);
	profile->output_score = ClampEvidence(profile->output_score);
	profile->persistent_boost = ClampScore(profile->persistent_boost);
	profile->mastered = NormalizeMasteredState(static_cast<int>(profile->mastered));
	if (profile->mastered != MasteredState::UserMastered && profile->mastered != MasteredState::Suppressed) {
		profile->mastered = IsMasteredProfileEvidence(*profile)
			? MasteredState::AutoMastered
			: MasteredState::Active;
	}
	profile->stage = DeriveUiStage(*profile);
}

void UpdateProfileFromAttempt(WordMasteryProfile *profile, const QuestionAttemptRecord &attempt) {
	if (profile == nullptr) {
		return;
	}
	if (attempt.skipped) {
		return;
	}
	const int64_t practiced_at = attempt.practiced_at > 0 ? attempt.practiced_at : NowSec();
	const double skill_weight = SkillWeight(attempt.target_skill);
	const double confidence_factor = ConfidenceFactor(attempt.response_time_ms);
	if (attempt.correct) {
		const int strength_delta_raw = static_cast<int>(std::lround(config::kCorrectStrengthBaseDelta * skill_weight * confidence_factor));
		const int strength_delta = strength_delta_raw > config::kCorrectStrengthMinDelta ?
			strength_delta_raw : config::kCorrectStrengthMinDelta;
		profile->strength = ClampScore(profile->strength + strength_delta);
		profile->persistent_boost = std::max(0, profile->persistent_boost - config::kPersistentBoostDecayOnCorrect);
		if (attempt.target_skill == TrainingSkill::Recall) {
			profile->recall_score = ClampEvidence(profile->recall_score + 1);
		} else if (attempt.target_skill == TrainingSkill::Output ||
			   attempt.target_skill == TrainingSkill::AdvancedSpeak) {
			profile->output_score = ClampEvidence(profile->output_score + 1);
		}
	} else {
		const double severity = ErrorSeverityFactor(attempt.target_skill, attempt.response_time_ms);
		const int strength_penalty_raw = static_cast<int>(std::lround(config::kWrongStrengthBasePenalty * skill_weight * severity));
		const int strength_penalty = strength_penalty_raw > config::kWrongStrengthMinPenalty ?
			strength_penalty_raw : config::kWrongStrengthMinPenalty;
		profile->strength = ClampScore(profile->strength - strength_penalty);
		++profile->lapse_count;
		if (profile->lapse_count >= config::kPersistentBoostTriggerLapseCount) {
			profile->persistent_boost = config::kPersistentBoostTriggeredValue;
		}
		if (attempt.target_skill == TrainingSkill::Recall) {
			profile->recall_score = ClampEvidence(profile->recall_score - 1);
		} else if (attempt.target_skill == TrainingSkill::Output ||
			   attempt.target_skill == TrainingSkill::AdvancedSpeak) {
			profile->output_score = ClampEvidence(profile->output_score - 1);
		}
	}
	profile->last_practiced_at = practiced_at;
	profile->last_reviewed_at = practiced_at;
	profile->last_response_time_ms = MaxValue(0, attempt.response_time_ms);
	SyncDerivedProfileState(profile);
	profile->next_review_at = practiced_at + review_model::NextReviewIntervalSec(*profile);
}

WordMasteryProfile BuildDefaultProfile(int user_id,
					   const SelectedWord &selected,
					   const std::string &textbook_name) {
	WordMasteryProfile profile;
	profile.user_id = user_id;
	profile.word_id = selected.word_id;
	profile.textbook_name = textbook_name;
	profile.strength = selected.is_review ? config::kDefaultReviewStrength : config::kDefaultNewWordStrength;
	profile.recall_score = selected.is_review ? config::kDefaultReviewRecallScore : 0;
	profile.output_score = 0;
	SyncDerivedProfileState(&profile);
	return profile;
}

void LoadProfileColumns(sqlite3_stmt *stmt, int word_id, int user_id, const std::string &textbook_name, WordMasteryProfile *profile) {
	if (stmt == nullptr || profile == nullptr) {
		return;
	}
	profile->user_id = user_id;
	profile->word_id = word_id;
	profile->textbook_name = textbook_name;
	profile->stage = GetColumnInt(stmt, "stage");
	profile->strength = GetColumnInt(stmt, "strength");
	profile->recall_score = GetColumnInt(stmt, "recall_score");
	profile->output_score = GetColumnInt(stmt, "output_score");
	profile->next_review_at = GetColumnInt64(stmt, "next_review_at");
	profile->lapse_count = GetColumnInt(stmt, "lapse_count");
	profile->last_practiced_at = GetColumnInt64(stmt, "last_practiced_at");
	profile->last_decay_at = GetColumnInt64(stmt, "last_decay_at");
	profile->last_reviewed_at = GetColumnInt64(stmt, "last_reviewed_at", profile->last_practiced_at);
	profile->last_response_time_ms = GetColumnInt(stmt, "last_response_time_ms");
	profile->persistent_boost = GetColumnInt(
		stmt,
		"persistent_boost",
		profile->lapse_count >= config::kPersistentBoostTriggerLapseCount ? config::kPersistentBoostTriggeredValue : 0);
	profile->mastered = NormalizeMasteredState(GetColumnInt(stmt, "mastered", static_cast<int>(MasteredState::Active)));
	SyncDerivedProfileState(profile);
}

std::string BuildLoadProfilesSql(size_t word_count) {
	std::string sql =
		"SELECT word_id, stage AS stage, strength AS strength, recall_score AS recall_score, output_score AS output_score, next_review_at AS next_review_at, lapse_count AS lapse_count, "
		"last_practiced_at AS last_practiced_at, last_decay_at AS last_decay_at, COALESCE(last_reviewed_at, last_practiced_at, 0) AS last_reviewed_at, COALESCE(last_response_time_ms, 0) AS last_response_time_ms, COALESCE(persistent_boost, 0) AS persistent_boost, COALESCE(mastered, 0) AS mastered "
		"FROM word_learning_profile WHERE user_id=? AND textbook_name=? AND word_id IN (";
	for (size_t index = 0; index < word_count; ++index) {
		if (index > 0) {
			sql += ",";
		}
		sql += "?";
	}
	sql += ");";
	return sql;
}

}  // namespace

const char *ToString(TrainingSkill skill) {
	switch (skill) {
		case TrainingSkill::Recognition:
			return "recognition";
		case TrainingSkill::Recall:
			return "recall";
		case TrainingSkill::Output:
			return "output";
		case TrainingSkill::AdvancedSpeak:
			return "advanced_speak";
		default:
			return "unknown";
	}
}

const char *ToString(QuestionReasonType reason_type) {
	return ReasonLabel(reason_type);
}

WordMasteryDao::WordMasteryDao(const char *log_tag, int user_id)
	: log_tag_(log_tag),
	  user_id_(std::max(0, user_id)) {
}

void WordMasteryDao::SetUserId(int user_id) {
	user_id_ = std::max(0, user_id);
}

std::string WordMasteryDao::DiscoverUserDbPath() const {
	static std::string cached_user_db_path;
	if (!cached_user_db_path.empty()) {
		return cached_user_db_path;
	}
	cached_user_db_path = eteacher::database_manager::DiscoverUserDataDbPath(log_tag_, nullptr);
	return cached_user_db_path;
}

bool WordMasteryDao::EnsureTables(sqlite3 *db) const {
	if (db == nullptr) {
		return false;
	}
	const char *sql_profile =
		"CREATE TABLE IF NOT EXISTS word_learning_profile ("
		"user_id INTEGER NOT NULL,"
		"word_id INTEGER NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"stage INTEGER DEFAULT 0,"
		"strength INTEGER DEFAULT 0,"
		"recall_score INTEGER DEFAULT 0,"
		"output_score INTEGER DEFAULT 0,"
		"next_review_at INTEGER DEFAULT 0,"
		"lapse_count INTEGER DEFAULT 0,"
		"last_practiced_at INTEGER DEFAULT 0,"
		"last_decay_at INTEGER DEFAULT 0,"
		"last_reviewed_at INTEGER DEFAULT 0,"
		"last_response_time_ms INTEGER DEFAULT 0,"
		"persistent_boost INTEGER DEFAULT 0,"
		"mastered INTEGER DEFAULT 0,"
		"PRIMARY KEY(user_id, word_id, textbook_name)"
		");";
	const char *sql_history =
		"CREATE TABLE IF NOT EXISTS word_practice_history ("
		"id INTEGER PRIMARY KEY,"
		"user_id INTEGER NOT NULL,"
		"word_id INTEGER NOT NULL,"
		"question_type INTEGER DEFAULT 0,"
		"target_skill TEXT,"
		"review_type TEXT,"
		"rating INTEGER DEFAULT 0,"
		"response_time INTEGER DEFAULT 0,"
		"correct INTEGER DEFAULT 0,"
		"question_reason TEXT,"
		"practiced_at INTEGER DEFAULT 0"
		");";
	const char *sql_profile_index =
		"CREATE INDEX IF NOT EXISTS idx_word_learning_profile_user_next_review "
		"ON word_learning_profile(user_id, next_review_at);";
	const char *sql_history_index =
		"CREATE INDEX IF NOT EXISTS idx_word_practice_history_user_word "
		"ON word_practice_history(user_id, word_id, practiced_at);";
	char *err = nullptr;
	if (sqlite3_exec(db, sql_profile, nullptr, nullptr, &err) != SQLITE_OK) {
		WP_LEARNING_LOGW(log_tag_,
			"ensure tables create profile failed msg=%s",
			err != nullptr ? err : sqlite3_errmsg(db));
		if (err != nullptr) {
			sqlite3_free(err);
		}
		return false;
	}
	if (sqlite3_exec(db, sql_history, nullptr, nullptr, &err) != SQLITE_OK) {
		WP_LEARNING_LOGW(log_tag_,
			"ensure tables create history failed msg=%s",
			err != nullptr ? err : sqlite3_errmsg(db));
		if (err != nullptr) {
			sqlite3_free(err);
		}
		return false;
	}
	(void)sqlite3_exec(db, sql_profile_index, nullptr, nullptr, nullptr);
	(void)sqlite3_exec(db, sql_history_index, nullptr, nullptr, nullptr);
	if (!ValidateWordLearningProfileSchema(db)) {
		WP_LEARNING_LOGW(log_tag_, "validate word_learning_profile schema failed msg=%s", sqlite3_errmsg(db));
		return false;
	}
	return true;
}

std::vector<WordMasteryProfile> WordMasteryDao::LoadProfiles(const std::vector<SelectedWord> &selected_words,
						     const std::string &textbook_name) const {
	std::vector<WordMasteryProfile> profiles;
	profiles.reserve(selected_words.size());
	if (selected_words.empty()) {
		return profiles;
	}
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		for (const auto &selected : selected_words) {
			profiles.push_back(BuildDefaultProfile(user_id_, selected, textbook_name));
		}
		return profiles;
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return profiles;
	}
	EnsureTables(db);
	profiles = LoadProfiles(db, selected_words, textbook_name);
	sqlite3_close(db);
	return profiles;
}

std::vector<WordMasteryProfile> WordMasteryDao::LoadProfiles(sqlite3 *db,
					      const std::vector<SelectedWord> &selected_words,
					      const std::string &textbook_name) const {
	std::vector<WordMasteryProfile> profiles;
	profiles.reserve(selected_words.size());
	if (selected_words.empty()) {
		return profiles;
	}
	if (db == nullptr) {
		for (const auto &selected : selected_words) {
			profiles.push_back(BuildDefaultProfile(user_id_, selected, textbook_name));
		}
		return profiles;
	}
	std::vector<int> word_ids;
	word_ids.reserve(selected_words.size());
	std::unordered_set<int> seen_word_ids;
	seen_word_ids.reserve(selected_words.size());
	for (const auto &selected : selected_words) {
		if (selected.word_id > 0 && seen_word_ids.insert(selected.word_id).second) {
			word_ids.push_back(selected.word_id);
		}
	}
	std::unordered_map<int, WordMasteryProfile> loaded_profiles;
	loaded_profiles.reserve(word_ids.size());
	if (!word_ids.empty()) {
		const std::string sql = BuildLoadProfilesSql(word_ids.size());
		StatementPtr stmt;
		if (PrepareStatement(db, sql, &stmt)) {
			sqlite3_bind_int(stmt.get(), 1, user_id_);
			sqlite3_bind_text(stmt.get(), 2, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
			for (size_t index = 0; index < word_ids.size(); ++index) {
				sqlite3_bind_int(stmt.get(), static_cast<int>(index) + 3, word_ids[index]);
			}
			for (int rc = sqlite3_step(stmt.get()); rc == SQLITE_ROW; rc = sqlite3_step(stmt.get())) {
				const int word_id = sqlite3_column_int(stmt.get(), 0);
				WordMasteryProfile profile;
				LoadProfileColumns(stmt.get(), word_id, user_id_, textbook_name, &profile);
				loaded_profiles[word_id] = std::move(profile);
			}
		}
	}
	for (const auto &selected : selected_words) {
		const auto it = loaded_profiles.find(selected.word_id);
		if (it != loaded_profiles.end()) {
			profiles.push_back(it->second);
		} else {
			profiles.push_back(BuildDefaultProfile(user_id_, selected, textbook_name));
		}
	}
	return profiles;
}

WordMasteryProfile WordMasteryDao::LoadProfile(sqlite3 *db, int word_id, const std::string &textbook_name) const {
	WordMasteryProfile profile;
	profile.user_id = user_id_;
	profile.word_id = word_id;
	profile.textbook_name = textbook_name;
	if (db == nullptr || word_id <= 0) {
		return profile;
	}
	const char *sql =
		"SELECT stage AS stage, strength AS strength, recall_score AS recall_score, output_score AS output_score, next_review_at AS next_review_at, lapse_count AS lapse_count, "
		"last_practiced_at AS last_practiced_at, last_decay_at AS last_decay_at, COALESCE(last_reviewed_at, last_practiced_at, 0) AS last_reviewed_at, COALESCE(last_response_time_ms, 0) AS last_response_time_ms, COALESCE(persistent_boost, 0) AS persistent_boost, COALESCE(mastered, 0) AS mastered "
		"FROM word_learning_profile WHERE user_id=? AND word_id=? AND textbook_name=? LIMIT 1;";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return profile;
	}
	sqlite3_bind_int(stmt.get(), 1, user_id_);
	sqlite3_bind_int(stmt.get(), 2, word_id);
	sqlite3_bind_text(stmt.get(), 3, textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
		LoadProfileColumns(stmt.get(), word_id, user_id_, textbook_name, &profile);
	}
	return profile;
}

int WordMasteryDao::ApplyDueDecayIfNeeded(std::vector<WordMasteryProfile> *profiles) const {
	if (profiles == nullptr || profiles->empty()) {
		return 0;
	}
	const int64_t now = NowSec();
	bool has_pending_decay = false;
	for (const auto &profile : *profiles) {
		if (profile.word_id <= 0 || profile.next_review_at <= 0) {
			continue;
		}
		if (now <= profile.next_review_at) {
			continue;
		}
		if (DayIndexFromSec(profile.last_decay_at) == DayIndexFromSec(now)) {
			continue;
		}
		has_pending_decay = true;
		break;
	}
	if (!has_pending_decay) {
		return 0;
	}
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return 0;
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return 0;
	}
	EnsureTables(db);
	const int decayed_count = ApplyDueDecayIfNeeded(db, profiles);
	sqlite3_close(db);
	return decayed_count;
}

int WordMasteryDao::ApplyDueDecayIfNeeded(sqlite3 *db, std::vector<WordMasteryProfile> *profiles) const {
	if (db == nullptr || profiles == nullptr || profiles->empty()) {
		return 0;
	}
	if (!ExecSql(db, "BEGIN IMMEDIATE TRANSACTION;")) {
		return 0;
	}
	int decayed_count = 0;
	bool ok = true;
	for (auto &profile : *profiles) {
		if (ApplyDueDecayIfNeeded(db, &profile)) {
			++decayed_count;
		} else if (sqlite3_errcode(db) != SQLITE_OK) {
			ok = false;
			break;
		}
	}
	if (ok) {
		ok = ExecSql(db, "COMMIT;");
	}
	if (!ok) {
		(void)ExecSql(db, "ROLLBACK;");
		decayed_count = 0;
	}
	return decayed_count;
}
bool WordMasteryDao::ApplyAttempt(sqlite3 *db, WordMasteryProfile *profile, const QuestionAttemptRecord &attempt) const {
	if (db == nullptr || profile == nullptr || profile->word_id <= 0 || attempt.word_id != profile->word_id) {
		WP_LEARNING_LOGW(log_tag_, "apply attempt skipped invalid db/profile word_id=%d attempt_word_id=%d", profile ? profile->word_id : -1, attempt.word_id);
		return false;
	}
	EnsureTables(db);
	if (attempt.skipped) {
		const bool recorded = RecordAttempt(db, attempt);
		WP_LEARNING_LOGI(
			log_tag_,
			"apply attempt recorded skip word_id=%d textbook=%s qtype=%d record_history=%d",
			attempt.word_id,
			attempt.textbook_name.c_str(),
			attempt.question_type,
			recorded ? 1 : 0);
		return recorded;
	}
	if (!attempt.textbook_name.empty()) {
		profile->textbook_name = attempt.textbook_name;
	}
	const WordMasteryProfile before_profile = *profile;
	UpdateProfileFromAttempt(profile, attempt);
	const int strength_delta = profile->strength - before_profile.strength;
	const int recall_delta = profile->recall_score - before_profile.recall_score;
	const int output_delta = profile->output_score - before_profile.output_score;
	const int lapse_delta = profile->lapse_count - before_profile.lapse_count;
	const int next_interval_sec =
		(profile->next_review_at > profile->last_practiced_at)
			? static_cast<int>(profile->next_review_at - profile->last_practiced_at)
			: 0;
	const double skill_weight = SkillWeight(attempt.target_skill);
	const double response_factor = attempt.correct
		? ConfidenceFactor(attempt.response_time_ms)
		: ErrorSeverityFactor(attempt.target_skill, attempt.response_time_ms);
	WP_LEARNING_LOGI(
		log_tag_,
		"apply attempt factors word_id=%d skill=%s correct=%d response_ms=%d weight=%.2f factor=%.2f strength_delta=%d recall_delta=%d output_delta=%d lapse_delta=%d interval_sec=%d",
		attempt.word_id,
		ToString(attempt.target_skill),
		attempt.correct ? 1 : 0,
		attempt.response_time_ms,
		skill_weight,
		response_factor,
		strength_delta,
		recall_delta,
		output_delta,
		lapse_delta,
		next_interval_sec);
	const bool saved = SaveProfile(db, *profile);
	const bool recorded = RecordAttempt(db, attempt);
	WP_LEARNING_LOGI(
		log_tag_,
		"apply attempt db=%s word_id=%d textbook=%s qtype=%d correct=%d save_profile=%d record_history=%d strength=%d recall=%d output=%d mastered=%d next_review_at=%d",
		"external",
		attempt.word_id,
		profile->textbook_name.c_str(),
		attempt.question_type,
		attempt.correct ? 1 : 0,
		saved ? 1 : 0,
		recorded ? 1 : 0,
		profile->strength,
		profile->recall_score,
		profile->output_score,
		static_cast<int>(profile->mastered),
		static_cast<int>(profile->next_review_at));
	return saved && recorded;
}

bool WordMasteryDao::SaveProfile(sqlite3 *db, const WordMasteryProfile &profile) const {
	if (db == nullptr || profile.word_id <= 0 || profile.textbook_name.empty()) {
		return false;
	}
	const char *update_sql =
		"UPDATE word_learning_profile SET "
		"stage=?, strength=?, recall_score=?, output_score=?, next_review_at=?, lapse_count=?, "
		"last_practiced_at=?, last_decay_at=?, last_reviewed_at=?, last_response_time_ms=?, persistent_boost=?, mastered=? "
		"WHERE user_id=? AND word_id=? AND textbook_name=?;";
	StatementPtr stmt;
	if (!PrepareStatement(db, update_sql, &stmt)) {
		WP_LEARNING_LOGW(log_tag_, "save profile prepare update failed word_id=%d textbook=%s msg=%s", profile.word_id, profile.textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt.get(), 1, profile.stage);
	sqlite3_bind_int(stmt.get(), 2, profile.strength);
	sqlite3_bind_int(stmt.get(), 3, profile.recall_score);
	sqlite3_bind_int(stmt.get(), 4, profile.output_score);
	sqlite3_bind_int64(stmt.get(), 5, profile.next_review_at);
	sqlite3_bind_int(stmt.get(), 6, profile.lapse_count);
	sqlite3_bind_int64(stmt.get(), 7, profile.last_practiced_at);
	sqlite3_bind_int64(stmt.get(), 8, profile.last_decay_at);
	sqlite3_bind_int64(stmt.get(), 9, profile.last_reviewed_at);
	sqlite3_bind_int(stmt.get(), 10, profile.last_response_time_ms);
	sqlite3_bind_int(stmt.get(), 11, profile.persistent_boost);
	sqlite3_bind_int(stmt.get(), 12, static_cast<int>(profile.mastered));
	sqlite3_bind_int(stmt.get(), 13, user_id_);
	sqlite3_bind_int(stmt.get(), 14, profile.word_id);
	sqlite3_bind_text(stmt.get(), 15, profile.textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
		WP_LEARNING_LOGW(log_tag_, "save profile update failed word_id=%d textbook=%s msg=%s", profile.word_id, profile.textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	if (sqlite3_changes(db) > 0) {
		return true;
	}

	const char *insert_sql =
		"INSERT INTO word_learning_profile(user_id, word_id, textbook_name, stage, strength, recall_score, output_score, next_review_at, lapse_count, last_practiced_at, last_decay_at, last_reviewed_at, last_response_time_ms, persistent_boost, mastered) "
		"VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
	stmt.reset();
	if (!PrepareStatement(db, insert_sql, &stmt)) {
		WP_LEARNING_LOGW(log_tag_, "save profile prepare insert failed word_id=%d textbook=%s msg=%s", profile.word_id, profile.textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt.get(), 1, user_id_);
	sqlite3_bind_int(stmt.get(), 2, profile.word_id);
	sqlite3_bind_text(stmt.get(), 3, profile.textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt.get(), 4, profile.stage);
	sqlite3_bind_int(stmt.get(), 5, profile.strength);
	sqlite3_bind_int(stmt.get(), 6, profile.recall_score);
	sqlite3_bind_int(stmt.get(), 7, profile.output_score);
	sqlite3_bind_int64(stmt.get(), 8, profile.next_review_at);
	sqlite3_bind_int(stmt.get(), 9, profile.lapse_count);
	sqlite3_bind_int64(stmt.get(), 10, profile.last_practiced_at);
	sqlite3_bind_int64(stmt.get(), 11, profile.last_decay_at);
	sqlite3_bind_int64(stmt.get(), 12, profile.last_reviewed_at);
	sqlite3_bind_int(stmt.get(), 13, profile.last_response_time_ms);
	sqlite3_bind_int(stmt.get(), 14, profile.persistent_boost);
	sqlite3_bind_int(stmt.get(), 15, static_cast<int>(profile.mastered));
	if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
		WP_LEARNING_LOGW(log_tag_, "save profile insert failed word_id=%d textbook=%s msg=%s", profile.word_id, profile.textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	return true;
}
bool WordMasteryDao::RecordAttempt(sqlite3 *db, const QuestionAttemptRecord &attempt) const {
	if (db == nullptr || attempt.word_id <= 0) {
		return false;
	}
	const char *sql =
		"INSERT INTO word_practice_history(user_id, word_id, question_type, target_skill, review_type, rating, response_time, correct, question_reason, practiced_at) "
		"VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
	StatementPtr stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return false;
	}
	sqlite3_bind_int(stmt.get(), 1, user_id_);
	sqlite3_bind_int(stmt.get(), 2, attempt.word_id);
	sqlite3_bind_int(stmt.get(), 3, attempt.question_type);
	sqlite3_bind_text(stmt.get(), 4, ToString(attempt.target_skill), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt.get(), 5, attempt.skipped ? "word_practice_skip" : "word_practice", -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt.get(), 6, attempt.correct ? 1 : 0);
	sqlite3_bind_int(stmt.get(), 7, MaxValue(0, attempt.response_time_ms));
	sqlite3_bind_int(stmt.get(), 8, attempt.correct ? 1 : 0);
	sqlite3_bind_text(stmt.get(), 9, attempt.question_reason.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt.get(), 10, attempt.practiced_at);
	return sqlite3_step(stmt.get()) == SQLITE_DONE;
}
bool WordMasteryDao::ApplyDueDecayIfNeeded(sqlite3 *db, WordMasteryProfile *profile) const {
	if (profile == nullptr || profile->word_id <= 0 || profile->next_review_at <= 0) {
		return false;
	}
	const int64_t now = NowSec();
	if (now <= profile->next_review_at) {
		return false;
	}
	if (DayIndexFromSec(profile->last_decay_at) == DayIndexFromSec(now)) {
		return false;
	}
	const double overdue_ratio = std::max(0.0, review_model::OverdueRatio(*profile, now));
	const int overdue_pressure = std::max<int>(1, static_cast<int>(std::ceil(overdue_ratio * 2.0)));
	const int decay_penalty = std::max(
		config::kDecayMinPenalty,
		std::min(
			config::kDecayMaxPenalty,
			overdue_pressure * config::kDecayPenaltyPerOverdueDay + profile->lapse_count));
	profile->strength = ClampScore(profile->strength - decay_penalty);
	if (overdue_ratio >= 1.0 && profile->recall_score > 0) {
		profile->recall_score = ClampEvidence(profile->recall_score - config::kDecayRecallDropPerTrigger);
	}
	profile->last_decay_at = now;
	SyncDerivedProfileState(profile);
	return SaveProfile(db, *profile);
}

LearningBatch LearningBatchPlanner::Build(const std::vector<SelectedWord> &selected_words,
					 const std::vector<WordMasteryProfile> &profiles,
				 LearningMode learning_mode) const {
	LearningBatch batch;
	if (selected_words.empty()) {
		return batch;
	}
	const int64_t now_sec = NowSec();
	const int total_slots = MinValue(config::kBatchMaxSlots, MaxValue(config::kBatchMinSlots, static_cast<int>(selected_words.size())));
	const bool cold_start_mode = learning_mode == LearningMode::ColdStart;
	const bool intensive_review_mode = learning_mode == LearningMode::IntensiveReview;
	int desired_new = cold_start_mode
		? MaxValue(config::kColdStartDesiredNewMin, MinValue(config::kColdStartDesiredNewMax, total_slots / config::kColdStartDesiredNewDivisor))
		: MaxValue(config::kNormalDesiredNewMin, MinValue(config::kNormalDesiredNewMax, total_slots / config::kNormalDesiredNewDivisor));
	int desired_weak = cold_start_mode
		? 0
		: (intensive_review_mode
			? MaxValue(config::kIntensiveDesiredWeakMin, MinValue(config::kIntensiveDesiredWeakMax, total_slots / config::kIntensiveDesiredWeakDivisor))
			: MaxValue(config::kNormalDesiredWeakMin, MinValue(config::kNormalDesiredWeakMax, total_slots / config::kNormalDesiredWeakDivisor)));
	int desired_review = MaxValue(config::kMinimumDesiredReviewSlots, total_slots - desired_new - desired_weak);

	std::vector<BatchWordPlan> new_words;
	std::vector<BatchWordPlan> review_words;
	std::vector<BatchWordPlan> weak_words;
	for (const auto &selected : selected_words) {
		const auto it = std::find_if(profiles.begin(), profiles.end(), [&selected](const WordMasteryProfile &profile) {
			return profile.word_id == selected.word_id;
		});
		const WordMasteryProfile *profile = (it != profiles.end()) ? &(*it) : nullptr;
		BatchWordPlan plan;
		plan.selected_word = selected;
		if (!selected.is_review) {
			plan.kind = BatchWordKind::NewWord;
			new_words.push_back(std::move(plan));
			continue;
		}
		const int strength = profile == nullptr ? 0 : profile->strength;
		const double overdue_ratio = profile != nullptr ? std::max(0.0, review_model::OverdueRatio(*profile, now_sec)) : 0.0;
		if (!cold_start_mode && profile != nullptr &&
			(overdue_ratio >= 0.5 ||
			 profile->lapse_count >= config::kWeakWordLapseThreshold ||
			 (strength < config::kWeakWordStrengthThreshold && overdue_ratio > 0.1))) {
			plan.kind = BatchWordKind::WeakWord;
			weak_words.push_back(std::move(plan));
		} else {
			plan.kind = BatchWordKind::ReviewWord;
			review_words.push_back(std::move(plan));
		}
	}

	auto append_up_to = [&batch](std::vector<BatchWordPlan> *source, int count) {
		while (source != nullptr && !source->empty() && count-- > 0) {
			batch.items.push_back(source->front());
			source->erase(source->begin());
		}
	};

	append_up_to(&weak_words, MinValue(desired_weak, static_cast<int>(weak_words.size())));
	append_up_to(&new_words, MinValue(desired_new, static_cast<int>(new_words.size())));
	append_up_to(&review_words, MinValue(desired_review, static_cast<int>(review_words.size())));

	while (static_cast<int>(batch.items.size()) < total_slots) {
		if (!review_words.empty()) {
			append_up_to(&review_words, 1);
		} else if (!new_words.empty()) {
			append_up_to(&new_words, 1);
		} else if (!weak_words.empty()) {
			append_up_to(&weak_words, 1);
		} else {
			break;
		}
	}

	for (const auto &plan : batch.items) {
		switch (plan.kind) {
			case BatchWordKind::NewWord:
				++batch.planned_new_words;
				break;
			case BatchWordKind::ReviewWord:
				++batch.planned_review_words;
				break;
			case BatchWordKind::WeakWord:
				++batch.planned_weak_words;
				break;
		}
	}
	return batch;
}

void BatchProgressTracker::Reset(const LearningBatch &batch, LearningMode learning_mode) {
	items_.clear();
	learning_mode_ = learning_mode;
	for (const auto &plan : batch.items) {
		ItemProgress progress;
		progress.kind = plan.kind;
		progress.completion_rule = CompletionRuleForKind(plan.kind);
		items_[plan.selected_word.word_id] = progress;
	}
}

void BatchProgressTracker::MarkPresented(int word_id) {
	auto it = items_.find(word_id);
	if (it == items_.end()) {
		return;
	}
	if (it->second.state == WordProgressState::NotStarted) {
		it->second.state = WordProgressState::InProgress;
	}
	++it->second.shown_count;
}

void BatchProgressTracker::MarkOutcome(int word_id,
			       BatchWordKind kind,
			       TrainingSkill skill,
			       bool correct) {
	auto it = items_.find(word_id);
	if (it == items_.end()) {
		return;
	}
	ItemProgress &progress = it->second;
	progress.kind = kind;
	progress.completion_rule = CompletionRuleForKind(kind);
	switch (skill) {
		case TrainingSkill::Recognition:
			progress.recognition_done = true;
			break;
		case TrainingSkill::Recall:
			progress.recall_done = true;
			break;
		case TrainingSkill::Output:
		case TrainingSkill::AdvancedSpeak:
			progress.output_attempted = true;
			break;
		default:
			break;
	}
	if (correct) {
		++progress.correct_count;
		++progress.any_correct_count;
		switch (skill) {
			case TrainingSkill::Recognition:
				++progress.recognition_count;
				progress.recognition_done = true;
				break;
			case TrainingSkill::Recall:
				++progress.recall_count;
				progress.recall_done = true;
				break;
			case TrainingSkill::Output:
			case TrainingSkill::AdvancedSpeak:
				++progress.output_count;
				break;
			default:
				break;
		}
	}
	if (IsProgressComplete(
			progress.completion_rule,
			progress.shown_count,
			progress.any_correct_count,
			progress.recognition_count,
			progress.recall_count,
			progress.output_count)) {
		progress.state = WordProgressState::Completed;
	}
	if (progress.state == WordProgressState::NotStarted) {
		progress.state = WordProgressState::InProgress;
	}
}

bool BatchProgressTracker::ContainsWord(int word_id) const {
	return items_.find(word_id) != items_.end();
}

WordProgressState BatchProgressTracker::ProgressState(int word_id) const {
	const auto it = items_.find(word_id);
	return it == items_.end() ? WordProgressState::NotStarted : it->second.state;
}

bool BatchProgressTracker::IsCompleted(int word_id) const {
	return ProgressState(word_id) == WordProgressState::Completed;
}

bool BatchProgressTracker::HasRecognitionCheckpoint(int word_id) const {
	const auto it = items_.find(word_id);
	return it != items_.end() && it->second.recognition_done;
}

bool BatchProgressTracker::HasRecallCheckpoint(int word_id) const {
	const auto it = items_.find(word_id);
	return it != items_.end() && it->second.recall_done;
}

bool BatchProgressTracker::IsBatchComplete() const {
	const int total_items = static_cast<int>(items_.size());
	if (total_items <= 0) {
		return false;
	}
	int completed_items = 0;
	for (const auto &entry : items_) {
		if (entry.second.state == WordProgressState::Completed) {
			++completed_items;
		}
	}
	return completed_items >= static_cast<int>(std::ceil(static_cast<double>(total_items) * config::kBatchCompletionRatio));
}

BatchProgressSummary BatchProgressTracker::BuildSummary() const {
	BatchProgressSummary summary;
	summary.total_items = static_cast<int>(items_.size());
	summary.recognition_coverage_ok = true;
	summary.recall_coverage_ok = true;
	summary.output_coverage_ok = (learning_mode_ == LearningMode::ColdStart);
	for (const auto &entry : items_) {
		const ItemProgress &progress = entry.second;
		const bool completed = (progress.state == WordProgressState::Completed);
		if (completed) {
			++summary.completed_items;
		}
		switch (progress.kind) {
			case BatchWordKind::NewWord:
				++summary.new_total;
				summary.recognition_coverage_ok = summary.recognition_coverage_ok && progress.recognition_done;
				summary.recall_coverage_ok = summary.recall_coverage_ok && progress.recall_done;
				summary.skill_coverage.recognition_done = summary.skill_coverage.recognition_done || progress.recognition_done;
				summary.skill_coverage.recall_done = summary.skill_coverage.recall_done || progress.recall_done;
				if (completed) {
					++summary.new_completed;
				}
				break;
			case BatchWordKind::ReviewWord:
				++summary.review_total;
				summary.output_coverage_ok = summary.output_coverage_ok || progress.output_attempted;
				summary.skill_coverage.output_attempted = summary.skill_coverage.output_attempted || progress.output_attempted;
				if (completed) {
					++summary.review_completed;
				}
				break;
			case BatchWordKind::WeakWord:
				++summary.weak_total;
				summary.output_coverage_ok = summary.output_coverage_ok || progress.output_attempted;
				summary.skill_coverage.output_attempted = summary.skill_coverage.output_attempted || progress.output_attempted;
				if (completed) {
					++summary.weak_completed;
				}
				break;
		}
	}
	summary.skill_coverage_ok = summary.recognition_coverage_ok && summary.recall_coverage_ok && summary.output_coverage_ok;
	summary.batch_completed = IsBatchComplete();
	return summary;
}

void QuestionScheduler::Reset() {
	word_seen_count_.clear();
	last_question_type_by_word_.clear();
	skill_question_cursor_.clear();
}

TrainingSkill QuestionScheduler::ChooseSkill(const BatchWordPlan &plan,
				    const WordMasteryProfile *profile,
				    const BatchProgressTracker &tracker) const {
	const bool needs_recognition = !tracker.HasRecognitionCheckpoint(plan.selected_word.word_id);
	const bool needs_recall = !tracker.HasRecallCheckpoint(plan.selected_word.word_id);
	const int recall_score = profile != nullptr ? profile->recall_score : 0;
	const int output_score = profile != nullptr ? profile->output_score : 0;

	double recognition_gain = needs_recognition ? 3.0 : 0.0;
	double recall_gain = (needs_recall ? 2.5 : 0.0) +
		std::max(0, config::kRecallToOutputThreshold - recall_score) * 1.2;
	double output_gain = (static_cast<double>(recall_score) / std::max(1, config::kRecallToOutputThreshold)) +
		std::max(0, config::kOutputToAdvancedSpeakThreshold - output_score) * 1.1;
	double advanced_gain = output_score >= config::kOutputToAdvancedSpeakThreshold
		? 1.0 + (output_score - config::kOutputToAdvancedSpeakThreshold + 1) * 0.5
		: -1.0;

	if (plan.kind == BatchWordKind::NewWord) {
		recognition_gain += 2.0;
		recall_gain += 1.0;
		output_gain -= 1.5;
		advanced_gain -= 2.0;
	} else if (plan.kind == BatchWordKind::WeakWord) {
		recall_gain += 0.8;
		output_gain += 0.5;
	}

	TrainingSkill best_skill = TrainingSkill::Recognition;
	double best_gain = recognition_gain;
	if (recall_gain > best_gain) {
		best_gain = recall_gain;
		best_skill = TrainingSkill::Recall;
	}
	if (output_gain > best_gain) {
		best_gain = output_gain;
		best_skill = TrainingSkill::Output;
	}
	if (advanced_gain > best_gain) {
		best_skill = TrainingSkill::AdvancedSpeak;
	}
	return best_skill;
}

ScheduledQuestion QuestionScheduler::BuildScheduledQuestion(int question_type,
					 int word_id,
					 TrainingSkill skill,
					 QuestionReasonType reason_type,
					 const WordMasteryProfile *profile) const {
	ScheduledQuestion scheduled;
	scheduled.has_value = true;
	scheduled.word_id = word_id;
	scheduled.question_type = question_type;
	scheduled.target_skill = skill;
	scheduled.reason_type = reason_type;
	scheduled.reason_text = BuildReasonJsonValue(reason_type, profile, skill);
	return scheduled;
}

void QuestionScheduler::AdvanceSkillQuestionCursor(TrainingSkill skill, int question_type) {
	const std::vector<int> base_types = [skill]() {
		switch (skill) {
			case TrainingSkill::Recognition:
				return std::vector<int>{kQuestionTypeImageChoice, kQuestionTypeMeaningChoice, kQuestionTypeAudioWordChoice, kQuestionTypeAudioMeaningChoice};
			case TrainingSkill::Recall:
				return std::vector<int>{kQuestionTypeWordToMeaning, kQuestionTypePairMatch, kQuestionTypeSpeakMeaning, kQuestionTypeImageChoice};
			case TrainingSkill::Output:
				return std::vector<int>{kQuestionTypeSentenceFillZh, kQuestionTypeSentenceBuildEn, kQuestionTypeSpeakWord, kQuestionTypeImageChoice};
			case TrainingSkill::AdvancedSpeak:
				return std::vector<int>{kQuestionTypeSpeakSentence, kQuestionTypeSpeakTranslate};
			default:
				return std::vector<int>{kQuestionTypeMeaningChoice, kQuestionTypeWordToMeaning, kQuestionTypeSentenceFillZh};
		}
	}();
	if (base_types.empty()) {
		return;
	}
	for (size_t index = 0; index < base_types.size(); ++index) {
		if (base_types[index] == question_type) {
			skill_question_cursor_[static_cast<int>(skill)] = (index + 1) % base_types.size();
			return;
		}
	}
}

std::vector<int> QuestionScheduler::QuestionTypesForSkill(TrainingSkill skill) const {
	const std::vector<int> base_types = [skill]() {
		switch (skill) {
			case TrainingSkill::Recognition:
				return std::vector<int>{kQuestionTypeImageChoice, kQuestionTypeMeaningChoice, kQuestionTypeAudioWordChoice, kQuestionTypeAudioMeaningChoice};
			case TrainingSkill::Recall:
				return std::vector<int>{kQuestionTypeWordToMeaning, kQuestionTypePairMatch, kQuestionTypeSpeakMeaning, kQuestionTypeImageChoice};
			case TrainingSkill::Output:
				return std::vector<int>{kQuestionTypeSentenceFillZh, kQuestionTypeSentenceBuildEn, kQuestionTypeSpeakWord, kQuestionTypeImageChoice};
			case TrainingSkill::AdvancedSpeak:
				return std::vector<int>{kQuestionTypeSpeakSentence, kQuestionTypeSpeakTranslate};
			default:
				return std::vector<int>{kQuestionTypeMeaningChoice, kQuestionTypeWordToMeaning, kQuestionTypeSentenceFillZh};
		}
	}();
	if (base_types.empty()) {
		return base_types;
	}
	const auto it = skill_question_cursor_.find(static_cast<int>(skill));
	const size_t cursor = it == skill_question_cursor_.end() ? 0 : (it->second % base_types.size());
	std::vector<int> rotated_types;
	rotated_types.reserve(base_types.size());
	for (size_t offset = 0; offset < base_types.size(); ++offset) {
		rotated_types.push_back(base_types[(cursor + offset) % base_types.size()]);
	}
	return rotated_types;
}

ScheduledQuestion QuestionScheduler::FindQuestionForWord(const std::unordered_map<int, std::vector<int>> &available_question_types,
					      int word_id,
					      TrainingSkill skill,
					      QuestionReasonType reason_type,
					      const WordMasteryProfile *profile) const {
	const auto it_available = available_question_types.find(word_id);
	if (it_available == available_question_types.end() || it_available->second.empty()) {
		return {};
	}
	const std::vector<int> &available_types = it_available->second;
	if (skill == TrainingSkill::AdvancedSpeak &&
		std::find(available_types.begin(), available_types.end(), kQuestionTypeSpeakSentence) == available_types.end() &&
		std::find(available_types.begin(), available_types.end(), kQuestionTypeSpeakTranslate) == available_types.end()) {
		skill = TrainingSkill::Output;
	}
	if (skill == TrainingSkill::Output &&
		std::find(available_types.begin(), available_types.end(), kQuestionTypeSentenceFillZh) == available_types.end() &&
		std::find(available_types.begin(), available_types.end(), kQuestionTypeSentenceBuildEn) == available_types.end() &&
		std::find(available_types.begin(), available_types.end(), kQuestionTypeSpeakWord) == available_types.end()) {
		skill = TrainingSkill::Recall;
	}
	const std::vector<int> preferred_types = QuestionTypesForSkill(skill);
	for (int question_type : preferred_types) {
		if (std::find(available_types.begin(), available_types.end(), question_type) == available_types.end()) {
			continue;
		}
		const auto it_type = last_question_type_by_word_.find(word_id);
		if (it_type != last_question_type_by_word_.end() && it_type->second == question_type && preferred_types.size() > 1) {
			continue;
		}
		return BuildScheduledQuestion(question_type, word_id, skill, reason_type, profile);
	}
	for (int question_type : preferred_types) {
		if (std::find(available_types.begin(), available_types.end(), question_type) != available_types.end()) {
			return BuildScheduledQuestion(question_type, word_id, skill, reason_type, profile);
		}
	}
	return {};
}

const BatchWordPlan *QuestionScheduler::FindPlan(const LearningBatch &batch, int word_id) const {
	for (const auto &plan : batch.items) {
		if (plan.selected_word.word_id == word_id) {
			return &plan;
		}
	}
	return nullptr;
}

const SelectedWord *QuestionScheduler::FindSelectedWord(const std::vector<SelectedWord> &selected_words, int word_id) const {
	for (const auto &selected_word : selected_words) {
		if (selected_word.word_id == word_id) {
			return &selected_word;
		}
	}
	return nullptr;
}

const WordMasteryProfile *QuestionScheduler::FindProfile(const std::vector<WordMasteryProfile> &profiles, int word_id) const {
	for (const auto &profile : profiles) {
		if (profile.word_id == word_id) {
			return &profile;
		}
	}
	return nullptr;
}

BatchWordKind QuestionScheduler::ResolveWordKind(const SelectedWord &selected_word,
						 const WordMasteryProfile *profile,
						 LearningMode learning_mode) const {
	if (!selected_word.is_review) {
		return BatchWordKind::NewWord;
	}
	const bool cold_start_mode = learning_mode == LearningMode::ColdStart;
	const int strength = profile == nullptr ? 0 : profile->strength;
	const double overdue_ratio = profile != nullptr ? std::max(0.0, review_model::OverdueRatio(*profile, NowSec())) : 0.0;
	if (!cold_start_mode && profile != nullptr &&
		(overdue_ratio >= 0.5 ||
		 profile->lapse_count >= config::kWeakWordLapseThreshold ||
		 (strength < config::kWeakWordStrengthThreshold && overdue_ratio > 0.1))) {
		return BatchWordKind::WeakWord;
	}
	return BatchWordKind::ReviewWord;
}

ScheduledQuestion QuestionScheduler::ScheduleNext(const std::unordered_map<int, std::vector<int>> &available_question_types,
					  const std::vector<SelectedWord> &selected_words,
					  const LearningBatch &batch,
					  const std::vector<WordMasteryProfile> &profiles,
					  const BatchProgressTracker &tracker,
					  LearningMode learning_mode,
					  int total_answered,
					  int hard_limit) {
	if (hard_limit > 0 && total_answered >= hard_limit) {
		return {};
	}

	std::vector<int> candidate_word_ids;
	for (const auto &plan : batch.items) {
		if (!tracker.IsCompleted(plan.selected_word.word_id)) {
			candidate_word_ids.push_back(plan.selected_word.word_id);
		}
	}
	const int64_t now_sec = NowSec();
	auto candidate_priority = [&](int word_id) {
		const BatchWordPlan *plan = FindPlan(batch, word_id);
		const SelectedWord *selected_word = FindSelectedWord(selected_words, word_id);
		const WordMasteryProfile *profile = FindProfile(profiles, word_id);
		if (plan == nullptr && selected_word == nullptr) {
			return -100000.0;
		}
		const BatchWordKind kind = plan != nullptr
			? plan->kind
			: ResolveWordKind(*selected_word, profile, learning_mode);
		double score = (kind == BatchWordKind::NewWord)
			? static_cast<double>(config::kSchedulerPriorityNewWord)
			: static_cast<double>(config::kSchedulerPriorityReviewBacklog);
		if (profile != nullptr) {
			const double overdue_ratio = std::max(0.0, review_model::OverdueRatio(*profile, now_sec));
			if (overdue_ratio > 0.0) {
				const double overdue_pressure = std::min(3.0, overdue_ratio);
				score += static_cast<double>(config::kSchedulerPriorityDueReview) + overdue_pressure * 24.0;
			}
			const double weakness_from_strength = std::max(0, config::kWeakWordStrengthThreshold - profile->strength) * 0.35;
			const double weakness_from_lapse = profile->lapse_count * 6.0;
			score += weakness_from_strength + weakness_from_lapse;
		}
		if (kind == BatchWordKind::WeakWord) {
			score += 10.0;
		}
		if (learning_mode == LearningMode::IntensiveReview && kind != BatchWordKind::NewWord) {
			score += static_cast<double>(config::kSchedulerIntensiveReviewBonus);
		}
		if (plan == nullptr) {
			score -= 15.0;
		}
		score -= static_cast<double>(word_seen_count_[word_id] * config::kSchedulerSeenPenaltyPerShow);
		return score;
	};
	std::stable_sort(candidate_word_ids.begin(), candidate_word_ids.end(), [&candidate_priority, this](int lhs, int rhs) {
		const double lhs_priority = candidate_priority(lhs);
		const double rhs_priority = candidate_priority(rhs);
		if (lhs_priority != rhs_priority) {
			return lhs_priority > rhs_priority;
		}
		return word_seen_count_[lhs] < word_seen_count_[rhs];
	});

	for (int word_id : candidate_word_ids) {
		const BatchWordPlan *plan = FindPlan(batch, word_id);
		const SelectedWord *selected_word = FindSelectedWord(selected_words, word_id);
		const WordMasteryProfile *profile = FindProfile(profiles, word_id);
		if (plan == nullptr && selected_word == nullptr) {
			continue;
		}
		const BatchWordKind kind = plan != nullptr
			? plan->kind
			: ResolveWordKind(*selected_word, profile, learning_mode);
		QuestionReasonType reason_type = QuestionReasonType::BatchTarget;
		if (kind == BatchWordKind::NewWord) {
			reason_type = QuestionReasonType::NewWord;
		} else if (kind == BatchWordKind::WeakWord) {
			reason_type = QuestionReasonType::WeakReinforce;
		} else if (profile != nullptr && profile->next_review_at > 0 && profile->next_review_at <= NowSec()) {
			reason_type = QuestionReasonType::ReviewDue;
		}
		TrainingSkill skill = ChooseSkill(
			plan != nullptr ? *plan : BatchWordPlan{*selected_word, kind},
			profile,
			tracker);
		const ScheduledQuestion scheduled = FindQuestionForWord(available_question_types,
								   word_id,
							   skill,
								   reason_type,
								   profile);
		if (scheduled.has_value) {
			return scheduled;
		}
	}
	return {};
}

void QuestionScheduler::RecordSkip(const ScheduledQuestion &scheduled) {
	if (!scheduled.has_value || scheduled.word_id <= 0) {
		return;
	}
	AdvanceSkillQuestionCursor(scheduled.target_skill, scheduled.question_type);
	last_question_type_by_word_[scheduled.word_id] = scheduled.question_type;
	++word_seen_count_[scheduled.word_id];
}

void QuestionScheduler::RecordResult(const ScheduledQuestion &scheduled) {
	if (!scheduled.has_value || scheduled.word_id <= 0) {
		return;
	}
	AdvanceSkillQuestionCursor(scheduled.target_skill, scheduled.question_type);
	last_question_type_by_word_[scheduled.word_id] = scheduled.question_type;
	++word_seen_count_[scheduled.word_id];
}

}  // namespace word_practice
