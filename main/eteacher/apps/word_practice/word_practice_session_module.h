#pragma once

namespace word_practice {

class SessionModule {
public:
	void ResetForNewRound(int pass_target_questions);

	int CorrectCount() const;
	int WrongCount() const;
	int SkipCount() const;
	int Score() const;
	int TotalAnswered() const;
	int ConsecutiveCorrectAnswers() const;
	int PassTargetQuestions() const;
	bool AwaitingNextQuestion() const;
	bool AnswerLimitReached() const;
	bool ForceFinished() const;
	bool IsFinished() const;

	void SetAwaitingNextQuestion(bool awaiting);
	void FinishNow();
	void RecordAnswer(bool correct, int correct_reward = 10, int wrong_penalty = 2);
	void RecordSkip();

private:
	int correct_count_ = 0;
	int wrong_count_ = 0;
	int skip_count_ = 0;
	int score_ = 0;
	int total_answered_ = 0;
	int consecutive_correct_answers_ = 0;
	int pass_target_questions_ = 10;
	bool awaiting_next_question_ = false;
	bool force_finished_ = false;
};

}  // namespace word_practice
