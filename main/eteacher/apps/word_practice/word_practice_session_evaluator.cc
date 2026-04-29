#include "eteacher/apps/word_practice/word_practice_session_evaluator.h"

#include <algorithm>
#include <cmath>

#include "eteacher/apps/word_practice/word_practice_config.h"

namespace word_practice {
namespace {

float ClampUnit(float value) {
	return std::max(0.0f, std::min(1.0f, value));
}

const WordMasteryProfile *FindProfile(const std::vector<WordMasteryProfile> &profiles, int word_id) {
	for (const auto &profile : profiles) {
		if (profile.word_id == word_id) {
			return &profile;
		}
	}
	return nullptr;
}

}  // namespace

float SessionEvaluator::ComputeWordCompletion(const WordMasteryProfile &profile) {
	const float recognition = ClampUnit(static_cast<float>(profile.strength) / 100.0f);
	const float recall = ClampUnit(static_cast<float>(profile.recall_score) / 5.0f);
	const float output = ClampUnit(static_cast<float>(profile.output_score) / 5.0f);
	return ClampUnit(
		recognition * config::kWordCompletionRecognitionWeight +
		recall * config::kWordCompletionRecallWeight +
		output * config::kWordCompletionOutputWeight);
}

SessionEvaluation SessionEvaluator::Evaluate(const SessionModule &session,
					 const LearningBatch &batch,
					 const BatchProgressSummary &progress,
				 const std::vector<WordMasteryProfile> &profiles,
				 bool scheduler_exhausted) {
	SessionEvaluation evaluation;
	evaluation.total_answered = session.TotalAnswered();
	evaluation.correct_answers = session.CorrectCount();
	evaluation.wrong_answers = session.WrongCount();
	evaluation.skipped_answers = session.SkipCount();
	evaluation.completed_words = std::max(0, progress.completed_items);
	evaluation.target_words = std::max(0, progress.total_items);
	evaluation.detail.finish_by_answer_limit = session.AnswerLimitReached() || session.ForceFinished();
	evaluation.detail.scheduler_exhausted = scheduler_exhausted;

	const int graded_answers = evaluation.correct_answers + evaluation.wrong_answers;
	if (graded_answers > 0) {
		evaluation.accuracy = ClampUnit(static_cast<float>(evaluation.correct_answers) / static_cast<float>(graded_answers));
	}

	if (!batch.items.empty()) {
		float total_completion = 0.0f;
		for (const auto &item : batch.items) {
			const WordMasteryProfile *profile = FindProfile(profiles, item.selected_word.word_id);
			total_completion += profile != nullptr ? ComputeWordCompletion(*profile) : 0.0f;
		}
		evaluation.completion = ClampUnit(total_completion / static_cast<float>(batch.items.size()));
	}

	const int target_words = std::max(0, evaluation.target_words);
	const int required_completed_words = target_words > 0
		? static_cast<int>(std::ceil(static_cast<float>(target_words) * config::kSessionRequiredCompletedWordsRatio))
		: 0;
	evaluation.detail.pass_words = target_words > 0 && evaluation.completed_words >= required_completed_words;
	evaluation.minimum_questions = std::max(
		config::kSessionMinimumQuestionFloor,
		std::min(
			session.PassTargetQuestions(),
			std::max(evaluation.completed_words, std::min(target_words, config::kSessionMinimumQuestionSoftCap))));
	evaluation.detail.pass_minimum_questions = evaluation.total_answered >= evaluation.minimum_questions;
	evaluation.detail.pass_skill_coverage = progress.skill_coverage_ok;
	evaluation.detail.finish_by_progress_gate = evaluation.detail.pass_words &&
		evaluation.detail.pass_minimum_questions &&
		evaluation.detail.pass_skill_coverage;
	evaluation.finished = evaluation.detail.finish_by_answer_limit ||
		evaluation.detail.finish_by_progress_gate ||
		evaluation.detail.scheduler_exhausted;
	evaluation.detail.pass_completion = evaluation.completion >= config::kSessionSuccessCompletionThreshold;
	evaluation.detail.pass_accuracy = evaluation.accuracy >= config::kSessionSuccessAccuracyThreshold;
	evaluation.success = evaluation.finished &&
		evaluation.detail.pass_words &&
		evaluation.detail.pass_skill_coverage &&
		evaluation.detail.pass_completion &&
		evaluation.detail.pass_accuracy;
	return evaluation;
}

}  // namespace word_practice