#include "eteacher/apps/word_practice/word_practice_quiz_module.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

#include <cJSON.h>

namespace word_practice {
namespace {

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

std::string JsonString(cJSON *obj, const char *key) {
	if (!obj || !key) {
		return {};
	}
	cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsString(item) && item->valuestring) {
		return item->valuestring;
	}
	if (cJSON_IsNumber(item)) {
		return std::to_string(item->valuedouble);
	}
	return {};
}

std::string JsonItemString(cJSON *item) {
	if (cJSON_IsString(item) && item->valuestring) {
		return item->valuestring;
	}
	if (cJSON_IsNumber(item)) {
		return std::to_string(item->valuedouble);
	}
	return {};
}

cJSON *JsonObjectItem(cJSON *obj, const char *key) {
	if (!obj || !key) {
		return nullptr;
	}
	return cJSON_GetObjectItemCaseSensitive(obj, key);
}

std::string NestedJsonString(cJSON *obj, const char *parent_key, const char *child_key) {
	cJSON *parent = JsonObjectItem(obj, parent_key);
	if (!cJSON_IsObject(parent)) {
		return {};
	}
	return JsonString(parent, child_key);
}

std::string ExtractWordText(cJSON *obj) {
	if (!obj) {
		return {};
	}

	cJSON *word_item = JsonObjectItem(obj, "word");
	if (cJSON_IsString(word_item) || cJSON_IsNumber(word_item)) {
		return Trim(JsonItemString(word_item));
	}
	if (cJSON_IsObject(word_item)) {
		std::string value = Trim(JsonString(word_item, "word"));
		if (value.empty()) {
			value = Trim(JsonString(word_item, "text"));
		}
		if (value.empty()) {
			value = Trim(JsonString(word_item, "label"));
		}
		return value;
	}

	std::string value = Trim(JsonString(obj, "word_text"));
	if (value.empty()) {
		value = Trim(JsonString(obj, "text"));
	}
	return value;
}

std::string ExtractMeaningZhText(cJSON *obj) {
	if (!obj) {
		return {};
	}

	std::string value = Trim(NestedJsonString(obj, "word_meaning", "meaning_zh"));
	if (value.empty()) {
		value = Trim(JsonString(obj, "meaning_zh"));
	}
	if (value.empty()) {
		value = Trim(JsonString(obj, "meaning"));
	}
	if (value.empty()) {
		value = Trim(NestedJsonString(obj, "word_meaning", "meaning_en"));
	}
	if (value.empty()) {
		value = Trim(JsonString(obj, "meaning_en"));
	}
	return value;
}

std::string ExtractDefaultOptionText(cJSON *obj) {
	if (!obj) {
		return {};
	}

	std::string text = Trim(JsonString(obj, "tex"));
	if (text.empty()) {
		text = Trim(JsonString(obj, "text"));
	}
	if (text.empty()) {
		text = Trim(JsonString(obj, "label"));
	}
	if (text.empty()) {
		text = Trim(JsonString(obj, "value"));
	}
	if (text.empty()) {
		text = ExtractMeaningZhText(obj);
	}
	if (text.empty()) {
		text = ExtractWordText(obj);
	}
	return text;
}

std::string ExtractOptionTextForType(cJSON *obj, int question_type) {
	if (!obj) {
		return {};
	}
	if (cJSON_IsString(obj) || cJSON_IsNumber(obj)) {
		return Trim(JsonItemString(obj));
	}
	if (!cJSON_IsObject(obj)) {
		return {};
	}

	std::string text;
	switch (question_type) {
		case 1:
		case 3:
		case 12:
			text = ExtractMeaningZhText(obj);
			break;
		case 2:
		case 11:
			text = ExtractWordText(obj);
			break;
		default:
			break;
	}

	if (text.empty()) {
		text = ExtractDefaultOptionText(obj);
	}
	return text;
}

std::string ExtractPairLeftText(cJSON *obj) {
	std::string value = ExtractWordText(obj);
	if (value.empty()) {
		value = ExtractDefaultOptionText(obj);
	}
	return value;
}

std::string NormalizeComparableText(const std::string &value) {
	std::string text = Trim(value);
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return text;
}

bool IsChoiceAnswerType(int question_type) {
	switch (question_type) {
		case 1:
		case 2:
		case 3:
		case 11:
		case 12:
			return true;
		default:
			return false;
	}
}

std::vector<std::string> SplitHintWords(const std::string &text) {
	std::vector<std::string> words;
	std::string current;
	for (char ch : text) {
		if (ch == ',' || ch == ';' || ch == '|' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
			std::string token = Trim(current);
			if (!token.empty()) {
				words.push_back(std::move(token));
			}
			current.clear();
			continue;
		}
		current.push_back(ch);
	}
	std::string token = Trim(current);
	if (!token.empty()) {
		words.push_back(std::move(token));
	}
	return words;
}

}  // namespace

