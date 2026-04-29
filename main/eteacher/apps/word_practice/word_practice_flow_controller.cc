#include "eteacher/apps/word_practice/word_practice_flow_controller.h"

#include "eteacher/apps/word_practice/word_practice_config.h"

namespace word_practice {

PracticeRoundPlan PracticeFlowController::BuildRoundPlan(int practice_word_count) const {
	PracticeRoundPlan plan;
	plan.selection_config.total_word_count =
		std::max(1, practice_word_count > 0 ? practice_word_count : config::kDefaultRoundWordTarget);
	return plan;
}

int PracticeFlowController::MaxSpeakRetryCount() const {
	return config::kMaxSpeakRetryCount;
}

bool PracticeFlowController::ShouldAutoFailSpeakQuestion(int failed_attempts) const {
	return failed_attempts >= MaxSpeakRetryCount();
}

int PracticeFlowController::RemainingSpeakRetries(int failed_attempts) const {
	return std::max(0, MaxSpeakRetryCount() - failed_attempts);
}

}  // namespace word_practice