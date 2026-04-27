#include "eteacher/apps/word_practice/word_practice_learning_module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include <esp_timer.h>

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

#define WP_LEARNING_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define WP_LEARNING_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)

namespace word_practice {
namespace {

constexpr const char *kLearningLogTag = "WordPracticeLearning";

constexpr int kRecentWordWindowSize = 5;
constexpr int kRecentWordMaxRounds = 2;
constexpr int kScoreMax = 100;
constexpr int kEvidenceMax = 5;
constexpr int kStageUpgradeScore = 60;
constexpr int kStageGuardScore = 40;
constexpr int kMistakeChainMaxTriggers = 3;
constexpr int64_t kMinReviewIntervalSec = 3600;
constexpr int64_t kMaxReviewIntervalSec = 21 * 86400;

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
	return value < 0 ? 0 : (value > kScoreMax ? kScoreMax : value);
}

int ClampEvidence(int value) {
	return value < 0 ? 0 : (value > kEvidenceMax ? kEvidenceMax : value);
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
			return 1.0;
		case TrainingSkill::Recall:
			return 1.5;
		case TrainingSkill::Output:
		case TrainingSkill::AdvancedSpeak:
			return 2.0;
		default:
			return 1.0;
	}
}

double ConfidenceFactor(int response_time_ms) {
	if (response_time_ms <= 0) {
		return 1.0;
	}
	if (response_time_ms <= 4000) {
		return 1.2;
	}
	if (response_time_ms <= 8000) {
		return 1.0;
	}
	if (response_time_ms <= 12000) {
		return 0.8;
	}
	return 0.65;
}

double ErrorSeverityFactor(TrainingSkill skill, int response_time_ms) {
	double factor = 1.0;
	if (skill == TrainingSkill::Output || skill == TrainingSkill::AdvancedSpeak) {
		factor += 0.35;
	} else if (skill == TrainingSkill::Recall) {
		factor += 0.2;
	}
	if (response_time_ms > 8000) {
		factor += 0.15;
	}
	return factor;
}