ChoiceState QuizModule::Generate(const QuestionData &question) const {
	ChoiceState state;
	state.prompt = "请作答";
	state.option_keys = {"A", "B", "C", "D"};
	state.options = {"A", "B", "C", "D"};
	state.option_images = {"", "", "", ""};
	state.source_word_id = 0;
	state.expected = question.answer;
	state.textbook_name = question.stage.empty() ? "default" : question.stage;

	if (question.content_json.empty()) {
		return state;
	}

	cJSON *root = cJSON_Parse(question.content_json.c_str());
	if (!root) {
		return state;
	}

	std::string prompt = JsonString(root, "question");
	if (prompt.empty()) {
		prompt = JsonString(root, "prompt");
	}
	if (prompt.empty()) {
		prompt = JsonString(root, "text");
	}
	if (!prompt.empty()) {
		state.prompt = prompt;
	}

	const std::string word_text = ExtractWordText(root);
	const std::string meaning_zh_text = ExtractMeaningZhText(root);
	state.source_word = word_text;
	const std::string word_id_text = Trim(JsonString(root, "word_id"));
	if (!word_id_text.empty()) {
		state.source_word_id = std::atoi(word_id_text.c_str());
	}
	switch (question.type) {
		case 1:
		case 3:
			if (!word_text.empty()) {
				state.prompt = word_text;
			}
			break;
		case 2:
			if (!meaning_zh_text.empty()) {
				state.prompt = meaning_zh_text;
			} else if (!word_text.empty()) {
				state.prompt = word_text;
			}
			break;
		case 11:
		case 12:
			state.prompt.clear();
			break;
		default:
			break;
	}

	std::string textbook = JsonString(root, "textbook");
	if (!textbook.empty()) {
		state.textbook_name = textbook;
	}

	std::string audio_file = Trim(JsonString(root, "audio"));
	if (!audio_file.empty()) {
		state.audio_filename = audio_file;
	}

	cJSON *options_obj = cJSON_GetObjectItemCaseSensitive(root, "options");
	if (!options_obj) {
		options_obj = cJSON_GetObjectItemCaseSensitive(root, "option");
	}
	if (cJSON_IsObject(options_obj)) {
		std::array<const char *, 4> keys = {"A", "B", "C", "D"};
		for (size_t index = 0; index < keys.size(); ++index) {
			cJSON *entry = cJSON_GetObjectItemCaseSensitive(options_obj, keys[index]);
			if (cJSON_IsString(entry) && entry->valuestring) {
				state.options[index] = Trim(entry->valuestring);
				continue;
			}
			if (cJSON_IsObject(entry)) {
				std::string key = Trim(JsonString(entry, "key"));
				if (!key.empty()) {
					state.option_keys[index] = key;
				}

				std::string image = Trim(JsonString(entry, "image"));
				if (!image.empty()) {
					state.option_images[index] = image;
				}

				std::string text = ExtractOptionTextForType(entry, question.type);
				if (!text.empty()) {
					state.options[index] = text;
				}
				continue;
			}

			std::string value = JsonString(options_obj, keys[index]);
			if (!value.empty()) {
				state.options[index] = Trim(value);
			}
		}
	} else if (cJSON_IsArray(options_obj)) {
		for (int index = 0; index < cJSON_GetArraySize(options_obj) && index < 4; ++index) {
			cJSON *item = cJSON_GetArrayItem(options_obj, index);
			if (cJSON_IsString(item) && item->valuestring) {
				state.options[static_cast<size_t>(index)] = Trim(item->valuestring);
			} else if (cJSON_IsObject(item)) {
				std::string key = Trim(JsonString(item, "key"));
				size_t dst_index = static_cast<size_t>(index);
				if (!key.empty()) {
					const std::string token = NormalizeAnswerToken(key);
					if (token.size() == 1 && token[0] >= 'A' && token[0] <= 'D') {
						dst_index = static_cast<size_t>(token[0] - 'A');
					}
				}
				if (!key.empty()) {
					state.option_keys[dst_index] = key;
				}

				std::string image = Trim(JsonString(item, "image"));
				if (!image.empty()) {
					state.option_images[dst_index] = image;
				}

				std::string text = ExtractOptionTextForType(item, question.type);
				if (!text.empty()) {
					state.options[dst_index] = text;
				}
			}
		}
	}

	cJSON *left_obj = cJSON_GetObjectItemCaseSensitive(root, "left");
	if (cJSON_IsArray(left_obj)) {
		for (int index = 0; index < cJSON_GetArraySize(left_obj) && index < 4; ++index) {
			cJSON *item = cJSON_GetArrayItem(left_obj, index);
			if (cJSON_IsString(item) && item->valuestring) {
				state.pair_left.push_back(item->valuestring);
				state.pair_left_audio.emplace_back();
			} else if (cJSON_IsObject(item)) {
				std::string text = ExtractPairLeftText(item);
				if (!text.empty()) {
					state.pair_left.push_back(std::move(text));
					state.pair_left_audio.push_back(Trim(JsonString(item, "audio")));
				}
			}
		}
	}

	cJSON *right_obj = cJSON_GetObjectItemCaseSensitive(root, "right");
	if (cJSON_IsArray(right_obj)) {
		for (int index = 0; index < cJSON_GetArraySize(right_obj) && index < 4; ++index) {
			cJSON *item = cJSON_GetArrayItem(right_obj, index);
			if (cJSON_IsString(item) && item->valuestring) {
				state.pair_right.push_back(item->valuestring);
			} else if (cJSON_IsObject(item)) {
				std::string text = ExtractMeaningZhText(item);
				if (text.empty()) {
					text = ExtractDefaultOptionText(item);
				}
				if (!text.empty()) {
					state.pair_right.push_back(std::move(text));
				}
			}
		}
	}

	cJSON *hints_obj = cJSON_GetObjectItemCaseSensitive(root, "hints");
	if (cJSON_IsArray(hints_obj)) {
		for (int index = 0; index < cJSON_GetArraySize(hints_obj); ++index) {
			cJSON *item = cJSON_GetArrayItem(hints_obj, index);
			if (cJSON_IsString(item) && item->valuestring) {
				std::string token = Trim(item->valuestring);
				if (!token.empty()) {
					state.hints.push_back(std::move(token));
				}
			}
		}
	} else if (cJSON_IsString(hints_obj) && hints_obj->valuestring) {
		state.hints = SplitHintWords(hints_obj->valuestring);
	}
	if (state.hints.empty() && question.type != 5 && question.type != 6) {
		state.hints = state.options;
	}

	if (IsChoiceAnswerType(question.type)) {
		const std::string normalized_expected = NormalizeAnswerToken(state.expected);
		if (!(normalized_expected.size() == 1 && normalized_expected[0] >= 'A' && normalized_expected[0] <= 'D')) {
			const std::string comparable_expected = NormalizeComparableText(state.expected);
			for (size_t index = 0; index < state.options.size(); ++index) {
				if (NormalizeComparableText(state.options[index]) == comparable_expected) {
					state.expected = std::string(1, static_cast<char>('A' + static_cast<int>(index)));
					break;
				}
			}
		}
	}

	cJSON_Delete(root);
	return state;
}

