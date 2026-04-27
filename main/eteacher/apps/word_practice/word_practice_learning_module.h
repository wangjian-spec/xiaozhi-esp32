#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include <sqlite3.h>

#include "eteacher/apps/word_practice/word_practice_quiz_module.h"
#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

class WordMasteryDao {
public:
	explicit WordMasteryDao(const char *log_tag = "WordPracticeApp", int user_id = 0);
	void SetUserId(int user_id);

	std::string DiscoverUserDbPath() const;
	bool EnsureTables(sqlite3 *db) const;
	WordMasteryProfile LoadProfile(int word_id, const std::string &textbook_name) const;
	std::vector<WordMasteryProfile> LoadProfiles(const std::vector<SelectedWord> &selected_words,
						      const std::string &textbook_name) const;
	int ApplyDueDecayIfNeeded(std::vector<WordMasteryProfile> *profiles) const;
	bool ApplyAttempt(WordMasteryProfile *profile, const QuestionAttemptRecord &attempt) const;
	bool ApplyAttempt(sqlite3 *db, WordMasteryProfile *profile, const QuestionAttemptRecord &attempt) const;
	bool SaveProfile(const WordMasteryProfile &profile) const;
	bool RecordAttempt(const QuestionAttemptRecord &attempt) const;
	bool ApplyDueDecayIfNeeded(WordMasteryProfile *profile) const;

private:
	WordMasteryProfile LoadProfile(sqlite3 *db, int word_id, const std::string &textbook_name) const;
	bool ApplyDueDecayIfNeeded(sqlite3 *db, WordMasteryProfile *profile) const;
	bool SaveProfile(sqlite3 *db, const WordMasteryProfile &profile) const;
	bool RecordAttempt(sqlite3 *db, const QuestionAttemptRecord &attempt) const;

	const char *log_tag_;
	int user_id_ = 0;
};

class LearningBatchPlanner {
public:
	LearningBatch Build(const std::vector<SelectedWord> &selected_words,
			    const std::vector<WordMasteryProfile> &profiles,
			    bool cold_start_mode) const;
};

class BatchProgressTracker {
public:
	void Reset(const LearningBatch &batch);
	void MarkPresented(int word_id);
	void MarkOutcome(int word_id,
			 BatchWordKind kind,
			 TrainingSkill skill,
			 QuestionReasonType reason_type,
			 bool correct);
	WordProgressState ProgressState(int word_id) const;
	bool HasRecognitionCheckpoint(int word_id) const;
	bool HasRecallCheckpoint(int word_id) const;
	bool IsCompleted(int word_id) const;
	bool IsBatchComplete() const;
	BatchProgressSummary BuildSummary() const;

private:
	struct ItemProgress {
		BatchWordKind kind = BatchWordKind::ReviewWord;
		WordProgressState state = WordProgressState::NotStarted;
		bool recognition_done = false;
		bool recall_done = false;
		int shown_count = 0;
		int correct_count = 0;
	};

	std::unordered_map<int, ItemProgress> items_{};
};

class QuestionScheduler {
public:
	QuestionScheduler() = default;

	void Reset();
	ScheduledQuestion ScheduleNext(const std::unordered_map<int, std::vector<int>> &available_question_types,
				       const LearningBatch &batch,
				       const std::vector<WordMasteryProfile> &profiles,
				       const BatchProgressTracker &tracker,
				       bool cold_start_mode,
				       int total_answered,
				       int hard_limit,
				       bool prefer_easy_confirmation);
	void RecordResult(const ScheduledQuestion &scheduled,
			 const WordMasteryProfile &profile,
			 bool correct,
			 bool cold_start_mode,
			 int total_answered,
			 int hard_limit);

private:
	struct MistakeFollowup {
		int word_id = 0;
		TrainingSkill forced_skill = TrainingSkill::Unknown;
	};

	int ExtractWordId(const QuestionData &question) const;
	double QuestionQualityScore(const QuestionData &question) const;
	TrainingSkill ChooseSkill(const BatchWordPlan &plan,
				 const WordMasteryProfile *profile,
				 const BatchProgressTracker &tracker,
				 bool forced_by_mistake_chain,
				 TrainingSkill forced_skill,
				 bool cold_start_mode,
				 bool prefer_easy_confirmation) const;
	ScheduledQuestion BuildScheduledQuestion(int question_type,
					      int word_id,
					      TrainingSkill skill,
					      QuestionReasonType reason_type,
					      const WordMasteryProfile *profile) const;
	ScheduledQuestion FindQuestionForWord(const std::unordered_map<int, std::vector<int>> &available_question_types,
					   int word_id,
					   TrainingSkill skill,
					   QuestionReasonType reason_type,
					   const WordMasteryProfile *profile) const;
	const BatchWordPlan *FindPlan(const LearningBatch &batch, int word_id) const;
	const WordMasteryProfile *FindProfile(const std::vector<WordMasteryProfile> &profiles, int word_id) const;
	int RecentWordRounds(int word_id) const;
	void TouchRecentWord(int word_id);
	void MaybeRotateRecentWord(const LearningBatch &batch);
	bool CanTriggerMistakeChain(int word_id, int total_answered) const;
	TrainingSkill DowngradedSkill(TrainingSkill skill) const;
	void AdvanceSkillQuestionCursor(TrainingSkill skill, int question_type);
	std::vector<int> QuestionTypesForSkill(TrainingSkill skill) const;
	std::string BuildReasonJson(QuestionReasonType reason_type,
				    const WordMasteryProfile *profile,
				    TrainingSkill target_skill) const;

	std::deque<int> recent_words_{};
	std::unordered_map<int, int> recent_rounds_{};
	std::unordered_map<int, int> word_seen_count_{};
	std::unordered_map<int, int> last_question_type_by_word_{};
	std::unordered_map<int, TrainingSkill> last_skill_by_word_{};
	std::unordered_map<int, size_t> skill_question_cursor_{};
	std::unordered_map<int, int> mistake_cooldown_until_{};
	std::deque<MistakeFollowup> mistake_queue_{};
	std::deque<TrainingSkill> recent_skill_history_{};
	int last_presented_word_id_ = 0;
	int consecutive_same_word_count_ = 0;
	int mistake_chain_trigger_count_ = 0;
	int mistake_chain_question_count_ = 0;
};

const char *ToString(TrainingSkill skill);
const char *ToString(QuestionReasonType reason_type);

}  // namespace word_practice
