#include "eteacher/apps/word_practice/word_practice_session_pass_policy.h"

namespace word_practice {

SessionPassContext SessionPassPolicy::BuildContext(const SessionModule &session,
						       const BatchProgressSummary &batch_summary) {
	SessionPassContext context;
	context.correct_count = session.CorrectCount();
	context.wrong_count = session.WrongCount();
	context.score = session.Score();
	context.total_answered = session.TotalAnswered();
	context.pass_target_questions = session.PassTargetQuestions();
	context.answer_limit_reached = session.AnswerLimitReached();
	context.force_finished = session.ForceFinished();
	context.batch_summary = batch_summary;
	return context;
}

bool SessionPassPolicy::IsPassed(const SessionPassContext &context) const {
	if (context.total_answered <= 0) {
		return false;
	}
	return context.batch_summary.batch_completed;
}

std::string SessionPassPolicy::BuildSummaryText(const SessionPassContext &context) const {
	const bool passed = IsPassed(context);
	return
		(passed ? "本轮结算 恭喜过关 " : "本轮结算 未过关 ") +
		(std::string("分数:") + std::to_string(context.score) +
		 " 正确:" + std::to_string(context.correct_count) +
		 " 错误:" + std::to_string(context.wrong_count) +
		 " 批次:" + std::to_string(context.batch_summary.completed_items) + "/" +
		 std::to_string(context.batch_summary.total_items));
}

}  // namespace word_practice
