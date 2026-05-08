#include "eteacher/apps/word_practice/word_practice_session_module.h"

#include <algorithm>

namespace word_practice {

void SessionModule::ResetForNewRound(int pass_target_questions) {
	correct_count_ = 0;
	wrong_count_ = 0;
	skip_count_ = 0;
	score_ = 0;
	total_answered_ = 0;
	consecutive_correct_answers_ = 0;
	pass_target_questions_ = std::max(1, pass_target_questions);
	awaiting_next_question_ = false;
	force_finished_ = false;
}

int SessionModule::CorrectCount() const {
	return correct_count_;
}

int SessionModule::WrongCount() const {
	return wrong_count_;
}

int SessionModule::SkipCount() const {
	return skip_count_;
}

int SessionModule::Score() const {
	return score_;
}

int SessionModule::TotalAnswered() const {
	return total_answered_;
}

int SessionModule::ConsecutiveCorrectAnswers() const {
	return consecutive_correct_answers_;
}

int SessionModule::PassTargetQuestions() const {
	return pass_target_questions_;
}

bool SessionModule::AwaitingNextQuestion() const {
	return awaiting_next_question_;
}

bool SessionModule::AnswerLimitReached() const {
	return total_answered_ >= pass_target_questions_;
}

bool SessionModule::ForceFinished() const {
	return force_finished_;
}

bool SessionModule::IsFinished() const {
	return force_finished_;
}

void SessionModule::SetAwaitingNextQuestion(bool awaiting) {
	awaiting_next_question_ = awaiting;
}

void SessionModule::FinishNow() {
	force_finished_ = true;
	awaiting_next_question_ = false;
}

void SessionModule::RecordAnswer(bool correct, int correct_reward, int wrong_penalty) {
	if (correct) {
		++correct_count_;
		score_ += correct_reward;
		++consecutive_correct_answers_;
	} else {
		++wrong_count_;
		score_ = std::max(0, score_ - wrong_penalty);
		consecutive_correct_answers_ = 0;
	}
	++total_answered_;
	awaiting_next_question_ = !force_finished_ && total_answered_ < pass_target_questions_;
}

void SessionModule::RecordSkip() {
	++skip_count_;
	++total_answered_;
	consecutive_correct_answers_ = 0;
	awaiting_next_question_ = !force_finished_ && total_answered_ < pass_target_questions_;
}

SessionModule::Snapshot SessionModule::CaptureSnapshot() const {
	Snapshot snapshot;
	snapshot.correct_count = correct_count_;
	snapshot.wrong_count = wrong_count_;
	snapshot.skip_count = skip_count_;
	snapshot.score = score_;
	snapshot.total_answered = total_answered_;
	snapshot.consecutive_correct_answers = consecutive_correct_answers_;
	snapshot.pass_target_questions = pass_target_questions_;
	snapshot.awaiting_next_question = awaiting_next_question_;
	snapshot.force_finished = force_finished_;
	return snapshot;
}

void SessionModule::RestoreSnapshot(const Snapshot &snapshot) {
	correct_count_ = snapshot.correct_count;
	wrong_count_ = snapshot.wrong_count;
	skip_count_ = snapshot.skip_count;
	score_ = snapshot.score;
	total_answered_ = snapshot.total_answered;
	consecutive_correct_answers_ = snapshot.consecutive_correct_answers;
	pass_target_questions_ = snapshot.pass_target_questions;
	awaiting_next_question_ = snapshot.awaiting_next_question;
	force_finished_ = snapshot.force_finished;
}

}  // namespace word_practice