QuizType QuizModule::ResolveQuizType(int question_type) const {
	switch (question_type) {
		case 1:
			return QuizType::ImageChoice;
		case 2:
		case 3:
		case 11:
		case 12:
			return QuizType::TranslationChoice;
		case 4:
			return QuizType::Match;
		case 5:
		case 6:
			return QuizType::SentenceBuild;
		case 7:
		case 8:
		case 9:
		case 10:
			return QuizType::Speak;
		default:
			return QuizType::Unknown;
	}
}

bool QuizModule::IsSpeakType(int question_type) const {
	return ResolveQuizType(question_type) == QuizType::Speak;
}

bool QuizModule::IsSentenceBuildType(int question_type) const {
	return ResolveQuizType(question_type) == QuizType::SentenceBuild;
}

std::string QuizModule::SelectSceneId(int question_type) const {
	if (question_type == 1) {
		return "page_3b66";
	}
	if (question_type == 2 || question_type == 3 || question_type == 11 || question_type == 12) {
		return "page_0b9a";
	}
	if (question_type == 4) {
		return "page_1faf";
	}
	if (question_type == 5 || question_type == 6) {
		return "page_d55e";
	}
	return "page_5d74";
}

std::string QuizModule::TypeTitle(int question_type) const {
	switch (question_type) {
		case 1:
			return "选择对应的图片";
		case 2:
			return "选择英文翻译";
		case 3:
			return "选择中文翻译";
		case 4:
			return "单词配对";
		case 5:
			return "翻译成英文";
		case 6:
			return "输入听到的句子";
		case 7:
			return "朗读单词";
		case 8:
			return "翻译并朗读单词";
		case 9:
			return "朗读句子";
		case 10:
			return "翻译并朗读句子";
		case 11:
			return "选择听到的单词";
		case 12:
			return "翻译听到的单词";
		default:
			return "题型未知";
	}
}

