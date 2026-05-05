#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <sqlite3.h>

#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

class QuestionSeedModule {
public:
	QuestionSeedModule() = default;
	~QuestionSeedModule();
	QuestionSeedModule(const QuestionSeedModule &) = delete;
	QuestionSeedModule &operator=(const QuestionSeedModule &) = delete;
	QuestionSeedModule(QuestionSeedModule &&) = delete;
	QuestionSeedModule &operator=(QuestionSeedModule &&) = delete;

	bool LoadVocabularySeedForWord(const SelectedWord &selected_word,
				      int stage_index,
				      VocabularySeed *out_seed) const;

	std::vector<VocabularySeed> LoadVocabularySeedsForWords(const std::vector<SelectedWord> &selected_words,
					      int stage_index) const;

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

private:
	bool EnsureCachedDictionaryDb(int stage_index, sqlite3 **out_db) const;
	void CloseCachedDictionaryDb() const;

	mutable sqlite3 *cached_dictionary_db_ = nullptr;
	mutable int cached_dictionary_stage_index_ = 0;
	mutable std::string cached_dictionary_db_path_{};
};

}  // namespace word_practice
