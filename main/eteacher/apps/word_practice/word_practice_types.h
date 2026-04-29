#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace word_practice {

struct QuestionData {
	int id = 0;
	int type = 1;
	std::string stage;
	int difficulty = 1;
	std::string content_json;
	std::string answer;
};

struct ChoiceState {
	std::string prompt;
	std::string textbook_name;
	std::string audio_filename;
	std::string source_word;
	int source_word_id = 0;
	std::vector<std::string> option_keys;
	std::vector<std::string> options;
	std::vector<std::string> option_images;
	std::vector<std::string> hints;
	std::vector<std::string> pair_left;
	std::vector<std::string> pair_left_audio;
	std::vector<std::string> pair_right;
	std::string expected;
};

enum class TrainingSkill {
	Recognition,
	Recall,
	Output,
	AdvancedSpeak,
	Unknown,
};

enum class BatchWordKind {
	NewWord,
	ReviewWord,
	WeakWord,
};

enum class LearningMode {
	ColdStart,
	Normal,
	IntensiveReview,
};

enum class WordProgressState {
	NotStarted,
	InProgress,
	Completed,
};

enum class QuestionReasonType {
	NewWord,
	ReviewDue,
	MistakeFollowup,
	WeakReinforce,
	BatchTarget,
	Unknown,
};

enum class QuizType {
	ImageChoice,
	TranslationChoice,
	Match,
	SentenceBuild,
	Speak,
	Unknown,
};
struct WordSelectionConfig {
	int total_word_count = 0;
};

struct SelectedWord {
	int word_id = 0;
	std::string word;
	std::string image;
	bool is_review = false;
};

struct VocabularySeed {
	int word_id = 0;
	int meaning_id = 0;
	int example_id = 0;
	int meaning_count = 0;
	int meaning_with_example_count = 0;
	int word_example_count = 0;
	int selected_meaning_example_count = 0;
	std::string word;
	std::string meaning_zh;
	std::string meaning_en;
	std::string image;
	std::string example_en;
	std::string example_zh;
	std::string selection_zh;
	std::string selection_en;
	int stage = 1;
	bool is_review = false;
};

struct WordMasteryProfile {
	int user_id = 0;
	int word_id = 0;
	std::string textbook_name;
	int stage = 0;
	int strength = 0;
	int recall_score = 0;
	int output_score = 0;
	int64_t next_review_at = 0;
	int lapse_count = 0;
	int64_t last_practiced_at = 0;
	int64_t last_decay_at = 0;
	int64_t last_reviewed_at = 0;
	int last_response_time_ms = 0;
	int persistent_boost = 0;
	bool mastered = false;
};

struct BatchWordPlan {
	SelectedWord selected_word;
	BatchWordKind kind = BatchWordKind::ReviewWord;
	WordProgressState progress_state = WordProgressState::NotStarted;
	bool recognition_done = false;
	bool recall_done = false;
	int shown_count = 0;
	int correct_count = 0;
};

struct CompletionRule {
	int required_shown = 0;
	int required_any_correct = 0;
	int required_recognition = 0;
	int required_recall = 0;
	int required_output = 0;
};

struct SkillCoverage {
	bool recognition_done = false;
	bool recall_done = false;
	bool output_attempted = false;
};

struct LearningBatch {
	std::vector<BatchWordPlan> items;
	int planned_new_words = 0;
	int planned_review_words = 0;
	int planned_weak_words = 0;
};

struct ScheduledQuestion {
	bool has_value = false;
	int word_id = 0;
	int question_type = 0;
	TrainingSkill target_skill = TrainingSkill::Unknown;
	QuestionReasonType reason_type = QuestionReasonType::Unknown;
	std::string reason_text;
};

struct CurrentQuestionSlot {
	bool has_value = false;
	QuestionData current{};
};

struct BatchProgressSummary {
	int total_items = 0;
	int completed_items = 0;
	int new_total = 0;
	int new_completed = 0;
	int review_total = 0;
	int review_completed = 0;
	int weak_total = 0;
	int weak_completed = 0;
	SkillCoverage skill_coverage{};
	bool recognition_coverage_ok = false;
	bool recall_coverage_ok = false;
	bool output_coverage_ok = false;
	bool skill_coverage_ok = false;
	bool batch_completed = false;
};

struct QuestionAttemptRecord {
	int word_id = 0;
	int question_type = 0;
	TrainingSkill target_skill = TrainingSkill::Unknown;
	QuestionReasonType reason_type = QuestionReasonType::Unknown;
	std::string textbook_name;
	std::string question_reason;
	bool correct = false;
	int response_time_ms = 0;
	int64_t practiced_at = 0;
};

}  // namespace word_practice