#pragma once

#include <cstdint>
#include <functional>
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
	std::vector<std::string> pair_right;
	std::string expected;
};

struct LearnedSnapshot {
	int correct = 0;
	int wrong = 0;
	int64_t last_seen_at = 0;
};

enum class QuestionSelectionStrategy {
	LegacyAdaptive,
	TypeCycleRandom,
};

enum class QuizType {
	ImageChoice,
	TranslationChoice,
	Match,
	SentenceBuild,
	Speak,
	Unknown,
};

using LearnedSnapshotProvider = std::function<LearnedSnapshot(const QuestionData &, const std::string &)>;

struct SelectionResult {
	bool has_value = false;
	size_t selected_index = 0;
	QuestionSelectionStrategy used_strategy = QuestionSelectionStrategy::TypeCycleRandom;
};

struct WordSelectionConfig {
	int review_word_count = 10;
	int new_word_count = 5;

	int TotalCount() const {
		return review_word_count + new_word_count;
	}
};

struct SelectedWord {
	int word_id = 0;
	std::string word;
	std::string image;
	bool is_review = false;
};

}  // namespace word_practice