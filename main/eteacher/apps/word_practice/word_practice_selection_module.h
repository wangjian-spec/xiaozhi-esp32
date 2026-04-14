#pragma once

#include <vector>

#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

class SelectionModule {
public:
	void LoadQuestionPool(std::vector<QuestionData> pool);
	void ResetProgress();
	std::vector<SelectedWord> SelectWordsFromVocabulary(const WordSelectionConfig &config,
							 int user_id = 0) const;

	const std::vector<QuestionData> &QuestionPool() const;
	const QuestionData *GetQuestion(size_t index) const;
	bool Empty() const;

	SelectionResult SelectNext(int total_answered,
						   int current_level,
						   QuestionSelectionStrategy strategy,
						   const LearnedSnapshotProvider &provider);

private:
	SelectionResult SelectByLegacyAdaptive(int total_answered,
						      int current_level,
						      const LearnedSnapshotProvider &provider) const;
	SelectionResult SelectByTypeCycleRandom() const;
	void RecordSelectedType(int question_type);

	std::vector<QuestionData> question_pool_{};
	std::vector<int> recent_types_{};
	int next_question_type_cursor_ = 1;
};

}  // namespace word_practice