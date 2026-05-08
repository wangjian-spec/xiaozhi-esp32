#pragma once

#include <vector>

#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

class PracticeFlowController {
public:
	WordSelectionConfig BuildRoundPlan(int total_word_count, int new_word_target, int review_word_target) const;

	int MaxSpeakRetryCount() const;
	bool ShouldAutoFailSpeakQuestion(int failed_attempts) const;
	int RemainingSpeakRetries(int failed_attempts) const;
};

}  // namespace word_practice