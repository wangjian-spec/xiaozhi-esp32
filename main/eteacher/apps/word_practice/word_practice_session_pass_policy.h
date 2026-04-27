#pragma once

#include <string>

#include "eteacher/apps/word_practice/word_practice_session_module.h"
#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

struct SessionPassContext {
	int correct_count = 0;
	int wrong_count = 0;
	int score = 0;
	int total_answered = 0;
	int pass_target_questions = 0;
	bool answer_limit_reached = false;
	bool force_finished = false;
	BatchProgressSummary batch_summary{};
};

class SessionPassPolicy {
public:
	static SessionPassContext BuildContext(const SessionModule &session,
					      const BatchProgressSummary &batch_summary);

	bool IsPassed(const SessionPassContext &context) const;
	std::string BuildSummaryText(const SessionPassContext &context) const;
};

}  // namespace word_practice
