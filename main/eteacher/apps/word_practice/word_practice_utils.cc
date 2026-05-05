#include "eteacher/apps/word_practice/word_practice_utils.h"

#include <algorithm>
#include <cctype>

#include "eteacher/app_ui/common_ui_utils.h"

namespace word_practice::utils {

std::string Trim(const std::string &value) {
	size_t start = 0;
	while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
		++start;
	}
	size_t end = value.size();
	while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
		--end;
	}
	return value.substr(start, end - start);
}

std::string BasenameFromPath(const std::string &value) {
	const size_t pos = value.find_last_of("\\/");
	return pos == std::string::npos ? value : value.substr(pos + 1);
}

std::string NormalizeAudioEntryName(const std::string &audio_filename) {
	std::string name = Trim(BasenameFromPath(audio_filename));
	if (name.empty()) {
		return {};
	}
	auto lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".mp3") == 0) {
		name.replace(name.size() - 4, 4, ".ogg");
	} else if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".opus") == 0) {
		name.replace(name.size() - 5, 5, ".ogg");
	}
	return name;
}

std::string BuildQuestionAudioPath(const std::string &audio_filename) {
	return eteacher::app_ui::BuildWordsAudioPath(audio_filename);
}

std::string StageNumberToTag(int stage) {
	switch (stage) {
		case 1:
			return "primary";
		case 2:
			return "middle";
		case 3:
			return "high";
		case 4:
			return "cet4";
		case 5:
			return "cet6";
		default:
			return {};
	}
}

std::vector<std::string> SplitHintWords(const std::string &text) {
	std::vector<std::string> words;
	std::string current;
	for (char ch : text) {
		if (ch == '\n' || ch == '\r' || ch == ',' || ch == ';' || ch == '|' || ch == '/' ||
			std::isspace(static_cast<unsigned char>(ch)) != 0) {
			std::string token = Trim(current);
			if (!token.empty()) {
				words.push_back(std::move(token));
			}
			current.clear();
		} else {
			current.push_back(ch);
		}
	}
	std::string token = Trim(current);
	if (!token.empty()) {
		words.push_back(std::move(token));
	}
	return words;
}

std::string NormalizeLettersOnlyLower(const std::string &value) {
	std::string out;
	out.reserve(value.size());
	for (unsigned char ch : value) {
		if (std::isalpha(ch) != 0) {
			out.push_back(static_cast<char>(std::tolower(ch)));
		}
	}
	return out;
}

std::vector<std::string> NormalizeSentenceWordsLower(const std::string &value) {
	std::vector<std::string> words;
	std::string current;
	for (unsigned char ch : value) {
		if (std::isalnum(ch) != 0) {
			current.push_back(static_cast<char>(std::tolower(ch)));
		} else if (!current.empty()) {
			words.push_back(std::move(current));
			current.clear();
		}
	}
	if (!current.empty()) {
		words.push_back(std::move(current));
	}
	return words;
}

}  // namespace word_practice::utils
