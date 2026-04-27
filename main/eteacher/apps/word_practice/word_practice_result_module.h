#pragma once

#include <string>

#include <sqlite3.h>

#include "eteacher/apps/word_practice/word_practice_session_pass_policy.h"
#include "eteacher/apps/word_practice/word_practice_session_module.h"
#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

struct Summary {
	bool passed = false;
	std::string summary_text;
};

struct DailyProgressState {
	int completed_words = 0;
	int target_words = 0;
	int progress_percent = 0;
};

class UserProgressDao {
public:
	explicit UserProgressDao(const char *log_tag = "WordPracticeApp", int user_id = 0);
	void SetUserId(int user_id);

	std::string DiscoverUserDbPath() const;
	bool EnsureStatsTables(sqlite3 *db) const;
	int QueryCurrentLevel() const;
	DailyProgressState QueryDailyProgress(const std::string &textbook_name, int target_words) const;
	bool UpdateDailyProgress(const std::string &textbook_name, int completed_words, int target_words) const;
	bool UpdateDailyProgress(sqlite3 *db, const std::string &textbook_name, int completed_words, int target_words) const;
	int QueryCompletedRounds(const std::string &textbook_name) const;
	bool RecordRoundCompletion(const std::string &textbook_name, bool passed) const;
	bool RecordRoundCompletion(sqlite3 *db, const std::string &textbook_name, bool passed) const;
	void SaveAnswerStats(const SessionModule &session,
				   int word_id,
					   const QuestionData &question,
					   const std::string &textbook_name,
					   bool correct,
					   bool round_finished,
					   bool round_passed) const;
	bool SaveAnswerStats(sqlite3 *db,
			     const SessionModule &session,
			     int word_id,
			     const QuestionData &question,
			     const std::string &textbook_name,
			     bool correct,
			     bool round_finished,
			     bool round_passed) const;

private:
	int QueryCurrentLevel(sqlite3 *db) const;
	DailyProgressState QueryDailyProgress(sqlite3 *db, const std::string &textbook_name, int target_words) const;
	int QueryCompletedRounds(sqlite3 *db, const std::string &textbook_name) const;

	const char *log_tag_;
	int user_id_ = 0;
};

class ResultModule {
public:
	explicit ResultModule(const char *log_tag = "WordPracticeApp", int user_id = 0);
	void SetUserId(int user_id);

	bool IsPassed(const SessionPassContext &context) const;
	Summary BuildSummary(const SessionPassContext &context) const;
	const UserProgressDao &ProgressDao() const;

private:
	SessionPassPolicy session_pass_policy_{};
	UserProgressDao progress_dao_;
};

}  // namespace word_practice
