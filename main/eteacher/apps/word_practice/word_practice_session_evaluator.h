#pragma once

#include <vector>

#include "eteacher/apps/word_practice/word_practice_session_module.h"
#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

struct SessionEvaluationDetail {
	bool pass_words = false;
	bool pass_completion = false;
	bool pass_accuracy = false;
	bool pass_minimum_questions = false;
	bool pass_skill_coverage = false;
	bool finish_by_answer_limit = false;
	bool finish_by_progress_gate = false;
	bool scheduler_exhausted = false;
};

struct SessionEvaluation {
	bool finished = false;
	bool success = false;
	float completion = 0.0f;
	float accuracy = 0.0f;
	int total_answered = 0;
	int correct_answers = 0;
	int wrong_answers = 0;
	int skipped_answers = 0;
	int completed_words = 0;
	int target_words = 0;
	int minimum_questions = 0;
	SessionEvaluationDetail detail{};
};

class SessionEvaluator {
public:
	static SessionEvaluation Evaluate(const SessionModule &session,
					 const LearningBatch &batch,
					 const BatchProgressSummary &progress,
				 const std::vector<WordMasteryProfile> &profiles,
				 bool scheduler_exhausted = false);

	static float ComputeWordCompletion(const WordMasteryProfile &profile);
};

}  // namespace word_practice