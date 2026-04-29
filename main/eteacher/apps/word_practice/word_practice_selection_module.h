#pragma once

#include <string>
#include <vector>

#include "eteacher/apps/word_practice/word_practice_types.h"

namespace word_practice {

class SelectionModule {
public:
	std::vector<SelectedWord> SelectWordsFromVocabulary(const WordSelectionConfig &config,
						 int user_id = 0,
						 int stage_index = 1,
						 const std::string &textbook_name = {},
						 int last_new_word_id = 0,
						 int *next_new_word_id = nullptr) const;
};

}  // namespace word_practice