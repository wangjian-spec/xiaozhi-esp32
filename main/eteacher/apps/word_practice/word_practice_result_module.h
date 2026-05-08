#pragma once

#include <string>

#include <sqlite3.h>

#include "eteacher/apps/word_practice/word_practice_session_module.h"
#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

class UserProgressDao {
public:
	explicit UserProgressDao(const char *log_tag = "WordPracticeApp", int user_id = 0);
	void SetUserId(int user_id);

	std::string DiscoverUserDbPath() const;
	bool EnsureStatsTables(sqlite3 *db) const;
	int QueryAppStateInt(const std::string &key, int fallback_value = 0) const;
	int QueryAppStateInt(sqlite3 *db, const std::string &key, int fallback_value = 0) const;
	bool SaveAppStateInt(const std::string &key, int value) const;
	bool SaveAppStateInt(sqlite3 *db, const std::string &key, int value) const;
	std::string QueryAppStateText(const std::string &key, const std::string &fallback_value = {}) const;
	std::string QueryAppStateText(sqlite3 *db, const std::string &key, const std::string &fallback_value = {}) const;
	bool SaveAppStateText(const std::string &key, const std::string &value) const;
	bool SaveAppStateText(sqlite3 *db, const std::string &key, const std::string &value) const;
	bool UpdateDailyProgress(sqlite3 *db,
				    const std::string &textbook_name,
				    int completed_words,
				    int target_words,
				    int progress_percent,
				    int completed_word_id = 0) const;
	bool RecordRoundCompletion(const std::string &textbook_name, bool passed) const;
	bool RecordRoundCompletion(sqlite3 *db, const std::string &textbook_name, bool passed) const;
	bool SaveAnswerStats(sqlite3 *db,
			     const SessionModule &session,
			     int word_id,
			     const QuestionData &question,
			     const std::string &textbook_name,
			     bool correct,
			     bool skipped,
			     bool round_finished,
			     bool round_passed) const;

private:
	bool RecordDailyCompletedWord(sqlite3 *db, const std::string &textbook_name, int word_id) const;

	const char *log_tag_;
	int user_id_ = 0;
};

}  // namespace word_practice