int BucketPriority(BatchWordKind kind) {
	switch (kind) {
		case BatchWordKind::WeakWord:
			return 3;
		case BatchWordKind::ReviewWord:
			return 2;
		case BatchWordKind::NewWord:
			return 1;
		default:
			return 0;
	}
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

int DeriveUiStage(const WordMasteryProfile &profile) {
	const int combined = static_cast<int>(std::lround(profile.familiarity * 0.6 + profile.stability * 0.4));
	int stage = 1 + (combined / 20);
	stage = MaxValue(1, MinValue(5, stage));
	if (profile.mastered && stage < 4) {
		stage = 4;
	}
	return stage;
}

bool IsMasteredProfile(const WordMasteryProfile &profile) {
	return profile.recall_score >= 3 &&
		profile.output_score >= 3 &&
		profile.consecutive_recall_correct >= 2 &&
		!profile.recent_review_failed &&
		profile.stability >= 60 &&
		profile.lapse_count <= 3;
}

void SyncDerivedProfileState(WordMasteryProfile *profile) {
	if (profile == nullptr) {
		return;
	}
	profile->familiarity = ClampScore(profile->familiarity);
	profile->stability = ClampScore(profile->stability);
	profile->recall_score = ClampEvidence(profile->recall_score);
	profile->output_score = ClampEvidence(profile->output_score);
	profile->recognition_score = profile->familiarity;
	profile->mastered = IsMasteredProfile(*profile);
	profile->stage = DeriveUiStage(*profile);
}

int64_t NextReviewIntervalSec(const WordMasteryProfile &profile) {
	const int64_t base_interval = 6 * 3600 + static_cast<int64_t>(profile.familiarity) * 720;
	const double error_factor = MaxValue(0.35, 1.0 - (static_cast<double>(profile.lapse_count) * 0.18));
	const double stability_factor = 0.75 + (static_cast<double>(profile.stability) / 100.0) * 1.25;
	const double recall_weight = 0.8 + static_cast<double>(profile.recall_score) * 0.2;
	const int64_t now = NowSec();
	const double recent_error_factor =
		(profile.last_error_at > 0 && (now - profile.last_error_at) < 86400) ? 0.55 : 1.0;
	const double interval = static_cast<double>(base_interval) * error_factor * stability_factor * recall_weight * recent_error_factor;
	return MaxValue<int64_t>(kMinReviewIntervalSec, MinValue<int64_t>(kMaxReviewIntervalSec, static_cast<int64_t>(std::llround(interval))));
}

void UpdateProfileFromAttempt(WordMasteryProfile *profile, const QuestionAttemptRecord &attempt) {
	if (profile == nullptr) {
		return;
	}
	const int64_t practiced_at = attempt.practiced_at > 0 ? attempt.practiced_at : NowSec();
	const double skill_weight = SkillWeight(attempt.target_skill);
	const double confidence_factor = ConfidenceFactor(attempt.response_time_ms);
	if (attempt.correct) {
		const int familiarity_delta_raw = static_cast<int>(std::lround(6.0 * skill_weight * confidence_factor));
		const int stability_delta_raw = static_cast<int>(std::lround(4.0 * skill_weight * confidence_factor));
		const int familiarity_delta = familiarity_delta_raw > 1 ? familiarity_delta_raw : 1;
		const int stability_delta = stability_delta_raw > 1 ? stability_delta_raw : 1;
		profile->familiarity = ClampScore(profile->familiarity + familiarity_delta);
		profile->stability = ClampScore(profile->stability + stability_delta);
		++profile->consecutive_correct;
		profile->consecutive_wrong = 0;
		if (attempt.target_skill == TrainingSkill::Recall) {
			profile->recall_score = ClampEvidence(profile->recall_score + 1);
			++profile->consecutive_recall_correct;
			profile->recent_review_failed = false;
		} else if (attempt.target_skill == TrainingSkill::Output ||
			   attempt.target_skill == TrainingSkill::AdvancedSpeak) {
			profile->output_score = ClampEvidence(profile->output_score + 1);
			profile->recent_review_failed = false;
		}
	} else {
		const double severity = ErrorSeverityFactor(attempt.target_skill, attempt.response_time_ms);
		const int familiarity_penalty_raw = static_cast<int>(std::lround(5.0 * skill_weight * severity));
		const int stability_penalty_raw = static_cast<int>(std::lround(6.0 * skill_weight * severity));
		const int familiarity_penalty = familiarity_penalty_raw > 1 ? familiarity_penalty_raw : 1;
		const int stability_penalty = stability_penalty_raw > 2 ? stability_penalty_raw : 2;
		profile->familiarity = ClampScore(profile->familiarity - familiarity_penalty);
		profile->stability = ClampScore(profile->stability - stability_penalty);
		profile->last_error_at = practiced_at;
		++profile->lapse_count;
		++profile->consecutive_wrong;
		profile->consecutive_correct = 0;
		profile->recent_review_failed = true;
		if (attempt.target_skill == TrainingSkill::Recall) {
			profile->recall_score = ClampEvidence(profile->recall_score - 1);
			profile->consecutive_recall_correct = 0;
		} else if (attempt.target_skill == TrainingSkill::Output ||
			   attempt.target_skill == TrainingSkill::AdvancedSpeak) {
			profile->output_score = ClampEvidence(profile->output_score - 1);
		}
	}
	profile->last_practiced_at = practiced_at;
		profile->last_response_time_ms = MaxValue(0, attempt.response_time_ms);
	SyncDerivedProfileState(profile);
	profile->next_review_at = practiced_at + NextReviewIntervalSec(*profile);
}

WordMasteryProfile BuildDefaultProfile(int user_id,
					   const SelectedWord &selected,
					   const std::string &textbook_name) {
	WordMasteryProfile profile;
	profile.user_id = user_id;
	profile.word_id = selected.word_id;
	profile.textbook_name = textbook_name;
	profile.familiarity = selected.is_review ? 36 : 8;
	profile.stability = selected.is_review ? 28 : 6;
	profile.recall_score = selected.is_review ? 1 : 0;
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
	profile->stage = sqlite3_column_int(stmt, 1);
	profile->familiarity = sqlite3_column_int(stmt, 2);
	profile->stability = sqlite3_column_int(stmt, 3);
	profile->recall_score = sqlite3_column_int(stmt, 4);
	profile->output_score = sqlite3_column_int(stmt, 5);
	profile->next_review_at = sqlite3_column_int64(stmt, 6);
	profile->lapse_count = sqlite3_column_int(stmt, 7);
	profile->last_practiced_at = sqlite3_column_int64(stmt, 8);
	profile->last_decay_at = sqlite3_column_int64(stmt, 9);
	profile->last_error_at = sqlite3_column_int64(stmt, 10);
	profile->consecutive_correct = sqlite3_column_int(stmt, 11);
	profile->consecutive_wrong = sqlite3_column_int(stmt, 12);
	profile->consecutive_recall_correct = sqlite3_column_int(stmt, 13);
	profile->recent_review_failed = sqlite3_column_int(stmt, 14) != 0;
	profile->last_response_time_ms = sqlite3_column_int(stmt, 15);
	profile->mastered = sqlite3_column_int(stmt, 16) != 0;
	profile->downgraded_from_stage = sqlite3_column_int(stmt, 17);
	SyncDerivedProfileState(profile);
}

std::string BuildLoadProfilesSql(size_t word_count) {
	std::string sql =
		"SELECT word_id, stage, COALESCE(familiarity, recognition_score), COALESCE(stability, 0), recall_score, output_score, next_review_at, lapse_count, "
		"last_practiced_at, last_decay_at, COALESCE(last_error_at, 0), consecutive_correct, consecutive_wrong, "
		"COALESCE(consecutive_recall_correct, 0), COALESCE(recent_review_failed, 0), COALESCE(last_response_time_ms, 0), COALESCE(mastered, 0), downgraded_from_stage "
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
	return eteacher::database_manager::DiscoverUserDataDbPath(log_tag_, nullptr);
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
		if (err != nullptr) {
			sqlite3_free(err);
		}
		return false;
	}
	if (sqlite3_exec(db, sql_history, nullptr, nullptr, &err) != SQLITE_OK) {
		if (err != nullptr) {
			sqlite3_free(err);
		}
		return false;
	}
	(void)sqlite3_exec(db, sql_profile_index, nullptr, nullptr, nullptr);
	(void)sqlite3_exec(db, sql_history_index, nullptr, nullptr, nullptr);
	return true;
}

