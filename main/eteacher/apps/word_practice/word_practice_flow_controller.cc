#include "eteacher/apps/word_practice/word_practice_flow_controller.h"

#include "eteacher/apps/word_practice/word_practice_config.h"

namespace word_practice {

WordSelectionConfig PracticeFlowController::BuildRoundPlan(int total_word_count,
								 int new_word_target,
								 int review_word_target) const {
	WordSelectionConfig selection_config;
	selection_config.total_word_count =
		std::max(1, total_word_count > 0 ? total_word_count : config::kDefaultRoundWordTarget);
	selection_config.new_word_target = std::max(0, new_word_target);
	selection_config.review_word_target = std::max(0, review_word_target);
	return selection_config;
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