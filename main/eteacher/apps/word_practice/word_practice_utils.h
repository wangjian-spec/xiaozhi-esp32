#pragma once

#include <string>
#include <vector>

namespace word_practice::utils {

std::string Trim(const std::string &value);
std::string BasenameFromPath(const std::string &value);
std::string NormalizeAudioEntryName(const std::string &audio_filename);
std::string BuildQuestionAudioPath(const std::string &audio_filename);
std::string StageNumberToTag(int stage);
std::vector<std::string> SplitHintWords(const std::string &text);
std::string NormalizeLettersOnlyLower(const std::string &value);
std::vector<std::string> NormalizeSentenceWordsLower(const std::string &value);

}  // namespace word_practice::utils