WordMasteryProfile WordMasteryDao::LoadProfile(int word_id, const std::string &textbook_name) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return {};
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return {};
	}
	EnsureTables(db);
	WordMasteryProfile profile = LoadProfile(db, word_id, textbook_name);
	sqlite3_close(db);
	return profile;
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
	sqlite3_close(db);
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
		"SELECT stage, COALESCE(familiarity, recognition_score), COALESCE(stability, 0), recall_score, output_score, next_review_at, lapse_count, "
		"last_practiced_at, last_decay_at, COALESCE(last_error_at, 0), consecutive_correct, consecutive_wrong, "
		"COALESCE(consecutive_recall_correct, 0), COALESCE(recent_review_failed, 0), COALESCE(last_response_time_ms, 0), COALESCE(mastered, 0), downgraded_from_stage "
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
	if (!ExecSql(db, "BEGIN IMMEDIATE TRANSACTION;")) {
		sqlite3_close(db);
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
	sqlite3_close(db);
	return decayed_count;
}

bool WordMasteryDao::SaveProfile(const WordMasteryProfile &profile) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return false;
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return false;
	}
	EnsureTables(db);
	const bool ok = SaveProfile(db, profile);
	sqlite3_close(db);
	return ok;
}

bool WordMasteryDao::ApplyAttempt(WordMasteryProfile *profile, const QuestionAttemptRecord &attempt) const {
	if (profile == nullptr || profile->word_id <= 0 || attempt.word_id != profile->word_id) {
		WP_LEARNING_LOGW(log_tag_, "apply attempt skipped invalid profile word_id=%d attempt_word_id=%d", profile ? profile->word_id : -1, attempt.word_id);
		return false;
	}
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		WP_LEARNING_LOGW(log_tag_, "apply attempt failed: user db path missing word_id=%d textbook=%s", attempt.word_id, attempt.textbook_name.c_str());
		return false;
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
		WP_LEARNING_LOGW(log_tag_, "apply attempt failed: open user db path=%s msg=%s", user_db.c_str(), db ? sqlite3_errmsg(db) : "null");
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return false;
	}
	EnsureTables(db);
	const bool ok = ApplyAttempt(db, profile, attempt);
	sqlite3_close(db);
	return ok;
}

bool WordMasteryDao::ApplyAttempt(sqlite3 *db, WordMasteryProfile *profile, const QuestionAttemptRecord &attempt) const {
	if (db == nullptr || profile == nullptr || profile->word_id <= 0 || attempt.word_id != profile->word_id) {
		WP_LEARNING_LOGW(log_tag_, "apply attempt skipped invalid db/profile word_id=%d attempt_word_id=%d", profile ? profile->word_id : -1, attempt.word_id);
		return false;
	}
	EnsureTables(db);
	if (!attempt.textbook_name.empty()) {
		profile->textbook_name = attempt.textbook_name;
	}
	UpdateProfileFromAttempt(profile, attempt);
	const bool saved = SaveProfile(db, *profile);
	const bool recorded = RecordAttempt(db, attempt);
	WP_LEARNING_LOGI(
		log_tag_,
		"apply attempt db=%s word_id=%d textbook=%s qtype=%d correct=%d save_profile=%d record_history=%d familiarity=%d stability=%d recall=%d output=%d mastered=%d next_review_at=%d",
		"external",
		attempt.word_id,
		profile->textbook_name.c_str(),
		attempt.question_type,
		attempt.correct ? 1 : 0,
		saved ? 1 : 0,
		recorded ? 1 : 0,
		profile->familiarity,
		profile->stability,
		profile->recall_score,
		profile->output_score,
		profile->mastered ? 1 : 0,
		static_cast<int>(profile->next_review_at));
	return saved && recorded;
}

