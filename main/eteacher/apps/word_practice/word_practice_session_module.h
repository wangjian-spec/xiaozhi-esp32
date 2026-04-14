#pragma once

#include <cstddef>

namespace word_practice {

class SessionModule {
public:
	void ResetForNewRound(int pass_target_questions);
	void SetCurrentQuestionIndex(size_t index);

	size_t CurrentQuestionIndex() const;
	int CorrectCount() const;
	int WrongCount() const;
	int Score() const;
	int TotalAnswered() const;
	int PassTargetQuestions() const;
	bool AwaitingNextQuestion() const;
	bool IsFinished() const;

	void SetAwaitingNextQuestion(bool awaiting);
	void RecordAnswer(bool correct, int correct_reward = 10, int wrong_penalty = 2);

private:
	size_t current_question_index_ = 0;
	int correct_count_ = 0;
	int wrong_count_ = 0;
	int score_ = 0;
	int total_answered_ = 0;
	int pass_target_questions_ = 10;
	bool awaiting_next_question_ = false;
};

}  // namespace word_practice