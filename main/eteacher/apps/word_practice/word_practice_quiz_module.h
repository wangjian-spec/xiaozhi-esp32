#pragma once

#include <string>

#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

class QuizModule {
public:
	ChoiceState Generate(const QuestionData &question) const;
	QuizType ResolveQuizType(int question_type) const;
	bool IsSpeakType(int question_type) const;
	bool IsSentenceBuildType(int question_type) const;

	std::string SelectSceneId(int question_type) const;
	std::string TypeTitle(int question_type) const;
	std::string TypeInstruction(int question_type) const;
	std::string NormalizeAnswerToken(std::string value) const;
	std::string NormalizePairWord(const std::string &value) const;
};

}  // namespace word_practice