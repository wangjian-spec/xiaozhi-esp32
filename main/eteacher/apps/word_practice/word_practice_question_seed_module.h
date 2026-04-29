#pragma once

#include <unordered_map>
#include <vector>

#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

class QuestionSeedModule {
public:
	bool LoadVocabularySeedForWord(const SelectedWord &selected_word,
				      int stage_index,
				      VocabularySeed *out_seed) const;

	LearningMode DetermineLearningMode(const std::vector<SelectedWord> &selected_words,
					  const std::vector<WordMasteryProfile> &profiles) const;

	std::unordered_map<int, std::vector<int>> BuildAvailableQuestionTypesByWord(
		const std::vector<VocabularySeed> &loaded_seeds,
		const std::vector<WordMasteryProfile> &profiles,
		bool include_speak_questions,
		LearningMode learning_mode) const;

	bool GenerateQuestionOnDemand(const std::vector<VocabularySeed> &loaded_seeds,
				      const std::vector<WordMasteryProfile> &profiles,
				      int word_id,
				      int question_type,
				      bool include_speak_questions,
				      LearningMode learning_mode,
				      QuestionData *out_question) const;
};

}  // namespace word_practice