bool WordMasteryDao::SaveProfile(sqlite3 *db, const WordMasteryProfile &profile) const {
	if (db == nullptr || profile.word_id <= 0 || profile.textbook_name.empty()) {
		return false;
	}
	const char *update_sql =
		"UPDATE word_learning_profile SET "
		"stage=?, familiarity=?, stability=?, recognition_score=?, recall_score=?, output_score=?, next_review_at=?, lapse_count=?, "
		"last_practiced_at=?, last_decay_at=?, last_error_at=?, consecutive_correct=?, consecutive_wrong=?, consecutive_recall_correct=?, "
		"recent_review_failed=?, last_response_time_ms=?, mastered=?, downgraded_from_stage=? "
		"WHERE user_id=? AND word_id=? AND textbook_name=?;";
	StatementPtr stmt;
	if (!PrepareStatement(db, update_sql, &stmt)) {
		WP_LEARNING_LOGW(log_tag_, "save profile prepare update failed word_id=%d textbook=%s msg=%s", profile.word_id, profile.textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt.get(), 1, profile.stage);
	sqlite3_bind_int(stmt.get(), 2, profile.familiarity);
	sqlite3_bind_int(stmt.get(), 3, profile.stability);
	sqlite3_bind_int(stmt.get(), 4, profile.recognition_score);
	sqlite3_bind_int(stmt.get(), 5, profile.recall_score);
	sqlite3_bind_int(stmt.get(), 6, profile.output_score);
	sqlite3_bind_int64(stmt.get(), 7, profile.next_review_at);
	sqlite3_bind_int(stmt.get(), 8, profile.lapse_count);
	sqlite3_bind_int64(stmt.get(), 9, profile.last_practiced_at);
	sqlite3_bind_int64(stmt.get(), 10, profile.last_decay_at);
	sqlite3_bind_int64(stmt.get(), 11, profile.last_error_at);
	sqlite3_bind_int(stmt.get(), 12, profile.consecutive_correct);
	sqlite3_bind_int(stmt.get(), 13, profile.consecutive_wrong);
	sqlite3_bind_int(stmt.get(), 14, profile.consecutive_recall_correct);
	sqlite3_bind_int(stmt.get(), 15, profile.recent_review_failed ? 1 : 0);
	sqlite3_bind_int(stmt.get(), 16, profile.last_response_time_ms);
	sqlite3_bind_int(stmt.get(), 17, profile.mastered ? 1 : 0);
	sqlite3_bind_int(stmt.get(), 18, profile.downgraded_from_stage);
	sqlite3_bind_int(stmt.get(), 19, user_id_);
	sqlite3_bind_int(stmt.get(), 20, profile.word_id);
	sqlite3_bind_text(stmt.get(), 21, profile.textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
		WP_LEARNING_LOGW(log_tag_, "save profile update failed word_id=%d textbook=%s msg=%s", profile.word_id, profile.textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	if (sqlite3_changes(db) > 0) {
		return true;
	}

	const char *insert_sql =
		"INSERT INTO word_learning_profile(user_id, word_id, textbook_name, stage, familiarity, stability, recognition_score, recall_score, output_score, next_review_at, lapse_count, last_practiced_at, last_decay_at, last_error_at, consecutive_correct, consecutive_wrong, consecutive_recall_correct, recent_review_failed, last_response_time_ms, mastered, downgraded_from_stage) "
		"VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
	stmt.reset();
	if (!PrepareStatement(db, insert_sql, &stmt)) {
		WP_LEARNING_LOGW(log_tag_, "save profile prepare insert failed word_id=%d textbook=%s msg=%s", profile.word_id, profile.textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	sqlite3_bind_int(stmt.get(), 1, user_id_);
	sqlite3_bind_int(stmt.get(), 2, profile.word_id);
	sqlite3_bind_text(stmt.get(), 3, profile.textbook_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt.get(), 4, profile.stage);
	sqlite3_bind_int(stmt.get(), 5, profile.familiarity);
	sqlite3_bind_int(stmt.get(), 6, profile.stability);
	sqlite3_bind_int(stmt.get(), 7, profile.recognition_score);
	sqlite3_bind_int(stmt.get(), 8, profile.recall_score);
	sqlite3_bind_int(stmt.get(), 9, profile.output_score);
	sqlite3_bind_int64(stmt.get(), 10, profile.next_review_at);
	sqlite3_bind_int(stmt.get(), 11, profile.lapse_count);
	sqlite3_bind_int64(stmt.get(), 12, profile.last_practiced_at);
	sqlite3_bind_int64(stmt.get(), 13, profile.last_decay_at);
	sqlite3_bind_int64(stmt.get(), 14, profile.last_error_at);
	sqlite3_bind_int(stmt.get(), 15, profile.consecutive_correct);
	sqlite3_bind_int(stmt.get(), 16, profile.consecutive_wrong);
	sqlite3_bind_int(stmt.get(), 17, profile.consecutive_recall_correct);
	sqlite3_bind_int(stmt.get(), 18, profile.recent_review_failed ? 1 : 0);
	sqlite3_bind_int(stmt.get(), 19, profile.last_response_time_ms);
	sqlite3_bind_int(stmt.get(), 20, profile.mastered ? 1 : 0);
	sqlite3_bind_int(stmt.get(), 21, profile.downgraded_from_stage);
	if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
		WP_LEARNING_LOGW(log_tag_, "save profile insert failed word_id=%d textbook=%s msg=%s", profile.word_id, profile.textbook_name.c_str(), sqlite3_errmsg(db));
		return false;
	}
	return true;
}

bool WordMasteryDao::RecordAttempt(const QuestionAttemptRecord &attempt) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return false;
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return false;
	}
	EnsureTables(db);
	const bool ok = RecordAttempt(db, attempt);
	sqlite3_close(db);
	return ok;
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
	sqlite3_bind_text(stmt.get(), 5, "word_practice", -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt.get(), 6, attempt.correct ? 1 : 0);
	sqlite3_bind_int(stmt.get(), 7, MaxValue(0, attempt.response_time_ms));
	sqlite3_bind_int(stmt.get(), 8, attempt.correct ? 1 : 0);
	sqlite3_bind_text(stmt.get(), 9, attempt.question_reason.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt.get(), 10, attempt.practiced_at);
	return sqlite3_step(stmt.get()) == SQLITE_DONE;
}

bool WordMasteryDao::ApplyDueDecayIfNeeded(WordMasteryProfile *profile) const {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return false;
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return false;
	}
	EnsureTables(db);
	const bool updated = ApplyDueDecayIfNeeded(db, profile);
	sqlite3_close(db);
	return updated;
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
	auto decay_metric = [](int value) {
		if (value >= 80) {
			return value - 2 > 0 ? value - 2 : 0;
		}
		if (value >= 55) {
			return value - 1 > 0 ? value - 1 : 0;
		}
		return value;
	};
	profile->familiarity = decay_metric(profile->familiarity);
	profile->stability = decay_metric(profile->stability);
	profile->last_decay_at = now;
	SyncDerivedProfileState(profile);
	return SaveProfile(db, *profile);
}

LearningBatch LearningBatchPlanner::Build(const std::vector<SelectedWord> &selected_words,
					 const std::vector<WordMasteryProfile> &profiles,
					 bool cold_start_mode) const {
	LearningBatch batch;
	if (selected_words.empty()) {
		return batch;
	}
	const int total_slots = MinValue(10, MaxValue(8, static_cast<int>(selected_words.size())));
	int desired_new = cold_start_mode ? MaxValue(3, MinValue(5, total_slots / 2)) : MaxValue(2, MinValue(4, total_slots / 3));
	int desired_weak = cold_start_mode ? 0 : MaxValue(1, MinValue(3, total_slots / 5));
	int desired_review = MaxValue(1, total_slots - desired_new - desired_weak);

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
		const int stability = profile == nullptr ? 0 : profile->stability;
		if (!cold_start_mode && profile != nullptr && (profile->recent_review_failed || profile->lapse_count > 0 || stability < 45)) {
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

void BatchProgressTracker::Reset(const LearningBatch &batch) {
	items_.clear();
	for (const auto &plan : batch.items) {
		ItemProgress progress;
		progress.kind = plan.kind;
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
			       QuestionReasonType reason_type,
			       bool correct) {
	auto it = items_.find(word_id);
	if (it == items_.end()) {
		return;
	}
	ItemProgress &progress = it->second;
	progress.kind = kind;
	if (correct) {
		++progress.correct_count;
	}
	switch (kind) {
		case BatchWordKind::NewWord:
			if (correct && skill == TrainingSkill::Recognition) {
				progress.recognition_done = true;
			}
			if (correct && skill == TrainingSkill::Recall) {
				progress.recall_done = true;
			}
			if (progress.recognition_done && progress.recall_done) {
				progress.state = WordProgressState::Completed;
			}
			break;
		case BatchWordKind::ReviewWord:
			if (correct) {
				progress.state = WordProgressState::Completed;
			}
			break;
		case BatchWordKind::WeakWord:
			if (reason_type == QuestionReasonType::MistakeFollowup || reason_type == QuestionReasonType::WeakReinforce) {
				progress.state = WordProgressState::Completed;
			}
			break;
	}
	if (progress.state == WordProgressState::NotStarted) {
		progress.state = WordProgressState::InProgress;
	}
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
	return completed_items >= static_cast<int>(std::ceil(static_cast<double>(total_items) * 0.8));
}

BatchProgressSummary BatchProgressTracker::BuildSummary() const {
	BatchProgressSummary summary;
	summary.total_items = static_cast<int>(items_.size());
	for (const auto &entry : items_) {
		const ItemProgress &progress = entry.second;
		const bool completed = (progress.state == WordProgressState::Completed);
		if (completed) {
			++summary.completed_items;
		}
		switch (progress.kind) {
			case BatchWordKind::NewWord:
				++summary.new_total;
				if (completed) {
					++summary.new_completed;
				}
				break;
			case BatchWordKind::ReviewWord:
				++summary.review_total;
				if (completed) {
					++summary.review_completed;
				}
				break;
			case BatchWordKind::WeakWord:
				++summary.weak_total;
				if (completed) {
					++summary.weak_completed;
				}
				break;
		}
	}
	summary.batch_completed = IsBatchComplete();
	return summary;
}

void QuestionScheduler::Reset() {
	recent_words_.clear();
	recent_rounds_.clear();
	word_seen_count_.clear();
	last_question_type_by_word_.clear();
	last_skill_by_word_.clear();
	mistake_cooldown_until_.clear();
	mistake_queue_.clear();
	recent_skill_history_.clear();
	last_presented_word_id_ = 0;
	consecutive_same_word_count_ = 0;
	mistake_chain_trigger_count_ = 0;
	mistake_chain_question_count_ = 0;
}

int QuestionScheduler::ExtractWordId(const QuestionData &question) const {
	return question.id > 100 ? (question.id / 100) : 0;
}

double QuestionScheduler::QuestionQualityScore(const QuestionData &question) const {
	switch (question.type) {
		case 9:
		case 10:
			return 0.4;
		case 5:
		case 6:
			return 0.8;
		default:
			return 1.0;
	}
}

TrainingSkill QuestionScheduler::ChooseSkill(const BatchWordPlan &plan,
				    const WordMasteryProfile *profile,
				    const BatchProgressTracker &tracker,
				    bool forced_by_mistake_chain,
				    TrainingSkill forced_skill,
				    bool cold_start_mode,
				    bool prefer_easy_confirmation) const {
	if (forced_by_mistake_chain && forced_skill != TrainingSkill::Unknown) {
		return forced_skill;
	}
	if (plan.kind == BatchWordKind::NewWord) {
		if (!tracker.HasRecognitionCheckpoint(plan.selected_word.word_id)) {
			return TrainingSkill::Recognition;
		}
		if (!tracker.HasRecallCheckpoint(plan.selected_word.word_id)) {
			return TrainingSkill::Recall;
		}
	}
	if (cold_start_mode) {
		if (prefer_easy_confirmation) {
			return TrainingSkill::Recognition;
		}
		if (profile == nullptr || profile->familiarity < 25) {
			return TrainingSkill::Recognition;
		}
		return TrainingSkill::Recall;
	}
	if (profile == nullptr) {
		return TrainingSkill::Recognition;
	}
	if (profile->familiarity < 35) {
		return TrainingSkill::Recognition;
	}
	if (profile->stability < 30) {
		return TrainingSkill::Recognition;
	}
	if (profile->recall_score < 3 || profile->consecutive_recall_correct < 2) {
		return TrainingSkill::Recall;
	}
	if (profile->output_score < 3 || profile->recent_review_failed) {
		return TrainingSkill::Recall;
	}
	if (profile->output_score >= 4 && profile->stability >= 45) {
		return TrainingSkill::AdvancedSpeak;
	}
	return TrainingSkill::Output;
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
				return std::vector<int>{1, 2, 11, 12};
			case TrainingSkill::Recall:
				return std::vector<int>{1, 3, 4, 8};
			case TrainingSkill::Output:
				return std::vector<int>{1, 5, 6, 7};
			case TrainingSkill::AdvancedSpeak:
				return std::vector<int>{9, 10};
			default:
				return std::vector<int>{2, 3, 5};
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
				return std::vector<int>{1, 2, 11, 12};
			case TrainingSkill::Recall:
				return std::vector<int>{1, 3, 4, 8};
			case TrainingSkill::Output:
				return std::vector<int>{1, 5, 6, 7};
			case TrainingSkill::AdvancedSpeak:
				return std::vector<int>{9, 10};
			default:
				return std::vector<int>{2, 3, 5};
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
		std::find(available_types.begin(), available_types.end(), 9) == available_types.end() &&
		std::find(available_types.begin(), available_types.end(), 10) == available_types.end()) {
		skill = TrainingSkill::Output;
	}
	const std::vector<int> preferred_types = QuestionTypesForSkill(skill);
	for (int question_type : preferred_types) {
		if (std::find(available_types.begin(), available_types.end(), question_type) == available_types.end()) {
			continue;
		}
			const auto it_type = last_question_type_by_word_.find(word_id);
			if (it_type != last_question_type_by_word_.end() && it_type->second == question_type) {
				continue;
			}
			const auto it_skill = last_skill_by_word_.find(word_id);
			if (it_skill != last_skill_by_word_.end() && it_skill->second == skill && preferred_types.size() > 1) {
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

const WordMasteryProfile *QuestionScheduler::FindProfile(const std::vector<WordMasteryProfile> &profiles, int word_id) const {
	for (const auto &profile : profiles) {
		if (profile.word_id == word_id) {
			return &profile;
		}
	}
	return nullptr;
}

int QuestionScheduler::RecentWordRounds(int word_id) const {
	const auto it = recent_rounds_.find(word_id);
	return it == recent_rounds_.end() ? 0 : it->second;
}

void QuestionScheduler::TouchRecentWord(int word_id) {
	auto it = std::find(recent_words_.begin(), recent_words_.end(), word_id);
	if (it == recent_words_.end()) {
		if (recent_words_.size() >= kRecentWordWindowSize) {
			recent_rounds_.erase(recent_words_.front());
			recent_words_.pop_front();
		}
		recent_words_.push_back(word_id);
		recent_rounds_[word_id] = 1;
	} else {
		recent_rounds_[word_id] += 1;
	}
}

void QuestionScheduler::MaybeRotateRecentWord(const LearningBatch &batch) {
	while (!recent_words_.empty() && RecentWordRounds(recent_words_.front()) > kRecentWordMaxRounds) {
		recent_rounds_.erase(recent_words_.front());
		recent_words_.pop_front();
	}
	if (recent_words_.size() >= kRecentWordWindowSize) {
		return;
	}
	for (const auto &plan : batch.items) {
		if (std::find(recent_words_.begin(), recent_words_.end(), plan.selected_word.word_id) == recent_words_.end()) {
			recent_words_.push_back(plan.selected_word.word_id);
			recent_rounds_[plan.selected_word.word_id] = 1;
			break;
		}
	}
}

bool QuestionScheduler::CanTriggerMistakeChain(int word_id, int total_answered) const {
	const auto it = mistake_cooldown_until_.find(word_id);
	if (it != mistake_cooldown_until_.end() && total_answered < it->second) {
		return false;
	}
	if (mistake_chain_trigger_count_ >= kMistakeChainMaxTriggers) {
		return false;
	}
	return true;
}

TrainingSkill QuestionScheduler::DowngradedSkill(TrainingSkill skill) const {
	switch (skill) {
		case TrainingSkill::AdvancedSpeak:
			return TrainingSkill::Output;
		case TrainingSkill::Output:
			return TrainingSkill::Recall;
		case TrainingSkill::Recall:
			return TrainingSkill::Recognition;
		case TrainingSkill::Recognition:
		default:
			return TrainingSkill::Recognition;
	}
}

std::string QuestionScheduler::BuildReasonJson(QuestionReasonType reason_type,
					 const WordMasteryProfile *profile,
					 TrainingSkill target_skill) const {
	return BuildReasonJsonValue(reason_type, profile, target_skill);
}

ScheduledQuestion QuestionScheduler::ScheduleNext(const std::unordered_map<int, std::vector<int>> &available_question_types,
					  const LearningBatch &batch,
					  const std::vector<WordMasteryProfile> &profiles,
					  const BatchProgressTracker &tracker,
					  bool cold_start_mode,
					  int total_answered,
					  int hard_limit,
					  bool prefer_easy_confirmation) {
	if (hard_limit > 0 && total_answered >= hard_limit) {
		return {};
	}
	MaybeRotateRecentWord(batch);
	if (!cold_start_mode && !mistake_queue_.empty()) {
		const MistakeFollowup followup = mistake_queue_.front();
		mistake_queue_.pop_front();
		const BatchWordPlan *plan = FindPlan(batch, followup.word_id);
		const WordMasteryProfile *profile = FindProfile(profiles, followup.word_id);
		if (plan != nullptr && !tracker.IsCompleted(followup.word_id)) {
			ScheduledQuestion scheduled = FindQuestionForWord(available_question_types,
								     followup.word_id,
							     ChooseSkill(*plan, profile, tracker, true, followup.forced_skill, cold_start_mode, false),
								     QuestionReasonType::MistakeFollowup,
								     profile);
			if (scheduled.has_value) {
				++mistake_chain_question_count_;
				TouchRecentWord(scheduled.word_id);
				return scheduled;
			}
		}
	}

	std::vector<int> candidate_word_ids;
	for (int word_id : recent_words_) {
		if (word_seen_count_[word_id] <= 1 && !tracker.IsCompleted(word_id)) {
			candidate_word_ids.push_back(word_id);
		}
	}
	if (candidate_word_ids.empty()) {
		for (const auto &plan : batch.items) {
			if (tracker.IsCompleted(plan.selected_word.word_id)) {
				continue;
			}
			candidate_word_ids.push_back(plan.selected_word.word_id);
		}
	}
	const int64_t now_sec = NowSec();
	auto candidate_priority = [&](int word_id) {
		const BatchWordPlan *plan = FindPlan(batch, word_id);
		const WordMasteryProfile *profile = FindProfile(profiles, word_id);
		if (plan == nullptr) {
			return -100000;
		}
		int score = prefer_easy_confirmation
			? (plan->kind == BatchWordKind::ReviewWord ? 300 : (plan->kind == BatchWordKind::NewWord ? 200 : 100))
			: (BucketPriority(plan->kind) * 100);
		if (!prefer_easy_confirmation && profile != nullptr) {
			if (plan->kind != BatchWordKind::NewWord && profile->next_review_at > 0 && profile->next_review_at <= now_sec) {
				const int overdue_days = static_cast<int>(std::min<int64_t>(7, (now_sec - profile->next_review_at) / 86400));
				score += 40 + overdue_days * 5;
			}
			score += std::max(0, 40 - profile->stability);
			score += std::max(0, 35 - profile->familiarity);
		}
		score -= word_seen_count_[word_id] * 5;
		return score;
	};
	std::stable_sort(candidate_word_ids.begin(), candidate_word_ids.end(), [&candidate_priority, this](int lhs, int rhs) {
		const int lhs_priority = candidate_priority(lhs);
		const int rhs_priority = candidate_priority(rhs);
		if (lhs_priority != rhs_priority) {
			return lhs_priority > rhs_priority;
		}
		return word_seen_count_[lhs] < word_seen_count_[rhs];
	});

	for (int word_id : candidate_word_ids) {
		if (last_presented_word_id_ == word_id && consecutive_same_word_count_ >= 2) {
			continue;
		}
		const BatchWordPlan *plan = FindPlan(batch, word_id);
		const WordMasteryProfile *profile = FindProfile(profiles, word_id);
		if (plan == nullptr) {
			continue;
		}
		QuestionReasonType reason_type = QuestionReasonType::BatchTarget;
		if (plan->kind == BatchWordKind::NewWord) {
			reason_type = QuestionReasonType::NewWord;
		} else if (plan->kind == BatchWordKind::WeakWord) {
			reason_type = QuestionReasonType::WeakReinforce;
		} else if (profile != nullptr && profile->next_review_at > 0 && profile->next_review_at <= NowSec()) {
			reason_type = QuestionReasonType::ReviewDue;
		}
		TrainingSkill skill = ChooseSkill(
			*plan,
			profile,
			tracker,
			false,
			TrainingSkill::Unknown,
			cold_start_mode,
			prefer_easy_confirmation);
		if (recent_skill_history_.size() >= 2 &&
			recent_skill_history_[recent_skill_history_.size() - 1] == TrainingSkill::Output &&
			recent_skill_history_[recent_skill_history_.size() - 2] == TrainingSkill::Output &&
			skill == TrainingSkill::Output) {
			skill = TrainingSkill::Recall;
		}
		const ScheduledQuestion scheduled = FindQuestionForWord(available_question_types,
								   word_id,
							   skill,
								   reason_type,
								   profile);
		if (scheduled.has_value) {
			TouchRecentWord(word_id);
			return scheduled;
		}
	}
	return {};
}

void QuestionScheduler::RecordResult(const ScheduledQuestion &scheduled,
				 const WordMasteryProfile &profile,
				 bool correct,
				 bool cold_start_mode,
				 int total_answered,
				 int hard_limit) {
	if (!scheduled.has_value || scheduled.word_id <= 0) {
		return;
	}
	AdvanceSkillQuestionCursor(scheduled.target_skill, scheduled.question_type);
	last_question_type_by_word_[scheduled.word_id] = scheduled.question_type;
	last_skill_by_word_[scheduled.word_id] = scheduled.target_skill;
	++word_seen_count_[scheduled.word_id];
	if (last_presented_word_id_ == scheduled.word_id) {
		++consecutive_same_word_count_;
	} else {
		last_presented_word_id_ = scheduled.word_id;
		consecutive_same_word_count_ = 1;
	}
	recent_skill_history_.push_back(scheduled.target_skill);
	if (recent_skill_history_.size() > 3) {
		recent_skill_history_.pop_front();
	}
	if (profile.consecutive_recall_correct >= 2 || profile.mastered) {
		recent_words_.erase(std::remove(recent_words_.begin(), recent_words_.end(), scheduled.word_id), recent_words_.end());
		recent_rounds_.erase(scheduled.word_id);
	}
	if (!correct && !cold_start_mode && CanTriggerMistakeChain(scheduled.word_id, total_answered)) {
		mistake_queue_.push_back({scheduled.word_id, DowngradedSkill(scheduled.target_skill)});
		mistake_cooldown_until_[scheduled.word_id] = total_answered + 3;
		++mistake_chain_trigger_count_;
	}
	const int max_mistake_chain_questions_raw = static_cast<int>(std::floor(static_cast<double>(hard_limit) * 0.3));
	const int max_mistake_chain_questions = max_mistake_chain_questions_raw > 1 ? max_mistake_chain_questions_raw : 1;
	if (mistake_chain_question_count_ > max_mistake_chain_questions) {
		mistake_queue_.clear();
	}
}

}  // namespace word_practice