std::string QuizModule::TypeInstruction(int question_type) const {
	switch (question_type) {
		case 1:
				return "请选择对应的图片(A/B/C)";
		case 2:
			return "请选择正确英文翻译(A/B/C/D)";
		case 3:
			return "请选择正确中文翻译(A/B/C/D)";
		case 4:
			return "请选择正确配对(A/B/C/D)";
		case 5:
			return "Start播放音频，C选词，B删除，D确认英文";
		case 6:
			return "Start播放音频，C选词，B删除，D确认句子";
		case 7:
			return "按住Start录音，朗读单词，D跳过";
		case 8:
			return "按住Start录音，翻译并朗读单词，D跳过";
		case 9:
			return "按住Start录音，朗读句子，D跳过";
		case 10:
			return "按住Start录音，翻译并朗读句子，D跳过";
		case 11:
			return "按Start重听，选择听到的单词(A/B/C/D)";
		case 12:
			return "按Start重听，选择听到单词的翻译(A/B/C/D)";
		default:
			return "按键作答";
	}
}

std::string QuizModule::NormalizeAnswerToken(std::string value) const {
	value = Trim(value);
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		return static_cast<char>(std::toupper(ch));
	});
	if (value.empty()) {
		return value;
	}
	if (value.size() == 1 && value[0] >= 'A' && value[0] <= 'D') {
		return value;
	}
	if (value.rfind("OPTION_", 0) == 0 && value.size() >= 8) {
		return std::string(1, value[7]);
	}
	if (value == "UP") return "A";
	if (value == "LEFT") return "B";
	if (value == "DOWN") return "C";
	if (value == "RIGHT") return "D";
	return value;
}

std::string QuizModule::NormalizePairWord(const std::string &value) const {
	std::string text = Trim(value);
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return text;
}

}  // namespace word_practice