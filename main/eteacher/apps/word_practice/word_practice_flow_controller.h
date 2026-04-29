#pragma once

#include <vector>

#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

struct PracticeRoundPlan {
	WordSelectionConfig selection_config{};
};

class PracticeFlowController {
public:
	PracticeRoundPlan BuildRoundPlan(int practice_word_count) const;

	int MaxSpeakRetryCount() const;
	bool ShouldAutoFailSpeakQuestion(int failed_attempts) const;
	int RemainingSpeakRetries(int failed_attempts) const;
};

}  // namespace word_practice