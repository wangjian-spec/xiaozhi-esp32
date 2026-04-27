#include "eteacher/apps/word_practice/word_practice_flow_controller.h"

namespace word_practice {

namespace {

constexpr int kDefaultRoundWordTarget = 15;

}  // namespace

PracticeRoundPlan PracticeFlowController::BuildRoundPlan() const {
	PracticeRoundPlan plan;
	plan.selection_config.review_word_count = kDefaultRoundWordTarget;
	plan.selection_config.new_word_count = 0;
	return plan;
}

int PracticeFlowController::MaxSpeakRetryCount() const {
	return 3;
}

bool PracticeFlowController::ShouldAutoFailSpeakQuestion(int failed_attempts) const {
	return failed_attempts >= MaxSpeakRetryCount();
}

int PracticeFlowController::RemainingSpeakRetries(int failed_attempts) const {
	return std::max(0, MaxSpeakRetryCount() - failed_attempts);
}

}  // namespace word_practice