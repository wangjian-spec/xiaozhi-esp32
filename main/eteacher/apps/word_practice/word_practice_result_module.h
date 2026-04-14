#pragma once

#include <string>

#include <sqlite3.h>

#include "eteacher/apps/word_practice/word_practice_session_module.h"
#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

struct Summary {
	bool passed = false;
	std::string summary_text;
};

class UserProgressDao {
public:
	explicit UserProgressDao(const char *log_tag = "WordPracticeApp");

	std::string DiscoverUserDbPath() const;
	bool EnsureStatsTables(sqlite3 *db) const;
	int QueryCurrentLevel() const;
	LearnedSnapshot QueryLearned(int question_id, const std::string &textbook) const;
	void SaveAnswerStats(const SessionModule &session,
					   const QuestionData &question,
					   const std::string &textbook_name,
					   bool correct) const;

private:
	int QueryCurrentLevel(sqlite3 *db) const;
	LearnedSnapshot QueryLearned(sqlite3 *db, int question_id, const std::string &textbook) const;

	const char *log_tag_;
};

class ResultModule {
public:
	explicit ResultModule(const char *log_tag = "WordPracticeApp");

	bool IsPassed(const SessionModule &session) const;
	Summary BuildSummary(const SessionModule &session) const;
	const UserProgressDao &ProgressDao() const;

private:
	UserProgressDao progress_dao_;
};

}  // namespace word_practice