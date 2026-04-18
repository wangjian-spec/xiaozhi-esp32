#include "eteacher/apps/word_practice/word_practice.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <memory>
#include <random>
#include <sys/stat.h>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <SD.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/apps/word_practice/word_practice_ui.h"
#include "eteacher/app_ui/common_ui_utils.h"
#include "eteacher/database_manager/database_debug.h"
#include "eteacher/app_service/app_service.h"
#include "eteacher/database_manager/sqlite_db_api.h"

#define WP_APP_DBLOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define WP_DIAG_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define WP_ASSET_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define WP_ASSET_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#define ESP_LOGE DB_LOGE
#define ESP_LOGW DB_LOGW
#define ESP_LOGI DB_LOGI
#define ESP_LOGD DB_LOGD

namespace {
constexpr const char *kTag = "WordPracticeApp";
constexpr const app_ui::desc::UiDesc *kUiDesc = &app_ui::generated::word_practice::kUi;

constexpr int kDefaultUserId = 0;

constexpr uint32_t kWidgetPublicTeacher = 0xB1B2CF12u;
constexpr uint32_t kWidgetPublicCup = 0x1F95DB60u;
constexpr uint32_t kWidgetPublicCorrect = 0x7A89E02Au;
constexpr uint32_t kWidgetPublicWrong = 0x845D7D71u;
constexpr uint32_t kWidgetPublicSpeaker = 0x12756A55u;
constexpr uint32_t kWidgetSpeakMic = 0xA4144FDCu;
constexpr uint32_t kWidgetSpeaker1 = 0xA7C4F81Du;
constexpr uint32_t kWidgetSpeaker2 = 0xA4C4F364u;
constexpr uint32_t kWidgetSpeaker3 = 0xA5C4F4F7u;
constexpr uint32_t kWidgetSpeaker4 = 0xA2C4F03Eu;
constexpr uint32_t kWidgetImageWrite = 0xDD2976A2u;
constexpr uint32_t kWidgetImageInput = 0xE37DF08Du;

constexpr uint32_t kWidgetLabelA = 0xA1804BC5u;
constexpr uint32_t kWidgetLabelB = 0x9E80470Cu;
constexpr uint32_t kWidgetLabelC = 0x9F80489Fu;
constexpr uint32_t kWidgetLabelD = 0x9C8043E6u;
constexpr uint32_t kWidgetLabelA1 = 0x30F7911Cu;
constexpr uint32_t kWidgetLabelB1 = 0xC0F02507u;
constexpr uint32_t kWidgetLabelC1 = 0xC4F269EAu;
constexpr uint32_t kWidgetLabelD1 = 0x34EACB75u;
constexpr uint32_t kWidgetImageA = 0x682A05DAu;
constexpr uint32_t kWidgetImageB = 0x672A0447u;
constexpr uint32_t kWidgetImageC = 0x662A02B4u;
constexpr uint32_t kWidgetImageD = 0x652A0121u;
constexpr uint32_t kWidgetLabelA2 = 0x33F795D5u;
constexpr uint32_t kWidgetLabelB2 = 0xC1F0269Au;
constexpr uint32_t kWidgetLabelC2 = 0xC3F26857u;
constexpr uint32_t kWidgetLabelD2 = 0x31EAC6BCu;

constexpr uint32_t kWidgetLabelUp = 0xFA65017Bu;
constexpr uint32_t kWidgetLabelLeft = 0x3A32AEE3u;
constexpr uint32_t kWidgetLabelDown = 0x102F8D2Eu;
constexpr uint32_t kWidgetLabelRight = 0xB4F4F36Cu;

constexpr uint32_t kWidgetLabelQuestionType = 0x7BDF1BB3u;
constexpr uint32_t kWidgetLabelCorrectCount = 0x88D31A8Bu;
constexpr uint32_t kWidgetLabelWrongCount = 0x85C7B0CAu;
constexpr uint32_t kWidgetImageGood = 0xCB906C69u;
constexpr uint32_t kWidgetImageBad = 0xBD6C3E3Fu;
constexpr uint32_t kWidgetLabelAlert = 0x2C695914u;
constexpr uint32_t kWidgetLabelQuestion = 0x5B4BF056u;
constexpr uint32_t kWidgetLabelAsrResult = 0xBB35B429u;
constexpr uint32_t kWidgetLabelPressARead = 0x24F8BC60u;
constexpr uint32_t kWidgetLabelPressDSkip = 0x8138F428u;
constexpr uint32_t kWidgetBottomBar = 0x80DAA8ADu;
constexpr uint32_t kWidgetTextAreaInputAnswer = 0x9210078Au;
constexpr uint32_t kWidgetDialogSelectBoard = 0x3175DFAAu;
constexpr char kAudioBundleMagic[] = {'O', 'G', 'G', 'B', 'I', 'N', '1', '\0'};
constexpr int kType4MeaningMaxWidth = 110;
constexpr size_t kImageChoiceOptionCount = 3;

void LogResolvedDbAccess(const char *scope, const std::string &path, const char *method) {
	(void)scope;
	(void)path;
	(void)method;
}

void LogResolvedAssetAccess(const char *kind,
				 const char *action,
				 const std::string &request,
				 const std::string &path,
				 const char *method,
				 const char *detail = nullptr) {
	(void)kind;
	(void)action;
	(void)request;
	(void)path;
	(void)method;
	(void)detail;
}

bool IsClickLike(const ButtonEvent &event) {
	return event.action == ButtonAction::Click;
}

bool BufferContainsToken(const std::string &data, const char *token, size_t token_length) {
	if (token == nullptr || token_length == 0 || data.size() < token_length) {
		return false;
	}
	for (size_t index = 0; index + token_length <= data.size(); ++index) {
		if (std::memcmp(data.data() + index, token, token_length) == 0) {
			return true;
		}
	}
	return false;
}

std::string BuildAudioPreviewHex(const std::string &data, size_t max_bytes) {
	constexpr char kHexDigits[] = "0123456789ABCDEF";
	const size_t preview_size = std::min(max_bytes, data.size());
	std::string preview;
	preview.reserve(preview_size * 3);
	for (size_t index = 0; index < preview_size; ++index) {
		const unsigned char value = static_cast<unsigned char>(data[index]);
		if (!preview.empty()) {
			preview.push_back(' ');
		}
		preview.push_back(kHexDigits[(value >> 4) & 0x0F]);
		preview.push_back(kHexDigits[value & 0x0F]);
	}
	return preview;
}

bool IsNextQuestionTriggerButton(AppButton button) {
	return button == AppButton::Up || button == AppButton::Down || button == AppButton::Left ||
		   button == AppButton::Right || button == AppButton::A || button == AppButton::B ||
		   button == AppButton::C || button == AppButton::D;
}

std::string Trim(const std::string &value);

std::string BasenameFromPath(const std::string &value) {
	const size_t pos = value.find_last_of("\\/");
	return pos == std::string::npos ? value : value.substr(pos + 1);
}

std::string SanitizeStemPart(const std::string &value) {
	std::string sanitized;
	sanitized.reserve(value.size());
	for (unsigned char ch : value) {
		if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' ||
			ch == '-') {
			sanitized.push_back(static_cast<char>(ch));
		} else {
			sanitized.push_back('_');
		}
	}
	while (!sanitized.empty() && sanitized.front() == '_') {
		sanitized.erase(sanitized.begin());
	}
	while (!sanitized.empty() && sanitized.back() == '_') {
		sanitized.pop_back();
	}
	return sanitized.empty() ? "record" : sanitized;
}

std::string BuildStage1AssetBaseName(int word_id, const std::string &word_text) {
	if (word_id <= 0) {
		return {};
	}
	return std::to_string(word_id) + "_" + SanitizeStemPart(Trim(word_text));
}

std::string BuildStage1ExampleAudioBaseName(int word_id, const std::string &word_text, int example_id) {
	if (word_id <= 0 || example_id <= 0) {
		return {};
	}
	return std::to_string(word_id) + "_" + SanitizeStemPart(Trim(word_text)) + "_" + std::to_string(example_id);
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

bool LooksLikeAssetReference(const std::string &value, const char *expected_extension) {
	const std::string trimmed = Trim(value);
	if (trimmed.empty()) {
		return false;
	}
	if (trimmed.find('/') != std::string::npos || trimmed.find('\\') != std::string::npos) {
		return true;
	}
	auto lower = trimmed;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	if (expected_extension != nullptr) {
		const std::string ext = expected_extension;
		if (lower.size() >= ext.size() && lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0) {
			return true;
		}
	}
	if (!trimmed.empty() && std::isdigit(static_cast<unsigned char>(trimmed.front())) != 0 &&
		trimmed.find('_') != std::string::npos) {
		return true;
	}
	return false;
}

std::string NormalizeImageEntryName(const std::string &image_name) {
	std::string name = Trim(BasenameFromPath(image_name));
	if (name.empty()) {
		return {};
	}
	auto lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	const size_t ext_pos = lower.find_last_of('.');
	if (ext_pos == std::string::npos) {
		name += ".bin";
	} else if (lower.compare(ext_pos, std::string::npos, ".bin") != 0) {
		name.replace(ext_pos, std::string::npos, ".bin");
	}
	return name;
}

std::string ResolveStage1AudioName(const std::string &audio_filename, int word_id, const std::string &word_text) {
	std::string name;
	if (LooksLikeAssetReference(audio_filename, ".ogg") || LooksLikeAssetReference(audio_filename, ".mp3") ||
		LooksLikeAssetReference(audio_filename, ".opus")) {
		name = NormalizeAudioEntryName(audio_filename);
	}
	if (!name.empty()) {
		return name;
	}
	const std::string base_name = BuildStage1AssetBaseName(word_id, word_text);
	return base_name.empty() ? std::string() : (base_name + ".ogg");
}

std::string ResolveStage1ExampleAudioName(int word_id, const std::string &word_text, int example_id) {
	const std::string base_name = BuildStage1ExampleAudioBaseName(word_id, word_text, example_id);
	return base_name.empty() ? std::string() : (base_name + ".ogg");
}

std::string ResolveStage1ImageName(const std::string &image_name, int word_id, const std::string &word_text) {
	const std::string base_name = BuildStage1AssetBaseName(word_id, word_text);
	return base_name.empty() ? std::string() : (base_name + ".bin");
}

std::string BuildQuestionAudioPath(const std::string &audio_filename) {
	return eteacher::app_ui::BuildWordsAudioPath(audio_filename);
}

std::string BuildExampleQuestionAudioPath(const std::string &audio_filename) {
	return eteacher::app_ui::BuildExampleAudioPath(audio_filename);
}

bool IsExampleAudioProxyPath(const std::string &audio_path) {
	return audio_path.rfind(eteacher::app_ui::GetExampleAudioDir(), 0) == 0 ||
		   audio_path.rfind("/sdcard/resource/audio/example/", 0) == 0;
}

const char *ResolveAudioBundlePath(const std::string &audio_path) {
	return IsExampleAudioProxyPath(audio_path) ? eteacher::app_ui::GetExampleAudioBundlePath() :
									  eteacher::app_ui::GetWordsAudioBundlePath();
}

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

[[maybe_unused]] std::string JsonString(cJSON *obj, const char *key) {
	if (!obj || !key) {
		return {};
	}
	cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
	if (cJSON_IsString(item) && item->valuestring) {
		return item->valuestring;
	}
	if (cJSON_IsNumber(item)) {
		return std::to_string(item->valueint);
	}
	return {};
}

std::string FormatOptionWithKey(const std::string &key, const std::string &option_text) {
	const std::string key_text = Trim(key);
	const std::string text = Trim(option_text);
	if (key_text.empty()) {
		return text;
	}
	if (text.empty()) {
		return key_text;
	}
	return key_text + "  " + text;
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

bool DecodeNextUtf8Codepoint(const std::string &text, size_t &index, uint32_t &codepoint) {
	if (index >= text.size()) {
		return false;
	}

	const unsigned char first = static_cast<unsigned char>(text[index]);
	if ((first & 0x80u) == 0) {
		codepoint = first;
		++index;
		return true;
	}

	int length = 0;
	uint32_t cp = 0;
	if ((first & 0xE0u) == 0xC0u) {
		length = 2;
		cp = first & 0x1Fu;
	} else if ((first & 0xF0u) == 0xE0u) {
		length = 3;
		cp = first & 0x0Fu;
	} else if ((first & 0xF8u) == 0xF0u) {
		length = 4;
		cp = first & 0x07u;
	} else {
		++index;
		return false;
	}

	if (index + static_cast<size_t>(length) > text.size()) {
		index = text.size();
		return false;
	}

	for (int i = 1; i < length; ++i) {
		const unsigned char cont = static_cast<unsigned char>(text[index + static_cast<size_t>(i)]);
		if ((cont & 0xC0u) != 0x80u) {
			++index;
			return false;
		}
		cp = (cp << 6) | static_cast<uint32_t>(cont & 0x3Fu);
	}

	index += static_cast<size_t>(length);
	codepoint = cp;
	return true;
}

bool IsHanCodepoint(uint32_t cp) {
	return (cp >= 0x3400u && cp <= 0x4DBFu) || (cp >= 0x4E00u && cp <= 0x9FFFu) ||
		   (cp >= 0xF900u && cp <= 0xFAFFu) || (cp >= 0x20000u && cp <= 0x2A6DFu) ||
		   (cp >= 0x2A700u && cp <= 0x2B73Fu) || (cp >= 0x2B740u && cp <= 0x2B81Fu) ||
		   (cp >= 0x2B820u && cp <= 0x2CEAFu) || (cp >= 0x2CEB0u && cp <= 0x2EBEFu);
}

std::string NormalizeType56ForCompare(const std::string &value) {
	std::string out;
	out.reserve(value.size());

	for (size_t i = 0; i < value.size();) {
		const unsigned char ch = static_cast<unsigned char>(value[i]);
		if ((ch & 0x80u) == 0) {
			if (std::isalpha(ch) != 0) {
				out.push_back(static_cast<char>(std::tolower(ch)));
			}
			++i;
			continue;
		}

		size_t utf8_start = i;
		uint32_t cp = 0;
		if (!DecodeNextUtf8Codepoint(value, i, cp)) {
			continue;
		}
		if (IsHanCodepoint(cp)) {
			out.append(value, utf8_start, i - utf8_start);
		}
	}

	return out;
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

std::vector<std::string> ExtractSentenceDisplayTokens(const std::string &value) {
	std::vector<std::string> words;
	std::string current;
	for (unsigned char ch : value) {
		if (std::isalnum(ch) != 0) {
			current.push_back(static_cast<char>(ch));
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

void ShuffleStringVector(std::vector<std::string> *items) {
	if (items == nullptr || items->size() < 2) {
		return;
	}
	for (size_t index = items->size(); index > 1; --index) {
		const size_t swap_index = static_cast<size_t>(esp_random() % index);
		std::swap((*items)[index - 1], (*items)[swap_index]);
	}
}

bool PreservesAnswerTokenOrder(const std::vector<std::string> &items, const std::vector<std::string> &answer_tokens) {
	if (answer_tokens.empty()) {
		return false;
	}

	std::vector<std::string> normalized_items;
	normalized_items.reserve(items.size());
	for (const auto &item : items) {
		normalized_items.push_back(NormalizeLettersOnlyLower(item));
	}

	size_t matched = 0;
	for (const auto &item : normalized_items) {
		if (matched >= answer_tokens.size()) {
			break;
		}
		if (item == NormalizeLettersOnlyLower(answer_tokens[matched])) {
			++matched;
		}
	}
	return matched == answer_tokens.size();
}

float ComputeWordCoverageRatio(const std::vector<std::string> &asr_words, const std::vector<std::string> &answer_words) {
	if (answer_words.empty()) {
		return asr_words.empty() ? 1.0f : 0.0f;
	}

	std::unordered_map<std::string, int> answer_count;
	for (const auto &word : answer_words) {
		++answer_count[word];
	}

	int matched = 0;
	for (const auto &word : asr_words) {
		auto it = answer_count.find(word);
		if (it == answer_count.end() || it->second <= 0) {
			continue;
		}
		--(it->second);
		++matched;
	}

	return static_cast<float>(matched) / static_cast<float>(answer_words.size());
}

[[maybe_unused]] int ParseTypeToken(const std::string &value) {
	std::string text = Trim(value);
	if (text.empty()) {
		return 1;
	}

	int type = 0;
	std::sscanf(text.c_str(), "%d", &type);
	if (type < 1 || type > 12) {
		return 1;
	}
	return type;
}

int64_t NowSec() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000000ULL);
}

int64_t NowMs() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000ULL);
}

[[maybe_unused]] std::string TodayDate() {
	const int64_t now = NowSec();
	const int day = static_cast<int>((now / 86400) % 3650);
	const int year = 2024 + day / 365;
	const int rem = day % 365;
	const int month = 1 + (rem / 30);
	const int d = 1 + (rem % 30);

	char buf[16] = {0};
	std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, d);
	return buf;
}

[[maybe_unused]] bool StepDone(sqlite3_stmt *stmt) {
	const int rc = sqlite3_step(stmt);
	return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

struct VocabularySeed {
	int word_id = 0;
	int meaning_id = 0;
	int example_id = 0;
	int meaning_count = 0;
	int meaning_with_example_count = 0;
	int word_example_count = 0;
	int selected_meaning_example_count = 0;
	std::string word;
	std::string meaning_zh;
	std::string meaning_en;
	std::string image;
	std::string example_en;
	std::string example_zh;
	std::string selection_zh;
	std::string selection_en;
	int stage = 1;
	bool is_review = false;
};

struct OptionSeed {
	int word_id = 0;
	std::string word;
	std::string meaning_zh;
	std::string meaning_en;
	std::string image;
	std::string audio_path;
};

struct SqliteStmtFinalizer {
	void operator()(sqlite3_stmt *stmt) const {
		if (stmt != nullptr) {
			sqlite3_finalize(stmt);
		}
	}
};

bool PrepareStatement(sqlite3 *db, const char *sql, std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> *stmt) {
	if (db == nullptr || sql == nullptr || stmt == nullptr) {
		return false;
	}

	sqlite3_stmt *raw_stmt = nullptr;
	const int rc = sqlite3_prepare_v2(db, sql, -1, &raw_stmt, nullptr);
	if (rc != SQLITE_OK || raw_stmt == nullptr) {
		WP_APP_DBLOGW(kTag, "prepare failed rc=%d msg=%s sql=%s", rc, db ? sqlite3_errmsg(db) : "null", sql);
		if (raw_stmt != nullptr) {
			sqlite3_finalize(raw_stmt);
		}
		return false;
	}

	stmt->reset(raw_stmt);
	return true;
}

bool LoadVocabularyMeaningFromStatement(sqlite3_stmt *stmt, VocabularySeed *seed) {
	if (stmt == nullptr || seed == nullptr) {
		return false;
	}

	if (sqlite3_step(stmt) != SQLITE_ROW) {
		return false;
	}

	seed->meaning_id = sqlite3_column_int(stmt, 1);
	const unsigned char *meaning_zh_text = sqlite3_column_text(stmt, 2);
	const unsigned char *meaning_en_text = sqlite3_column_text(stmt, 3);
	const unsigned char *image_text = sqlite3_column_text(stmt, 4);
	seed->meaning_zh = meaning_zh_text != nullptr ? reinterpret_cast<const char *>(meaning_zh_text) : "";
	seed->meaning_en = meaning_en_text != nullptr ? reinterpret_cast<const char *>(meaning_en_text) : "";
	if (Trim(seed->image).empty()) {
		seed->image = image_text != nullptr ? reinterpret_cast<const char *>(image_text) : "";
	}
	seed->stage = sqlite3_column_int(stmt, 5);
	return true;
}

bool LoadVocabularyExampleFromStatement(sqlite3_stmt *stmt, VocabularySeed *seed) {
	if (stmt == nullptr || seed == nullptr) {
		return false;
	}

	if (sqlite3_step(stmt) != SQLITE_ROW) {
		return false;
	}

	seed->example_id = sqlite3_column_int(stmt, 0);
	const unsigned char *example_en_text = sqlite3_column_text(stmt, 1);
	const unsigned char *example_zh_text = sqlite3_column_text(stmt, 2);
	const unsigned char *selection_zh_text = sqlite3_column_text(stmt, 3);
	const unsigned char *selection_en_text = sqlite3_column_text(stmt, 4);
	seed->example_en = example_en_text != nullptr ? reinterpret_cast<const char *>(example_en_text) : "";
	seed->example_zh = example_zh_text != nullptr ? reinterpret_cast<const char *>(example_zh_text) : "";
	seed->selection_zh = selection_zh_text != nullptr ? reinterpret_cast<const char *>(selection_zh_text) : "";
	seed->selection_en = selection_en_text != nullptr ? reinterpret_cast<const char *>(selection_en_text) : "";
	return true;
}

void ClearVocabularyExample(VocabularySeed *seed) {
	if (seed == nullptr) {
		return;
	}
	seed->example_id = 0;
	seed->selected_meaning_example_count = 0;
	seed->example_en.clear();
	seed->example_zh.clear();
	seed->selection_zh.clear();
	seed->selection_en.clear();
}

int QuerySingleInt(sqlite3 *db, const char *sql, int bind_int) {
	if (db == nullptr || sql == nullptr) {
		return 0;
	}
	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		return 0;
	}
	sqlite3_bind_int(stmt.get(), 1, bind_int);
	if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
		return 0;
	}
	return sqlite3_column_int(stmt.get(), 0);
}

void LoadVocabularySeedStats(sqlite3 *db, VocabularySeed *seed) {
	if (db == nullptr || seed == nullptr || seed->word_id <= 0) {
		return;
	}
	static constexpr const char *kMeaningCountSql =
		"SELECT COUNT(*) FROM word_meaning WHERE word_id = ?;";
	static constexpr const char *kMeaningWithExampleCountSql =
		"SELECT COUNT(DISTINCT m.id) "
		"FROM word_meaning AS m "
		"JOIN word_example AS e ON e.meaning_id = m.id "
		"WHERE m.word_id = ?;";
	static constexpr const char *kWordExampleCountSql =
		"SELECT COUNT(*) "
		"FROM word_example AS e "
		"JOIN word_meaning AS m ON m.id = e.meaning_id "
		"WHERE m.word_id = ?;";

	seed->meaning_count = QuerySingleInt(db, kMeaningCountSql, seed->word_id);
	seed->meaning_with_example_count = QuerySingleInt(db, kMeaningWithExampleCountSql, seed->word_id);
	seed->word_example_count = QuerySingleInt(db, kWordExampleCountSql, seed->word_id);
}

bool LoadRandomMeaningForSeed(sqlite3 *db, const char *sql, const word_practice::SelectedWord &selected_word, VocabularySeed *seed, bool bind_word) {
	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
	if (!PrepareStatement(db, sql, &stmt)) {
		WP_APP_DBLOGW(kTag,
			"load random meaning prepare failed word_id=%d word=%s bind_word=%d",
			selected_word.word_id,
			selected_word.word.c_str(),
			bind_word ? 1 : 0);
		return false;
	}

	if (bind_word) {
		sqlite3_bind_text(stmt.get(), 1, seed->word.c_str(), -1, SQLITE_TRANSIENT);
	} else {
		sqlite3_bind_int(stmt.get(), 1, selected_word.word_id);
	}

	if (!LoadVocabularyMeaningFromStatement(stmt.get(), seed)) {
		return false;
	}

	const int resolved_word_id = sqlite3_column_int(stmt.get(), 0);
	if (resolved_word_id > 0) {
		seed->word_id = resolved_word_id;
	}
	return true;
}

bool LoadRandomExampleForSeed(sqlite3 *db, VocabularySeed *seed) {
	if (db == nullptr || seed == nullptr || seed->meaning_id <= 0) {
		return false;
	}

	static constexpr const char *kSql =
		"SELECT "
		"e.id, "
		"COALESCE(e.example_en, ''), "
		"COALESCE(e.example_zh, ''), "
		"COALESCE(e.selection_zh, ''), "
		"COALESCE(e.selection_en, '') "
		"FROM word_example AS e "
		"WHERE e.meaning_id = ? "
		"ORDER BY RANDOM() "
		"LIMIT 1;";
	static constexpr const char *kLegacySql =
		"SELECT "
		"e.id, "
		"COALESCE(e.example_en, ''), "
		"COALESCE(e.example_zh, ''), "
		"'', "
		"'' "
		"FROM word_example AS e "
		"WHERE e.meaning_id = ? "
		"ORDER BY RANDOM() "
		"LIMIT 1;";

	seed->selected_meaning_example_count = QuerySingleInt(
		db,
		"SELECT COUNT(*) FROM word_example WHERE meaning_id = ?;",
		seed->meaning_id);

	std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
	if (!PrepareStatement(db, kSql, &stmt)) {
		WP_DIAG_LOGW(kTag,
			"load random example primary prepare failed meaning_id=%d word_id=%d msg=%s; trying legacy schema fallback",
			seed->meaning_id,
			seed->word_id,
			sqlite3_errmsg(db));
		if (!PrepareStatement(db, kLegacySql, &stmt)) {
			WP_DIAG_LOGW(kTag,
				"load random example legacy prepare failed meaning_id=%d word_id=%d msg=%s",
				seed->meaning_id,
				seed->word_id,
				sqlite3_errmsg(db));
			return false;
		}
	}
	sqlite3_bind_int(stmt.get(), 1, seed->meaning_id);
	return LoadVocabularyExampleFromStatement(stmt.get(), seed);
}

[[maybe_unused]] bool SeedHasDictionaryContent(const VocabularySeed &seed) {
	return !Trim(seed.meaning_zh).empty() ||
		   !Trim(seed.meaning_en).empty() ||
		   !Trim(seed.example_en).empty() ||
		   !Trim(seed.example_zh).empty() ||
		   !Trim(seed.selection_zh).empty() ||
		   !Trim(seed.selection_en).empty();
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

std::string BestMeaningText(const VocabularySeed &seed) {
	const std::string meaning_zh = Trim(seed.meaning_zh);
	if (!meaning_zh.empty()) {
		return meaning_zh;
	}
	return Trim(seed.meaning_en);
}

OptionSeed ToOptionSeed(const VocabularySeed &seed) {
	return OptionSeed{seed.word_id, seed.word, seed.meaning_zh, seed.meaning_en, seed.image, ""};
}

constexpr std::array<const char *, 4> kQuestionOptionTokens = {"A", "B", "C", "D"};

std::vector<std::string> BuildHintTokens(const std::string &text);
std::string BuildChoiceQuestionJson(const VocabularySeed &seed,
					const std::vector<OptionSeed> &options,
					const std::string &textbook_name,
					const std::string &prompt_text,
					const std::string &audio_path);
void ShuffleOptionVector(std::vector<OptionSeed> *options);
std::string BuildPairQuestionJson(const std::array<OptionSeed, 4> &left_options,
				  const std::array<OptionSeed, 4> &right_options,
				  const std::string &textbook_name);
std::string BuildSentenceQuestionJson(const VocabularySeed &seed,
				      const std::string &prompt_text,
				      const std::vector<std::string> &hints,
				      const std::string &textbook_name,
				      const std::string &audio_path);

int ComposeQuestionId(int word_id, int question_type) {
	return word_id > 0 ? word_id * 100 + question_type : question_type;
}

void ShuffleOptionArray(std::array<OptionSeed, 4> *options) {
	if (options == nullptr) {
		return;
	}
	for (size_t index = options->size(); index > 1; --index) {
		const size_t swap_index = static_cast<size_t>(esp_random() % index);
		std::swap((*options)[index - 1], (*options)[swap_index]);
	}
}

bool AppendChoiceQuestion(std::vector<word_practice::QuestionData> *question_pool,
					  int *question_built_count,
					  const VocabularySeed &seed,
					  const std::string &textbook_name,
					  int question_type,
					  const std::vector<OptionSeed> &candidate_options,
					  const std::string &prompt_text,
					  const std::string &audio_path,
					  size_t option_count = 4) {
	if (question_pool == nullptr ||
		question_built_count == nullptr ||
		option_count < 2 ||
		option_count > kQuestionOptionTokens.size() ||
		candidate_options.size() < option_count) {
		return false;
	}

	std::vector<OptionSeed> options(candidate_options.begin(), candidate_options.begin() + static_cast<std::ptrdiff_t>(option_count));
	ShuffleOptionVector(&options);

	size_t answer_index = 0;
	for (size_t index = 0; index < options.size(); ++index) {
		if (options[index].word_id == seed.word_id) {
			answer_index = index;
			break;
		}
	}

	word_practice::QuestionData question;
	question.id = ComposeQuestionId(seed.word_id, question_type);
	question.type = question_type;
	question.stage = textbook_name;
	question.difficulty = seed.is_review ? 1 : 2;
	question.answer = kQuestionOptionTokens[answer_index];
	question.content_json = BuildChoiceQuestionJson(seed, options, textbook_name, prompt_text, audio_path);
	if (question.content_json.empty()) {
		return false;
	}

	question_pool->push_back(std::move(question));
	++(*question_built_count);
	return true;
}

bool AppendPairQuestion(std::vector<word_practice::QuestionData> *question_pool,
				int *question_built_count,
				const VocabularySeed &seed,
				const std::string &textbook_name,
				const std::vector<OptionSeed> &standard_options) {
	if (question_pool == nullptr || question_built_count == nullptr || standard_options.size() < 4) {
		return false;
	}

	std::array<OptionSeed, 4> left_options = {
		standard_options[0], standard_options[1], standard_options[2], standard_options[3]};
	ShuffleOptionArray(&left_options);
	std::array<OptionSeed, 4> right_options = left_options;
	ShuffleOptionArray(&right_options);

	std::string expected_pairs;
	for (const auto &left : left_options) {
		for (const auto &right : right_options) {
			if (left.word_id != right.word_id) {
				continue;
			}
			if (!expected_pairs.empty()) {
				expected_pairs += ";";
			}
			expected_pairs += Trim(left.word) + "-" + Trim(right.meaning_zh);
			break;
		}
	}

	word_practice::QuestionData question;
	question.id = ComposeQuestionId(seed.word_id, 4);
	question.type = 4;
	question.stage = textbook_name;
	question.difficulty = seed.is_review ? 1 : 2;
	question.answer = std::move(expected_pairs);
	question.content_json = BuildPairQuestionJson(left_options, right_options, textbook_name);
	if (question.content_json.empty()) {
		return false;
	}

	question_pool->push_back(std::move(question));
	++(*question_built_count);
	return true;
}

bool AppendSentenceQuestion(std::vector<word_practice::QuestionData> *question_pool,
				    int *question_built_count,
				    const VocabularySeed &seed,
				    const std::string &textbook_name,
				    int question_type,
				    const std::string &prompt_text,
				    const std::string &answer_text,
				    const std::vector<std::string> &hints,
				    const std::string &audio_path) {
	if (question_pool == nullptr || question_built_count == nullptr) {
		return false;
	}
	if (Trim(prompt_text).empty() || Trim(answer_text).empty()) {
		return false;
	}

	word_practice::QuestionData question;
	question.id = ComposeQuestionId(seed.word_id, question_type);
	question.type = question_type;
	question.stage = textbook_name;
	question.difficulty = seed.is_review ? 1 : 2;
	question.answer = answer_text;
	question.content_json = BuildSentenceQuestionJson(
		seed,
		prompt_text,
		hints,
		textbook_name,
		audio_path);
	if (question.content_json.empty()) {
		return false;
	}

	question_pool->push_back(std::move(question));
	++(*question_built_count);
	return true;
}

void AddNestedWordPayload(cJSON *parent, const char *key, const std::string &word) {
	if (!parent || !key || word.empty()) {
		return;
	}
	cJSON *word_obj = cJSON_CreateObject();
	if (!word_obj) {
		return;
	}
	cJSON_AddStringToObject(word_obj, "word", word.c_str());
	cJSON_AddItemToObject(parent, key, word_obj);
}

void AddNestedMeaningPayload(cJSON *parent, const char *key, const std::string &meaning_zh, const std::string &meaning_en) {
	if (!parent || !key || (meaning_zh.empty() && meaning_en.empty())) {
		return;
	}
	cJSON *meaning_obj = cJSON_CreateObject();
	if (!meaning_obj) {
		return;
	}
	if (!meaning_zh.empty()) {
		cJSON_AddStringToObject(meaning_obj, "meaning_zh", meaning_zh.c_str());
	}
	if (!meaning_en.empty()) {
		cJSON_AddStringToObject(meaning_obj, "meaning_en", meaning_en.c_str());
	}
	cJSON_AddItemToObject(parent, key, meaning_obj);
}

void AddOptionPayload(cJSON *options_obj, const char *token, const OptionSeed &option) {
	if (!options_obj || !token) {
		return;
	}
	cJSON *option_obj = cJSON_CreateObject();
	if (!option_obj) {
		return;
	}
	AddNestedWordPayload(option_obj, "word", Trim(option.word));
	AddNestedMeaningPayload(option_obj, "word_meaning", Trim(option.meaning_zh), Trim(option.meaning_en));
	if (option.word_id > 0) {
		cJSON_AddNumberToObject(option_obj, "word_id", option.word_id);
	}
	const std::string image_name = ResolveStage1ImageName(option.image, option.word_id, option.word);
	if (!image_name.empty()) {
		cJSON_AddStringToObject(option_obj, "image", image_name.c_str());
	}
	cJSON_AddItemToObject(options_obj, token, option_obj);
}

std::vector<std::string> BuildHintTokens(const std::string &text) {
	std::vector<std::string> hints = SplitHintWords(text);
	if (!hints.empty()) {
		return hints;
	}
	std::vector<std::string> words = NormalizeSentenceWordsLower(text);
	if (!words.empty()) {
		return words;
	}
	const std::string trimmed = Trim(text);
	if (!trimmed.empty()) {
		hints.push_back(trimmed);
	}
	return hints;
}

std::vector<std::string> BuildQuestionHints(const std::string &answer_text, const std::string &selection_text) {
	std::vector<std::string> answer_tokens = ExtractSentenceDisplayTokens(answer_text);
	std::vector<std::string> hints = answer_tokens;
	std::vector<std::string> selection_tokens = ExtractSentenceDisplayTokens(selection_text);
	hints.insert(hints.end(), selection_tokens.begin(), selection_tokens.end());

	if (hints.empty()) {
		return BuildHintTokens(answer_text);
	}
	if (hints.size() <= 1 || answer_tokens.size() <= 1) {
		return hints;
	}

	std::vector<std::string> shuffled = hints;
	for (int attempt = 0; attempt < 32; ++attempt) {
		ShuffleStringVector(&shuffled);
		if (!PreservesAnswerTokenOrder(shuffled, answer_tokens)) {
			return shuffled;
		}
	}

	shuffled.clear();
	for (auto it = answer_tokens.rbegin(); it != answer_tokens.rend(); ++it) {
		shuffled.push_back(*it);
	}
	shuffled.insert(shuffled.end(), selection_tokens.begin(), selection_tokens.end());
	if (!PreservesAnswerTokenOrder(shuffled, answer_tokens)) {
		return shuffled;
	}

	return hints;
}

std::vector<OptionSeed> BuildOptionCandidates(const VocabularySeed &seed, const std::vector<OptionSeed> &distractors, bool require_image) {
	std::vector<OptionSeed> options;
	options.reserve(kQuestionOptionTokens.size());
	const OptionSeed correct = ToOptionSeed(seed);
	if (!require_image || !Trim(correct.image).empty()) {
		options.push_back(correct);
	}
	for (const auto &candidate : distractors) {
		if (options.size() >= kQuestionOptionTokens.size()) {
			break;
		}
		if (candidate.word_id == seed.word_id) {
			continue;
		}
		if (require_image && Trim(candidate.image).empty()) {
			continue;
		}
		options.push_back(candidate);
	}
	return options;
}

void ShuffleOptionVector(std::vector<OptionSeed> *options) {
	if (options == nullptr || options->size() < 2) {
		return;
	}
	for (size_t index = options->size(); index > 1; --index) {
		const size_t swap_index = static_cast<size_t>(esp_random() % index);
		std::swap((*options)[index - 1], (*options)[swap_index]);
	}
}

std::vector<OptionSeed> BuildDistractorOptionsFromSeeds(const VocabularySeed &seed,
										const std::vector<VocabularySeed> &loaded_seeds,
										size_t limit) {
	std::vector<OptionSeed> distractors;
	if (limit == 0) {
		return distractors;
	}

	distractors.reserve(std::min(limit, loaded_seeds.size()));
	for (const auto &candidate_seed : loaded_seeds) {
		if (candidate_seed.word_id == seed.word_id) {
			continue;
		}
		const OptionSeed candidate = ToOptionSeed(candidate_seed);
		if (Trim(candidate.word).empty()) {
			continue;
		}
		distractors.push_back(candidate);
	}

	ShuffleOptionVector(&distractors);
	if (distractors.size() > limit) {
		distractors.resize(limit);
	}
	return distractors;
}

std::string ResolveQuestionAudioField(const std::string &audio_path, int word_id, const std::string &word_text) {
	const std::string trimmed = Trim(audio_path);
	if (!trimmed.empty() && trimmed.front() == '/') {
		return trimmed;
	}
	return ResolveStage1AudioName(trimmed, word_id, word_text);
}

std::string BuildChoiceQuestionJson(const VocabularySeed &seed,
					const std::vector<OptionSeed> &options,
						const std::string &textbook_name,
						const std::string &prompt_text,
						const std::string &audio_path) {
	cJSON *root = cJSON_CreateObject();
	if (root == nullptr) {
		return {};
	}
	if (!prompt_text.empty()) {
		cJSON_AddStringToObject(root, "question", prompt_text.c_str());
	}
	if (!textbook_name.empty()) {
		cJSON_AddStringToObject(root, "textbook", textbook_name.c_str());
	}
	if (seed.word_id > 0) {
		cJSON_AddNumberToObject(root, "word_id", seed.word_id);
	}
	const std::string resolved_audio = ResolveQuestionAudioField(audio_path, seed.word_id, seed.word);
	if (!resolved_audio.empty()) {
		cJSON_AddStringToObject(root, "audio", resolved_audio.c_str());
	}
	AddNestedWordPayload(root, "word", Trim(seed.word));
	AddNestedMeaningPayload(root, "word_meaning", Trim(seed.meaning_zh), Trim(seed.meaning_en));

	cJSON *options_obj = cJSON_CreateObject();
	if (options_obj != nullptr) {
		for (size_t index = 0; index < options.size() && index < kQuestionOptionTokens.size(); ++index) {
			AddOptionPayload(options_obj, kQuestionOptionTokens[index], options[index]);
		}
		cJSON_AddItemToObject(root, "options", options_obj);
	}

	char *json = cJSON_PrintUnformatted(root);
	std::string content = json != nullptr ? json : "";
	if (json != nullptr) {
		cJSON_free(json);
	}
	cJSON_Delete(root);
	return content;
}

std::string BuildPairQuestionJson(const std::array<OptionSeed, 4> &left_options,
					   const std::array<OptionSeed, 4> &right_options,
					   const std::string &textbook_name) {
	cJSON *root = cJSON_CreateObject();
	if (root == nullptr) {
		return {};
	}
	cJSON_AddStringToObject(root, "question", "单词配对");
	if (!textbook_name.empty()) {
		cJSON_AddStringToObject(root, "textbook", textbook_name.c_str());
	}
	cJSON *left = cJSON_CreateArray();
	cJSON *right = cJSON_CreateArray();
	if (left != nullptr && right != nullptr) {
		for (const auto &option : left_options) {
			cJSON *left_item = cJSON_CreateObject();
			if (left_item == nullptr) {
				continue;
			}
			const std::string left_word = Trim(option.word);
			if (!left_word.empty()) {
				cJSON_AddStringToObject(left_item, "word", left_word.c_str());
			}
			const std::string resolved_audio = ResolveStage1AudioName(option.audio_path, option.word_id, option.word);
			if (!resolved_audio.empty()) {
				cJSON_AddStringToObject(left_item, "audio", resolved_audio.c_str());
			}
			cJSON_AddItemToArray(left, left_item);
		}
		for (const auto &option : right_options) {
			cJSON_AddItemToArray(right, cJSON_CreateString(Trim(option.meaning_zh).c_str()));
		}
		cJSON_AddItemToObject(root, "left", left);
		cJSON_AddItemToObject(root, "right", right);
	} else {
		if (left) cJSON_Delete(left);
		if (right) cJSON_Delete(right);
	}

	char *json = cJSON_PrintUnformatted(root);
	std::string content = json != nullptr ? json : "";
	if (json != nullptr) {
		cJSON_free(json);
	}
	cJSON_Delete(root);
	return content;
}

std::string BuildSentenceQuestionJson(const VocabularySeed &seed,
					  const std::string &prompt_text,
					  const std::vector<std::string> &hints,
					  const std::string &textbook_name,
					  const std::string &audio_path) {
	cJSON *root = cJSON_CreateObject();
	if (root == nullptr) {
		return {};
	}
	if (!prompt_text.empty()) {
		cJSON_AddStringToObject(root, "question", prompt_text.c_str());
		cJSON_AddStringToObject(root, "prompt", prompt_text.c_str());
	}
	if (!textbook_name.empty()) {
		cJSON_AddStringToObject(root, "textbook", textbook_name.c_str());
	}
	if (seed.word_id > 0) {
		cJSON_AddNumberToObject(root, "word_id", seed.word_id);
	}
	const std::string resolved_audio = ResolveQuestionAudioField(audio_path, seed.word_id, seed.word);
	if (!resolved_audio.empty()) {
		cJSON_AddStringToObject(root, "audio", resolved_audio.c_str());
	}
	AddNestedWordPayload(root, "word", Trim(seed.word));
	AddNestedMeaningPayload(root, "word_meaning", Trim(seed.meaning_zh), Trim(seed.meaning_en));
	if (!hints.empty()) {
		cJSON *hints_obj = cJSON_CreateArray();
		if (hints_obj != nullptr) {
			for (const auto &hint : hints) {
				cJSON_AddItemToArray(hints_obj, cJSON_CreateString(Trim(hint).c_str()));
			}
			cJSON_AddItemToObject(root, "hints", hints_obj);
		}
	}

	char *json = cJSON_PrintUnformatted(root);
	std::string content = json != nullptr ? json : "";
	if (json != nullptr) {
		cJSON_free(json);
	}
	cJSON_Delete(root);
	return content;
}

bool LoadVocabularySeed(sqlite3 *db, const word_practice::SelectedWord &selected_word, VocabularySeed *seed) {
	if (db == nullptr || seed == nullptr || selected_word.word_id <= 0 || selected_word.word.empty()) {
		return false;
	}

	seed->word_id = selected_word.word_id;
	seed->word = Trim(selected_word.word);
	seed->image = Trim(selected_word.image);
	seed->stage = 1;
	seed->is_review = selected_word.is_review;
	ClearVocabularyExample(seed);
	if (seed->word.empty()) {
		return false;
	}
	LoadVocabularySeedStats(db, seed);
	WP_DIAG_LOGW(kTag,
		"load seed start selected_word_id=%d resolved_word=%s meaning_count=%d meaning_with_example_count=%d word_example_count=%d",
		selected_word.word_id,
		seed->word.c_str(),
		seed->meaning_count,
		seed->meaning_with_example_count,
		seed->word_example_count);

	const char *sql_by_id =
		"SELECT "
		"w.id, "
		"COALESCE(m.id, 0), "
		"COALESCE(m.meaning_zh, ''), "
		"COALESCE(m.meaning_en, ''), "
		"COALESCE(w.image, ''), "
		"COALESCE(m.stage, 1) "
		"FROM word AS w "
		"LEFT JOIN word_meaning AS m ON m.word_id = w.id "
		"WHERE w.id = ? "
		"ORDER BY RANDOM() "
		"LIMIT 1;";
	const char *sql_by_id_with_example =
		"SELECT "
		"w.id, "
		"COALESCE(m.id, 0), "
		"COALESCE(m.meaning_zh, ''), "
		"COALESCE(m.meaning_en, ''), "
		"COALESCE(w.image, ''), "
		"COALESCE(m.stage, 1) "
		"FROM word AS w "
		"JOIN word_meaning AS m ON m.word_id = w.id "
		"JOIN word_example AS e ON e.meaning_id = m.id "
		"WHERE w.id = ? "
		"GROUP BY w.id, m.id, m.meaning_zh, m.meaning_en, w.image, m.stage "
		"ORDER BY RANDOM() "
		"LIMIT 1;";
	const char *sql_by_word =
		"SELECT "
		"w.id, "
		"COALESCE(m.id, 0), "
		"COALESCE(m.meaning_zh, ''), "
		"COALESCE(m.meaning_en, ''), "
		"COALESCE(w.image, ''), "
		"COALESCE(m.stage, 1) "
		"FROM word AS w "
		"LEFT JOIN word_meaning AS m ON m.word_id = w.id "
		"WHERE w.word = ? "
		"ORDER BY RANDOM() "
		"LIMIT 1;";
	const char *sql_by_word_with_example =
		"SELECT "
		"w.id, "
		"COALESCE(m.id, 0), "
		"COALESCE(m.meaning_zh, ''), "
		"COALESCE(m.meaning_en, ''), "
		"COALESCE(w.image, ''), "
		"COALESCE(m.stage, 1) "
		"FROM word AS w "
		"JOIN word_meaning AS m ON m.word_id = w.id "
		"JOIN word_example AS e ON e.meaning_id = m.id "
		"WHERE w.word = ? "
		"GROUP BY w.id, m.id, m.meaning_zh, m.meaning_en, w.image, m.stage "
		"ORDER BY RANDOM() "
		"LIMIT 1;";

	bool loaded_with_example = LoadRandomMeaningForSeed(db, sql_by_id_with_example, selected_word, seed, false);
	bool loaded_by_id = loaded_with_example;
	if (!loaded_by_id) {
		loaded_by_id = LoadRandomMeaningForSeed(db, sql_by_id, selected_word, seed, false);
	}

	if (seed->meaning_id <= 0) {
		loaded_with_example = LoadRandomMeaningForSeed(db, sql_by_word_with_example, selected_word, seed, true);
		if (loaded_with_example || LoadRandomMeaningForSeed(db, sql_by_word, selected_word, seed, true)) {
			WP_APP_DBLOGW(kTag,
				"load seed fallback hit by word word_id=%d word=%s meaning_id=%d",
				selected_word.word_id,
				seed->word.c_str(),
				seed->meaning_id);
		}
	}

	if (seed->meaning_id > 0) {
		ClearVocabularyExample(seed);
		const bool has_example = LoadRandomExampleForSeed(db, seed);
		WP_DIAG_LOGW(kTag,
			"load seed random result word_id=%d word=%s loaded_by_id=%d preferred_example_meaning=%d meaning_id=%d stage=%d selected_meaning_example_count=%d example_id=%d has_example=%d example_en_len=%d example_zh_len=%d selection_en_len=%d selection_zh_len=%d",
			seed->word_id,
			seed->word.c_str(),
			loaded_by_id ? 1 : 0,
			loaded_with_example ? 1 : 0,
			seed->meaning_id,
			seed->stage,
			seed->selected_meaning_example_count,
			seed->example_id,
			has_example ? 1 : 0,
			static_cast<int>(seed->example_en.size()),
			static_cast<int>(seed->example_zh.size()),
			static_cast<int>(seed->selection_en.size()),
			static_cast<int>(seed->selection_zh.size()));
	}

	return !seed->word.empty();
}

[[maybe_unused]] std::vector<OptionSeed> LoadDistractorOptions(sqlite3 *db, int word_id, const std::string &word, size_t limit) {
	std::vector<OptionSeed> distractors;
	if (db == nullptr || limit == 0) {
		return distractors;
	}
	distractors.reserve(limit);

	auto append_from_query = [&](const char *sql) {
		std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer> stmt;
		if (!PrepareStatement(db, sql, &stmt)) {
			WP_APP_DBLOGW(kTag, "load distractors prepare failed word_id=%d word=%s", word_id, word.c_str());
			return;
		}

		std::unordered_set<int> seen;
		if (word_id > 0) {
			seen.insert(word_id);
		}
		for (const auto &existing : distractors) {
			if (existing.word_id > 0) {
				seen.insert(existing.word_id);
			}
		}

		sqlite3_bind_text(stmt.get(), 1, word.c_str(), -1, SQLITE_TRANSIENT);
		while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
			OptionSeed option;
			option.word_id = sqlite3_column_int(stmt.get(), 0);
			const unsigned char *word_text = sqlite3_column_text(stmt.get(), 1);
			const unsigned char *meaning_zh_text = sqlite3_column_text(stmt.get(), 2);
			const unsigned char *meaning_en_text = sqlite3_column_text(stmt.get(), 3);
			const unsigned char *image_text = sqlite3_column_text(stmt.get(), 4);
			if (option.word_id <= 0 || word_text == nullptr || seen.find(option.word_id) != seen.end()) {
				continue;
			}
			option.word = Trim(reinterpret_cast<const char *>(word_text));
			option.meaning_zh = meaning_zh_text != nullptr ? reinterpret_cast<const char *>(meaning_zh_text) : "";
			option.meaning_en = meaning_en_text != nullptr ? reinterpret_cast<const char *>(meaning_en_text) : "";
			option.image = image_text != nullptr ? reinterpret_cast<const char *>(image_text) : "";
			if (option.word.empty()) {
				continue;
			}
			seen.insert(option.word_id);
			distractors.push_back(std::move(option));
			if (distractors.size() >= limit) {
				break;
			}
		}
	};

	const char *random_sql =
		"SELECT w.id, w.word, "
		"COALESCE(MIN(m.meaning_zh), ''), "
		"COALESCE(MIN(m.meaning_en), ''), "
		"COALESCE(w.image, '') "
		"FROM word AS w "
		"LEFT JOIN word_meaning AS m ON m.word_id = w.id "
		"WHERE w.word <> ? AND w.word IS NOT NULL AND TRIM(w.word) <> '' "
		"GROUP BY w.id, w.word, w.image "
		"ORDER BY RANDOM() "
		"LIMIT 256;";
	const char *fallback_sql =
		"SELECT w.id, w.word, "
		"COALESCE(MIN(m.meaning_zh), ''), "
		"COALESCE(MIN(m.meaning_en), ''), "
		"COALESCE(w.image, '') "
		"FROM word AS w "
		"LEFT JOIN word_meaning AS m ON m.word_id = w.id "
		"WHERE w.word <> ? AND w.word IS NOT NULL AND TRIM(w.word) <> '' "
		"GROUP BY w.id, w.word, w.image "
		"ORDER BY w.id ASC;";

	append_from_query(random_sql);
	if (distractors.size() < limit) {
		append_from_query(fallback_sql);
	}

	return distractors;
}

std::vector<word_practice::QuestionData> BuildVocabularyQuestionPool(
	const std::vector<word_practice::SelectedWord> &selected_words,
	bool include_speak_questions) {
	std::vector<word_practice::QuestionData> question_pool;
	if (selected_words.empty()) {
		return question_pool;
	}
	const int64_t build_start_ms = NowMs();
	WP_DIAG_LOGW(kTag, "build vocabulary pool start selected=%d", static_cast<int>(selected_words.size()));

	const std::string db_path = eteacher::database_manager::DiscoverDictionaryDbPath(kTag);
	if (db_path.empty()) {
		ESP_LOGW(kTag, "dictionary db path not found while building vocabulary pool");
		return question_pool;
	}

	sqlite3 *db = nullptr;
	std::string opened_db_path;
	if (!eteacher::database_manager::OpenReadonlyDbFile(db_path, &db, &opened_db_path, kTag, "word_practice_dictionary") || db == nullptr) {
		ESP_LOGW(kTag, "open dictionary db failed via sqlite_db_api path=%s", db_path.c_str());
		return question_pool;
	}
	LogResolvedDbAccess("word_practice_dictionary", opened_db_path, "sqlite_db_api::OpenReadonlyDbFile");

	question_pool.reserve(selected_words.size() * (include_speak_questions ? 12 : 8));
	std::vector<VocabularySeed> loaded_seeds;
	loaded_seeds.reserve(selected_words.size());
	int seed_loaded_count = 0;
	int question_built_count = 0;
	std::array<int, 13> type_built_count = {};
	int64_t seed_load_total_ms = 0;
	int64_t question_build_total_ms = 0;

	for (const auto &selected_word : selected_words) {
		const int64_t seed_start_ms = NowMs();
		VocabularySeed seed;
		if (!LoadVocabularySeed(db, selected_word, &seed)) {
			seed_load_total_ms += (NowMs() - seed_start_ms);
			ESP_LOGW(kTag, "skip vocabulary seed word_id=%d word=%s", selected_word.word_id, selected_word.word.c_str());
			continue;
		}
		seed_load_total_ms += (NowMs() - seed_start_ms);
		++seed_loaded_count;
		loaded_seeds.push_back(std::move(seed));
	}

	for (const auto &seed : loaded_seeds) {
		const int64_t question_start_ms = NowMs();
		const std::vector<OptionSeed> distractors = BuildDistractorOptionsFromSeeds(seed, loaded_seeds, 12);
		const std::string word_audio = ResolveStage1AudioName("", seed.word_id, seed.word);
		const std::string example_audio = ResolveStage1ExampleAudioName(seed.word_id, seed.word, seed.example_id);
		const std::string example_en = Trim(seed.example_en);
		const std::string example_zh = Trim(seed.example_zh);
		const bool has_example_sentence = !example_en.empty() && !example_zh.empty();

		const std::string stage_tag = StageNumberToTag(seed.stage);
		const std::string textbook_name = stage_tag.empty() ? "vocab" : stage_tag;

		const std::vector<OptionSeed> standard_options = BuildOptionCandidates(seed, distractors, false);
		const std::vector<OptionSeed> image_options = BuildOptionCandidates(seed, distractors, true);
		const size_t before_count = question_pool.size();
		WP_DIAG_LOGW(kTag,
			"build word start word_id=%d word=%s distractors=%d standard_options=%d image_options=%d has_image=%d meaning_count=%d meaning_with_example_count=%d word_example_count=%d meaning_id=%d selected_meaning_example_count=%d example_id=%d example_en=%d example_zh=%d has_example_sentence=%d word_audio=%d example_audio=%d",
			seed.word_id,
			seed.word.c_str(),
			static_cast<int>(distractors.size()),
			static_cast<int>(standard_options.size()),
			static_cast<int>(image_options.size()),
			Trim(seed.image).empty() ? 0 : 1,
			seed.meaning_count,
			seed.meaning_with_example_count,
			seed.word_example_count,
			seed.meaning_id,
			seed.selected_meaning_example_count,
			seed.example_id,
			example_en.empty() ? 0 : 1,
			example_zh.empty() ? 0 : 1,
			has_example_sentence ? 1 : 0,
			word_audio.empty() ? 0 : 1,
			example_audio.empty() ? 0 : 1);

		if (!Trim(seed.image).empty() && image_options.size() >= kImageChoiceOptionCount) {
			if (AppendChoiceQuestion(
					&question_pool,
					&question_built_count,
					seed,
					textbook_name,
					1,
					image_options,
					Trim(seed.word),
					"",
					kImageChoiceOptionCount)) {
				++type_built_count[1];
			}
		}
		if (AppendChoiceQuestion(&question_pool, &question_built_count, seed, textbook_name, 2, standard_options, BestMeaningText(seed), "")) {
			++type_built_count[2];
		}
		if (AppendChoiceQuestion(&question_pool, &question_built_count, seed, textbook_name, 3, standard_options, Trim(seed.word), "")) {
			++type_built_count[3];
		}

		if (AppendPairQuestion(&question_pool, &question_built_count, seed, textbook_name, standard_options)) {
			++type_built_count[4];
		}

		if (has_example_sentence) {
			if (AppendSentenceQuestion(&question_pool, &question_built_count, seed, textbook_name, 5, example_zh, example_en,
					   BuildQuestionHints(example_en, seed.selection_en), BuildExampleQuestionAudioPath(example_audio))) {
				++type_built_count[5];
			}
			if (AppendSentenceQuestion(&question_pool, &question_built_count, seed, textbook_name, 6, example_en, example_en,
					   BuildQuestionHints(example_en, seed.selection_en), BuildExampleQuestionAudioPath(example_audio))) {
				++type_built_count[6];
			}
		}
		if (!has_example_sentence) {
			WP_DIAG_LOGW(kTag,
				"skip example question types word_id=%d word=%s meaning_id=%d example_id=%d selected_meaning_example_count=%d example_en_len=%d example_zh_len=%d selection_en_len=%d selection_zh_len=%d",
				seed.word_id,
				seed.word.c_str(),
				seed.meaning_id,
				seed.example_id,
				seed.selected_meaning_example_count,
				static_cast<int>(example_en.size()),
				static_cast<int>(example_zh.size()),
				static_cast<int>(seed.selection_en.size()),
				static_cast<int>(seed.selection_zh.size()));
		}
		if (include_speak_questions) {
			if (AppendSentenceQuestion(&question_pool, &question_built_count, seed, textbook_name, 7, Trim(seed.word), Trim(seed.word),
					   BuildHintTokens(Trim(seed.word)), BuildQuestionAudioPath(word_audio))) {
				++type_built_count[7];
			}
			if (AppendSentenceQuestion(&question_pool, &question_built_count, seed, textbook_name, 8, BestMeaningText(seed), Trim(seed.word),
					   BuildHintTokens(Trim(seed.word)), BuildQuestionAudioPath(word_audio))) {
				++type_built_count[8];
			}
			if (has_example_sentence) {
				if (AppendSentenceQuestion(&question_pool, &question_built_count, seed, textbook_name, 9, example_en, example_en,
						   BuildQuestionHints(example_en, seed.selection_en), BuildExampleQuestionAudioPath(example_audio))) {
					++type_built_count[9];
				}
				if (AppendSentenceQuestion(&question_pool, &question_built_count, seed, textbook_name, 10, example_zh, example_en,
						   BuildQuestionHints(example_en, seed.selection_en), BuildExampleQuestionAudioPath(example_audio))) {
					++type_built_count[10];
				}
			}
		}
		if (AppendChoiceQuestion(
			&question_pool,
			&question_built_count,
			seed,
			textbook_name,
			11,
			standard_options,
			word_audio.empty() ? Trim(seed.word) : "按Start播放音频",
			word_audio)) {
			++type_built_count[11];
		}
		if (AppendChoiceQuestion(
			&question_pool,
			&question_built_count,
			seed,
			textbook_name,
			12,
			standard_options,
			word_audio.empty() ? BestMeaningText(seed) : "按Start播放音频",
			word_audio)) {
			++type_built_count[12];
		}

		std::array<int, 12> built_types = {};
		for (size_t index = before_count; index < question_pool.size(); ++index) {
			const int type = question_pool[index].type;
			if (type >= 1 && type <= 12) {
				++built_types[static_cast<size_t>(type - 1)];
			}
		}
		WP_DIAG_LOGW(kTag,
			"build word done word_id=%d built_total=%d built_types t1=%d t2=%d t3=%d t4=%d t5=%d t6=%d t7=%d t8=%d t9=%d t10=%d t11=%d t12=%d",
			seed.word_id,
			static_cast<int>(question_pool.size() - before_count),
			built_types[0],
			built_types[1],
			built_types[2],
			built_types[3],
			built_types[4],
			built_types[5],
			built_types[6],
			built_types[7],
			built_types[8],
			built_types[9],
			built_types[10],
			built_types[11]);
		question_build_total_ms += (NowMs() - question_start_ms);
	}

	WP_APP_DBLOGW(kTag,
		"build vocabulary pool selected=%d seed_loaded=%d question_built=%d",
		static_cast<int>(selected_words.size()),
		seed_loaded_count,
		question_built_count);
	WP_APP_DBLOGW(kTag,
		"build vocabulary pool by type t1=%d t2=%d t3=%d t4=%d t5=%d t6=%d t7=%d t8=%d t9=%d t10=%d t11=%d t12=%d",
		type_built_count[1],
		type_built_count[2],
		type_built_count[3],
		type_built_count[4],
		type_built_count[5],
		type_built_count[6],
		type_built_count[7],
		type_built_count[8],
		type_built_count[9],
		type_built_count[10],
		type_built_count[11],
		type_built_count[12]);
	ESP_LOGW(kTag,
		"startup timing build_pool total_ms=%lld seed_load_ms=%lld question_build_ms=%lld selected_words=%d loaded_seeds=%d question_pool=%d",
		static_cast<long long>(NowMs() - build_start_ms),
		static_cast<long long>(seed_load_total_ms),
		static_cast<long long>(question_build_total_ms),
		static_cast<int>(selected_words.size()),
		seed_loaded_count,
		static_cast<int>(question_pool.size()));

	sqlite3_close(db);
	return question_pool;
}

constexpr int kPromptDashWidthShort = 120;
constexpr int kPromptDashWidthLong = 300;
constexpr int kPromptDashGapPx = 2;

struct DashLineProfile {
	int16_t black_len = 4;
	int16_t white_len = 1;
};

class DashedLineWidget : public app_ui::Widget {
public:
	void SetProfile(const DashLineProfile &profile) {
		profile_.black_len = std::max<int16_t>(1, profile.black_len);
		profile_.white_len = std::max<int16_t>(1, profile.white_len);
		MarkMeasureDirty();
		MarkDirty();
	}

	const DashLineProfile &Profile() const {
		return profile_;
	}

protected:
	app_ui::Size OnMeasure(const app_ui::Size &constraint) override {
		return constraint;
	}

	void OnDraw(app_ui::Painter &p) override {
		const app_ui::Rect rect = LocalRect();
		if (rect.w <= 0 || rect.h <= 0) {
			return;
		}

		p.SetDrawColor(app_ui::Color::Black);
		const int step = std::max<int>(1, profile_.black_len + profile_.white_len);
		for (int16_t x = 0; x < rect.w; x = static_cast<int16_t>(x + step)) {
			const int seg_w = std::min<int>(profile_.black_len, rect.w - x);
			if (seg_w > 0) {
				p.DrawHLine({x, 0}, seg_w);
			}
		}
	}

private:
	DashLineProfile profile_{};
};

std::string FitUtf8TextToWidth(const std::string &text, int max_width, const char *font_name, CustomEpdDisplay *epd, size_t *used_bytes) {
	if (used_bytes) {
		*used_bytes = 0;
	}
	if (text.empty()) {
		return {};
	}
	if (!epd || max_width <= 0) {
		if (used_bytes) {
			*used_bytes = text.size();
		}
		return text;
	}

	const int full_width = static_cast<int>(epd->MeasureUtf8Width(text, font_name));
	if (full_width <= max_width) {
		if (used_bytes) {
			*used_bytes = text.size();
		}
		return text;
	}

	size_t idx = 0;
	size_t best = 0;
	while (idx < text.size()) {
		size_t next = idx;
		uint32_t cp = 0;
		if (!DecodeNextUtf8Codepoint(text, next, cp)) {
			next = idx + 1;
		}
		const std::string candidate = text.substr(0, next);
		const int candidate_width = static_cast<int>(epd->MeasureUtf8Width(candidate, font_name));
		if (candidate_width > max_width) {
			break;
		}
		best = next;
		idx = next;
	}

	if (best == 0) {
		best = 1;
	}

	if (used_bytes) {
		*used_bytes = best;
	}
	return text.substr(0, best);
}

std::string FitUtf8TextWithEllipsisToWidth(const std::string &text,
					  int max_width,
					  const char *font_name,
					  CustomEpdDisplay *epd) {
	if (text.empty() || !epd || max_width <= 0) {
		return text;
	}
	if (epd->MeasureUtf8Width(text, font_name) <= max_width) {
		return text;
	}

	const std::string ellipsis = "...";
	if (epd->MeasureUtf8Width(ellipsis, font_name) > max_width) {
		size_t used_bytes = 0;
		return FitUtf8TextToWidth(ellipsis, max_width, font_name, epd, &used_bytes);
	}

	size_t idx = 0;
	size_t best = 0;
	while (idx < text.size()) {
		size_t next = idx;
		uint32_t cp = 0;
		if (!DecodeNextUtf8Codepoint(text, next, cp)) {
			next = idx + 1;
		}
		const std::string candidate = text.substr(0, next) + ellipsis;
		if (epd->MeasureUtf8Width(candidate, font_name) > max_width) {
			break;
		}
		best = next;
		idx = next;
	}

	if (best == 0) {
		return ellipsis;
	}
	return text.substr(0, best) + ellipsis;
}

struct Type4MeaningTextLayout {
	std::string text;
	const char *font_name = "wenquanyi_11pt";
};

Type4MeaningTextLayout LayoutType4MeaningText(const std::string &text, CustomEpdDisplay *epd) {
	const std::string trimmed = Trim(text);
	if (trimmed.empty()) {
		return {};
	}

	if (!epd || epd->MeasureUtf8Width(trimmed, "wenquanyi_11pt") <= kType4MeaningMaxWidth) {
		return {trimmed, "wenquanyi_11pt"};
	}
	if (epd->MeasureUtf8Width(trimmed, "wenquanyi_9pt") <= kType4MeaningMaxWidth) {
		return {trimmed, "wenquanyi_9pt"};
	}
	return {FitUtf8TextWithEllipsisToWidth(trimmed, kType4MeaningMaxWidth, "wenquanyi_9pt", epd), "wenquanyi_9pt"};
}

int FontLineHeight(const char *font_name) {
	const auto *font = eteacher::font_manager::GetBuiltinFont(font_name ? font_name : "wenquanyi_11pt");
	if (!font || !font->Ready()) {
		return 16;
	}
	return static_cast<int>(font->Header().ascent + font->Header().descent);
}

void ApplyPromptPresentation(int question_type,
		bool visible,
		int dash_width,
		int dash_gap_px,
		int max_lines,
		int dash_black_len,
		int dash_white_len,
		const std::string &prompt,
		const app_ui::Rect &cached_rect,
		app_ui::LabelWidget *line1,
		app_ui::LabelWidget *line2,
		app_ui::LabelWidget *line3,
		app_ui::Widget *dash1,
		app_ui::Widget *dash2,
		app_ui::Widget *dash3,
		CustomEpdDisplay *epd) {
	const bool show = visible;
	const std::string prompt_text = Trim(prompt);

	if (line1) {
		line1->SetVisible(show);
	}
	if (line2) {
		line2->SetVisible(false);
		line2->SetText("");
	}
	if (line3) {
		line3->SetVisible(false);
		line3->SetText("");
	}
	if (dash1) {
		dash1->SetVisible(false);
	}
	if (dash2) {
		dash2->SetVisible(false);
	}
	if (dash3) {
		dash3->SetVisible(false);
	}

	if (!show || !line1) {
		return;
	}

	const app_ui::Rect base_declared_rect = (cached_rect.w > 0 && cached_rect.h > 0)
		? cached_rect
		: line1->DeclaredRect();

	int effective_dash_width = dash_width;
	if (question_type == 5 || question_type == 6 || question_type == 9 || question_type == 10) {
		const int label_width = static_cast<int>(base_declared_rect.w);
		if (label_width > 0) {
			effective_dash_width = label_width;
		}
	}
	if (effective_dash_width <= 0) {
		line1->SetText(prompt_text);
		return;
	}

	const bool center_text_on_dash =
		(question_type == 1 || question_type == 2 || question_type == 3 || question_type == 7 || question_type == 8 ||
		 question_type == 11 || question_type == 12);

	int16_t base_x = base_declared_rect.x;
	if (!center_text_on_dash && base_declared_rect.w > effective_dash_width) {
		base_x = static_cast<int16_t>(base_declared_rect.x + (base_declared_rect.w - effective_dash_width) / 2);
	}

	const DashLineProfile dash_profile = {
		static_cast<int16_t>(dash_black_len),
		static_cast<int16_t>(dash_white_len),
	};
	if (auto *dash = dynamic_cast<DashedLineWidget *>(dash1)) {
		dash->SetProfile(dash_profile);
	}
	if (auto *dash = dynamic_cast<DashedLineWidget *>(dash2)) {
		dash->SetProfile(dash_profile);
	}
	if (auto *dash = dynamic_cast<DashedLineWidget *>(dash3)) {
		dash->SetProfile(dash_profile);
	}

	const char *font_name = line1->FontName();
	const int line_height = std::max(1, FontLineHeight(font_name));
	const app_ui::Rect line_rect = {base_x, base_declared_rect.y, static_cast<int16_t>(effective_dash_width), static_cast<int16_t>(line_height)};
	const int full_width = static_cast<int>(epd ? epd->MeasureUtf8Width(prompt_text, font_name) : 0);

	app_ui::LabelProfile line1_profile = line1->Profile();
	line1_profile.center_text_h = center_text_on_dash;
	line1_profile.center_text_v = false;
	line1->SetProfile(line1_profile);
	if (line2) {
		app_ui::LabelProfile line2_profile = line2->Profile();
		line2_profile.center_text_h = center_text_on_dash;
		line2_profile.center_text_v = false;
		line2->SetProfile(line2_profile);
	}
	if (line3) {
		app_ui::LabelProfile line3_profile = line3->Profile();
		line3_profile.center_text_h = center_text_on_dash;
		line3_profile.center_text_v = false;
		line3->SetProfile(line3_profile);
	}

	const int effective_max_lines = std::max(1, max_lines);
	std::vector<std::string> lines;
	lines.reserve(static_cast<size_t>(effective_max_lines));
	if (prompt_text.empty()) {
		lines.push_back("");
	} else if (full_width <= effective_dash_width) {
		lines.push_back(prompt_text);
	} else {
		size_t consumed = 0;
		for (int line_index = 0; line_index < effective_max_lines && consumed < prompt_text.size(); ++line_index) {
			size_t used_bytes = 0;
			std::string piece = FitUtf8TextToWidth(prompt_text.substr(consumed), effective_dash_width, font_name, epd, &used_bytes);
			if (piece.empty()) {
				break;
			}
			lines.push_back(piece);
			if (used_bytes == 0) {
				break;
			}
			consumed += used_bytes;
		}
		if (lines.empty()) {
			lines.push_back(prompt_text);
		}
	}

	std::array<app_ui::LabelWidget *, 3> line_labels = {line1, line2, line3};
	std::array<app_ui::Widget *, 3> dash_lines = {dash1, dash2, dash3};
	const int16_t line_step = static_cast<int16_t>(line_height + dash_gap_px + 1 + dash_gap_px);

	for (size_t i = 0; i < line_labels.size() && i < lines.size(); ++i) {
		auto *line_label = line_labels[i];
		if (!line_label) {
			continue;
		}
		const int16_t line_y = static_cast<int16_t>(line_rect.y + static_cast<int16_t>(i) * line_step);
		line_label->SetRectInParent({line_rect.x, line_y, static_cast<int16_t>(dash_width), static_cast<int16_t>(line_height)});
		line_label->SetText(lines[i]);
		line_label->SetVisible(true);

		if (auto *dash = dynamic_cast<DashedLineWidget *>(dash_lines[i])) {
			dash->SetRectInParent({line_rect.x, static_cast<int16_t>(line_y + line_height + dash_gap_px),
				static_cast<int16_t>(effective_dash_width), 1});
			dash->SetVisible(true);
		}
	}
}

}  // namespace

MenuMeta WordPracticeApp::GetMenuMeta() const {
	return MenuMeta{"word_practice", "单词练习", "10题过关 Start进入 Select退出"};
}

void WordPracticeApp::OnEnter(AppContext &ctx) {
	const int64_t enter_start_ms = NowMs();
	ctx_ = &ctx;
	ui_ready_ = false;
	root_ = nullptr;
	epd_ = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
	speak_recording_ = false;
	if (epd_) {
		epd_->SetChatMessageListener(this);
	}

	label_question_type_ = nullptr;
	label_correct_count_ = nullptr;
	label_wrong_count_ = nullptr;
	label_alert_ = nullptr;
	label_question_ = nullptr;
	label_asr_result_ = nullptr;
	label_press_aread_ = nullptr;
	label_press_d_skip_ = nullptr;
	bottom_bar_ = nullptr;
	textarea_input_answer_ = nullptr;
	dialog_select_board_ = nullptr;
	image_good_ = nullptr;
	image_bad_ = nullptr;
	image_public_speaker_ = nullptr;
	image_a_ = nullptr;
	image_b_ = nullptr;
	image_c_ = nullptr;
	image_d_ = nullptr;
	image_write_ = nullptr;
	label_a_ = nullptr;
	label_b_ = nullptr;
	label_c_ = nullptr;
	label_d_ = nullptr;
	label_question_line2_ = nullptr;
	label_question_line3_ = nullptr;
	question_dash_line1_ = nullptr;
	question_dash_line2_ = nullptr;
	question_dash_line3_ = nullptr;
	label_asr_line2_ = nullptr;
	label_asr_line3_ = nullptr;
	asr_dash_line1_ = nullptr;
	asr_dash_line2_ = nullptr;
	asr_dash_line3_ = nullptr;
	label_up_ = nullptr;
	label_left_ = nullptr;
	label_down_ = nullptr;
	label_right_ = nullptr;
	label_question_static_rect_ = {};
	label_asr_static_rect_ = {};
	image_public_speaker_static_rect_ = {};

	session_module_.ResetForNewRound(pass_target_questions_);
	selection_module_.ResetProgress();
	current_question_type_ = 1;
	current_choice_ = {};
	type4_left_words_.clear();
	type4_left_audio_filenames_.clear();
	type4_right_words_.clear();
	type4_expected_right_index_.clear();
	type4_selected_right_by_left_.clear();
	type4_selected_left_index_ = 0;
	type4_last_spoken_left_index_ = -1;
	type56_words_.clear();
	type56_dialog_items_.clear();
	type56_selected_index_ = 0;
	type56_grid_cols_ = 1;
	type56_input_answer_.clear();
	type56_show_correct_answer_ = false;
	type56_correct_answer_display_.clear();
	learned_words_this_round_.clear();
	settlement_learned_word_labels_.clear();
	current_audio_path_.clear();
	question_prompt_profile_ = {};
	audio_bundle_entries_.clear();
	audio_bundle_index_loaded_ = false;
	audio_bundle_index_available_ = false;
	CancelQuestionAudioAutoPlay();

	router_.Reset();
	scene_load_id_ = 0;

	if (!LoadUi(ctx)) {
		return;
	}

	InitUiEngine();
	router_.SetActivateFn([this](AppContext &context, size_t /*index*/, const std::string &scene_id) {
		return LoadScene(context, scene_id, ++scene_load_id_);
	});

	if (router_.HasScenes()) {
		(void)router_.Activate(ctx, 0);
		if (bottom_bar_) {
			bottom_bar_->SetText("词库加载中...");
		}
		if (label_alert_) {
			label_alert_->SetText("");
		}
		Render(ctx);
	}

	const int64_t startup_begin_ms = NowMs();
	ESP_LOGW(kTag,
		"startup timing ui_setup_ms=%lld",
		static_cast<long long>(startup_begin_ms - enter_start_ms));
	LoadQuestionPool();
	const int64_t after_pool_ms = NowMs();
	if (!PickNextQuestion()) {
		ctx.board.GetDisplay()->SetChatMessage("system", "WordPractice: 词库为空");
		ESP_LOGW(kTag,
			"startup timing failed total_ms=%lld ui_setup_ms=%lld pool_ms=%lld",
			static_cast<long long>(after_pool_ms - enter_start_ms),
			static_cast<long long>(startup_begin_ms - enter_start_ms),
			static_cast<long long>(after_pool_ms - startup_begin_ms));
		return;
	}
	const int64_t after_pick_ms = NowMs();
	ESP_LOGW(kTag,
		"startup timing total_ms=%lld ui_setup_ms=%lld pool_ms=%lld first_question_ms=%lld",
		static_cast<long long>(after_pick_ms - enter_start_ms),
		static_cast<long long>(startup_begin_ms - enter_start_ms),
		static_cast<long long>(after_pool_ms - startup_begin_ms),
		static_cast<long long>(after_pick_ms - after_pool_ms));

	Render(ctx);
}

void WordPracticeApp::OnExit(AppContext &ctx) {
	(void)ctx;
	if (speak_recording_) {
		AppService::GetInstance().StopListening();
		speak_recording_ = false;
	}
	if (epd_) {
		epd_->SetChatMessageListener(nullptr);
	}
	CancelQuestionAudioAutoPlay();
	audio_bundle_entries_.clear();
	audio_bundle_index_loaded_ = false;
	audio_bundle_index_available_ = false;
	current_audio_path_.clear();
	ctx_ = nullptr;
	ui_ready_ = false;
	root_ = nullptr;
	epd_ = nullptr;
	router_.Reset();
}

void WordPracticeApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
	if (!ui_ready_) {
		return;
	}
	const bool is_speak_type = quiz_module_.IsSpeakType(current_question_type_);
	if (!is_speak_type && !IsClickLike(event)) {
		return;
	}

	if (event.id == AppButton::Start && event.action == ButtonAction::Click && session_module_.IsFinished()) {
		session_module_.ResetForNewRound(pass_target_questions_);
		selection_module_.ResetProgress();
		learned_words_this_round_.clear();
		HideSettlementLearnedWordLabels();
		settlement_learned_word_labels_.clear();
		if (bottom_bar_) {
			bottom_bar_->SetText("新一轮开始");
		}
		if (label_alert_) {
			label_alert_->SetText("");
		}
		PickNextQuestion();
		Render(ctx);
		return;
	}

	if (event.id == AppButton::Start && event.action == ButtonAction::Click && !current_audio_path_.empty() &&
		!((quiz_module_.IsSentenceBuildType(current_question_type_)) || is_speak_type)) {
		if (!PlayAudioFromSd(current_audio_path_)) {
			if (label_alert_) {
				label_alert_->SetText("音频播放失败");
			}
		}
		Render(ctx);
		return;
	}

	if (session_module_.AwaitingNextQuestion()) {
		if (IsNextQuestionTriggerButton(event.id)) {
			session_module_.SetAwaitingNextQuestion(false);
			if (!session_module_.IsFinished()) {
				(void)PickNextQuestion();
			}
		}
		if (session_module_.IsFinished()) {
			ShowSessionSummary();
		}
		Render(ctx);
		return;
	}

	if (current_question_type_ == 4) {
		HandleType4Action(event.id);
	} else if (current_question_type_ >= 7 && current_question_type_ <= 10) {
		HandleSpeakAction(event);
	} else if (current_question_type_ == 5 || current_question_type_ == 6) {
		HandleType56Action(event.id);
	} else {
		HandleAnswer(event.id);
	}

	if (session_module_.IsFinished()) {
		ShowSessionSummary();
	}
	SyncScoreLabels();
	Render(ctx);
}

bool WordPracticeApp::LoadUi(AppContext &ctx) {
	scene_ids_ = app_ui::CollectSceneIds(*kUiDesc);
	if (scene_ids_.empty()) {
		ctx.board.GetDisplay()->SetChatMessage("system", "WordPractice UI: no scenes");
		return false;
	}
	router_.SetScenes(scene_ids_);
	ui_ready_ = true;
	return true;
}

bool WordPracticeApp::LoadScene(AppContext &ctx, const std::string &scene_id, uint16_t scene_index) {
	if (!scene_runtime_.LoadFromDesc(*kUiDesc, scene_id.c_str(), scene_index)) {
		ctx.board.GetDisplay()->SetChatMessage("system", "WordPractice UI: load failed");
		return false;
	}

	auto new_root = scene_runtime_.TakeRoot();
	if (!new_root) {
		ctx.board.GetDisplay()->SetChatMessage("system", "WordPractice UI: take root failed");
		return false;
	}

	root_ = new_root.get();
	BindWidgets(root_);
	ui_engine_.SetRoot(std::move(new_root));
	return true;
}

void WordPracticeApp::InitUiEngine() {
	ui_engine_.Reset();
	ui_engine_.SetEpd(epd_);
}

void WordPracticeApp::Render(AppContext &ctx) {
	if (!ui_ready_) {
		return;
	}
	ui_engine_.RequestRender();

	if (!epd_) {
		std::string msg = "WordPractice\n";
		msg += "Q:" + std::to_string(session_module_.TotalAnswered() + 1) + "/" + std::to_string(session_module_.PassTargetQuestions());
		ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
	}
}

void WordPracticeApp::BindWidgets(app_ui::Widget *root) {
	if (!root) {
		return;
	}

	label_question_type_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelQuestionType));
	label_correct_count_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelCorrectCount));
	label_wrong_count_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelWrongCount));
	label_alert_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelAlert));
	label_question_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelQuestion));
	label_asr_result_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelAsrResult));
	label_press_aread_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelPressARead));
	label_press_d_skip_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelPressDSkip));
	bottom_bar_ = dynamic_cast<app_ui::TextWidget *>(root->FindById(kWidgetBottomBar));
	textarea_input_answer_ = dynamic_cast<app_ui::TextAreaWidget *>(root->FindById(kWidgetTextAreaInputAnswer));
	if (textarea_input_answer_) {
		textarea_input_answer_->SetFocusable(false);
		textarea_input_answer_->SetFocused(false);
		app_ui::TextAreaProfile profile;
		profile.decoration_mode = app_ui::TextAreaProfile::DecorationMode::UnderlineDashed;
		profile.max_lines = 3;
		profile.text_offset_x = 0;
		profile.text_offset_y = 0;
		profile.line_gap_px = 2;
		profile.underline_margin_x = 0;
		profile.underline_segment = 4;
		profile.underline_gap = 1;
		textarea_input_answer_->SetProfile(profile);
	}
	dialog_select_board_ = dynamic_cast<app_ui::DialogWidget *>(root->FindById(kWidgetDialogSelectBoard));
	if (dialog_select_board_) {
		app_ui::DialogProfile profile;
		profile.mode = app_ui::DialogProfile::Mode::Grid;
		profile.grid_rows = 2;
		profile.grid_cols = 0;
		profile.navigation_enabled = false;
		profile.selection_highlight_enabled = true;
		dialog_select_board_->SetProfile(profile);
	}
	image_good_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageGood));
	image_bad_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageBad));
	image_public_speaker_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetPublicSpeaker));
	if (image_public_speaker_) {
		image_public_speaker_static_rect_ = image_public_speaker_->DeclaredRect();
	}
	image_a_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageA));
	image_b_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageB));
	image_c_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageC));
	image_d_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageD));
	image_write_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageWrite));
	image_input_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageInput));
	if (label_question_) {
		label_question_static_rect_ = label_question_->DeclaredRect();
	}
	if (label_asr_result_) {
		label_asr_static_rect_ = label_asr_result_->DeclaredRect();
	}

	label_question_line2_ = nullptr;
	label_question_line3_ = nullptr;
	question_dash_line1_ = nullptr;
	question_dash_line2_ = nullptr;
	question_dash_line3_ = nullptr;
	label_asr_line2_ = nullptr;
	label_asr_line3_ = nullptr;
	asr_dash_line1_ = nullptr;
	asr_dash_line2_ = nullptr;
	asr_dash_line3_ = nullptr;
	if (root_) {
		auto *line2 = dynamic_cast<app_ui::LabelWidget *>(root_->AddChild(std::make_unique<app_ui::LabelWidget>()));
		if (line2) {
			line2->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			line2->SetFontName("wenquanyi_11pt");
			line2->SetVisible(false);
		}
		label_question_line2_ = line2;

		auto *line3 = dynamic_cast<app_ui::LabelWidget *>(root_->AddChild(std::make_unique<app_ui::LabelWidget>()));
		if (line3) {
			line3->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			line3->SetFontName("wenquanyi_11pt");
			line3->SetVisible(false);
		}
		label_question_line3_ = line3;

		auto *dash1 = dynamic_cast<DashedLineWidget *>(root_->AddChild(std::make_unique<DashedLineWidget>()));
		if (dash1) {
			dash1->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			dash1->SetProfile({4, 1});
			dash1->SetVisible(false);
		}
		question_dash_line1_ = dash1;

		auto *dash2 = dynamic_cast<DashedLineWidget *>(root_->AddChild(std::make_unique<DashedLineWidget>()));
		if (dash2) {
			dash2->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			dash2->SetProfile({4, 1});
			dash2->SetVisible(false);
		}
		question_dash_line2_ = dash2;

		auto *dash3 = dynamic_cast<DashedLineWidget *>(root_->AddChild(std::make_unique<DashedLineWidget>()));
		if (dash3) {
			dash3->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			dash3->SetProfile({4, 1});
			dash3->SetVisible(false);
		}
		question_dash_line3_ = dash3;

		auto *asr_line2 = dynamic_cast<app_ui::LabelWidget *>(root_->AddChild(std::make_unique<app_ui::LabelWidget>()));
		if (asr_line2) {
			asr_line2->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			asr_line2->SetFontName("wenquanyi_11pt");
			asr_line2->SetVisible(false);
		}
		label_asr_line2_ = asr_line2;

		auto *asr_line3 = dynamic_cast<app_ui::LabelWidget *>(root_->AddChild(std::make_unique<app_ui::LabelWidget>()));
		if (asr_line3) {
			asr_line3->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			asr_line3->SetFontName("wenquanyi_11pt");
			asr_line3->SetVisible(false);
		}
		label_asr_line3_ = asr_line3;

		auto *asr_dash1 = dynamic_cast<DashedLineWidget *>(root_->AddChild(std::make_unique<DashedLineWidget>()));
		if (asr_dash1) {
			asr_dash1->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			asr_dash1->SetProfile({4, 1});
			asr_dash1->SetVisible(false);
		}
		asr_dash_line1_ = asr_dash1;

		auto *asr_dash2 = dynamic_cast<DashedLineWidget *>(root_->AddChild(std::make_unique<DashedLineWidget>()));
		if (asr_dash2) {
			asr_dash2->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			asr_dash2->SetProfile({4, 1});
			asr_dash2->SetVisible(false);
		}
		asr_dash_line2_ = asr_dash2;

		auto *asr_dash3 = dynamic_cast<DashedLineWidget *>(root_->AddChild(std::make_unique<DashedLineWidget>()));
		if (asr_dash3) {
			asr_dash3->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			asr_dash3->SetProfile({4, 1});
			asr_dash3->SetVisible(false);
		}
		asr_dash_line3_ = asr_dash3;
	}

	label_a_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelA));
	label_b_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelB));
	label_c_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelC));
	label_d_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelD));
	if (!label_a_) label_a_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelA2));
	if (!label_b_) label_b_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelB2));
	if (!label_c_) label_c_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelC2));
	if (!label_d_) label_d_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelD2));
	if (!label_a_) label_a_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelA1));
	if (!label_b_) label_b_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelB1));
	if (!label_c_) label_c_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelC1));
	if (!label_d_) label_d_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelD1));

	label_up_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelUp));
	label_left_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelLeft));
	label_down_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelDown));
	label_right_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelRight));

	auto set_image = [root](uint32_t id, const char *name) {
		auto *img = dynamic_cast<app_ui::ImageWidget *>(root->FindById(id));
		if (img && name && name[0]) {
			img->SetText(name);
		}
	};

	set_image(kWidgetSpeakMic, "word_practice_mike.bin");
	set_image(kWidgetPublicTeacher, "word_practice_teacher.bin");
	set_image(kWidgetPublicCup, "word_practice_cup.bin");
	set_image(kWidgetPublicCorrect, "word_practice_correct.bin");
	set_image(kWidgetPublicWrong, "word_practice_wrong.bin");
	set_image(kWidgetPublicSpeaker, "word_practice_speaker.bin");
	set_image(kWidgetSpeaker1, "word_practice_speaker.bin");
	set_image(kWidgetSpeaker2, "word_practice_speaker.bin");
	set_image(kWidgetSpeaker3, "word_practice_speaker.bin");
	set_image(kWidgetSpeaker4, "word_practice_speaker.bin");
	set_image(kWidgetImageWrite, "word_practice_write.bin");
	set_image(kWidgetImageInput, "word_practice_write.bin");
	if (image_good_) image_good_->SetText("word_practice_good.bin");
	if (image_bad_) image_bad_->SetText("word_practice_bad.bin");
	if (image_good_) image_good_->SetVisible(false);
	if (image_bad_) image_bad_->SetVisible(false);
	if (image_write_) image_write_->SetVisible(false);
	if (image_input_) image_input_->SetVisible(false);
	if (image_a_) {
		app_ui::ImageProfile profile = image_a_->Profile();
		profile.draw_border = true;
		profile.draw_border_on_content = true;
		profile.border_thickness = 2;
		profile.content_inset = 2;
		image_a_->SetProfile(profile);
	}
	if (image_b_) {
		app_ui::ImageProfile profile = image_b_->Profile();
		profile.draw_border = true;
		profile.draw_border_on_content = true;
		profile.border_thickness = 2;
		profile.content_inset = 2;
		image_b_->SetProfile(profile);
	}
	if (image_c_) {
		app_ui::ImageProfile profile = image_c_->Profile();
		profile.draw_border = true;
		profile.draw_border_on_content = true;
		profile.border_thickness = 2;
		profile.content_inset = 2;
		image_c_->SetProfile(profile);
	}
	if (image_d_) {
		app_ui::ImageProfile profile = image_d_->Profile();
		profile.draw_border = true;
		profile.draw_border_on_content = true;
		profile.border_thickness = 2;
		profile.content_inset = 2;
		image_d_->SetProfile(profile);
	}

	if (label_question_type_) label_question_type_->SetFontName("wenquanyi_11pt");
	if (label_correct_count_) label_correct_count_->SetFontName("wenquanyi_11pt");
	if (label_wrong_count_) label_wrong_count_->SetFontName("wenquanyi_11pt");
	if (label_alert_) label_alert_->SetFontName("wenquanyi_11pt");
	if (label_question_) label_question_->SetFontName("wenquanyi_11pt");
	if (label_asr_result_) label_asr_result_->SetFontName("wenquanyi_11pt");
	if (label_press_aread_) label_press_aread_->SetFontName("wenquanyi_11pt");
	if (label_press_d_skip_) label_press_d_skip_->SetFontName("wenquanyi_11pt");
	if (label_press_aread_) {
		app_ui::LabelProfile profile = label_press_aread_->Profile();
		profile.draw_border = true;
		profile.draw_rounded_border = true;
		profile.corner_radius = 12;
		profile.center_text_h = true;
		profile.center_text_v = true;
		profile.focus_invert = false;
		label_press_aread_->SetProfile(profile);
		label_press_aread_->SetFocused(false);
	}
	if (label_press_d_skip_) {
		app_ui::LabelProfile profile = label_press_d_skip_->Profile();
		profile.draw_border = true;
		profile.draw_rounded_border = true;
		profile.corner_radius = 12;
		profile.center_text_h = true;
		profile.center_text_v = true;
		profile.focus_invert = false;
		label_press_d_skip_->SetProfile(profile);
		label_press_d_skip_->SetFocused(false);
	}
	if (label_a_) label_a_->SetFontName("wenquanyi_11pt");
	if (label_b_) label_b_->SetFontName("wenquanyi_11pt");
	if (label_c_) label_c_->SetFontName("wenquanyi_11pt");
	if (label_d_) label_d_->SetFontName("wenquanyi_11pt");
	if (label_up_) label_up_->SetFontName("wenquanyi_11pt");
	if (label_left_) label_left_->SetFontName("wenquanyi_11pt");
	if (label_down_) label_down_->SetFontName("wenquanyi_11pt");
	if (label_right_) label_right_->SetFontName("wenquanyi_11pt");
	if (label_question_) {
		label_question_->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
	}
	if (image_public_speaker_) {
		image_public_speaker_->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
	}
}

void WordPracticeApp::LoadQuestionPool() {
	const int64_t load_start_ms = NowMs();
	if (!eteacher::database_manager::EnsureSqliteRuntimeReady(kTag)) {
		ESP_LOGE(kTag, "sqlite runtime init failed");
		return;
	}
	(void)eteacher::database_manager::EnsureSqliteSdMounted(kTag);
	WP_APP_DBLOGW(kTag,
		"load question pool start review_target=%d new_target=%d requested_total=%d",
		word_selection_config_.review_word_count,
		word_selection_config_.new_word_count,
		word_selection_config_.TotalCount());
	const int64_t select_words_start_ms = NowMs();
	const std::vector<word_practice::SelectedWord> selected_words =
		selection_module_.SelectWordsFromVocabulary(word_selection_config_, kDefaultUserId);
	const int64_t select_words_end_ms = NowMs();
	WP_APP_DBLOGW(kTag, "load question pool selected_words=%d", static_cast<int>(selected_words.size()));
	const int64_t build_pool_start_ms = NowMs();
	std::vector<QuestionData> question_pool = BuildVocabularyQuestionPool(selected_words, enable_speak_questions_);
	ESP_LOGW(kTag,
		"question pool speak switch enabled=%d built=%d",
		enable_speak_questions_ ? 1 : 0,
		static_cast<int>(question_pool.size()));
	const int64_t build_pool_end_ms = NowMs();
	std::array<int, 13> pool_type_count = {};
	for (const auto &question : question_pool) {
		if (question.type >= 1 && question.type <= 12) {
			++pool_type_count[static_cast<size_t>(question.type)];
		}
	}
	WP_DIAG_LOGW(kTag,
		"load question pool built=%d by type t1=%d t2=%d t3=%d t4=%d t5=%d t6=%d t7=%d t8=%d t9=%d t10=%d t11=%d t12=%d",
		static_cast<int>(question_pool.size()),
		pool_type_count[1],
		pool_type_count[2],
		pool_type_count[3],
		pool_type_count[4],
		pool_type_count[5],
		pool_type_count[6],
		pool_type_count[7],
		pool_type_count[8],
		pool_type_count[9],
		pool_type_count[10],
		pool_type_count[11],
		pool_type_count[12]);
	selection_module_.LoadQuestionPool(std::move(question_pool));
	WP_APP_DBLOGW(kTag, "vocabulary question pool loaded: %d", static_cast<int>(selection_module_.QuestionPool().size()));
	ESP_LOGW(kTag,
		"startup timing load_question_pool total_ms=%lld select_words_ms=%lld build_pool_ms=%lld selected_words=%d question_pool=%d",
		static_cast<long long>(build_pool_end_ms - load_start_ms),
		static_cast<long long>(select_words_end_ms - select_words_start_ms),
		static_cast<long long>(build_pool_end_ms - build_pool_start_ms),
		static_cast<int>(selected_words.size()),
		static_cast<int>(selection_module_.QuestionPool().size()));
}

bool WordPracticeApp::PickNextQuestion() {
	if (selection_module_.Empty()) {
		return false;
	}

	if (session_module_.IsFinished()) {
		return true;
	}

	const int current_level = result_module_.ProgressDao().QueryCurrentLevel();
	auto learned_provider = [this](const QuestionData &question, const std::string &textbook) {
		return result_module_.ProgressDao().QueryLearned(question.id, textbook);
	};

	const auto result = selection_module_.SelectNext(
		session_module_.TotalAnswered(),
		current_level,
		question_selection_strategy_,
		learned_provider);
	WP_APP_DBLOGW(kTag,
		"pick next question answered=%d current_level=%d strategy=%d has_value=%d selected_index=%d",
		session_module_.TotalAnswered(),
		current_level,
		static_cast<int>(question_selection_strategy_),
		result.has_value ? 1 : 0,
		static_cast<int>(result.selected_index));
	if (!result.has_value) {
		return false;
	}
	if (const QuestionData *selected = selection_module_.GetQuestion(result.selected_index)) {
		WP_APP_DBLOGW(kTag,
			"pick next question selected type=%d id=%d stage=%s answer=%s",
			selected->type,
			selected->id,
			selected->stage.c_str(),
			selected->answer.c_str());
	}

	ESP_LOGI(kTag, "pick question strategy=%s",
		result.used_strategy == QuestionSelectionStrategy::LegacyAdaptive ? "legacy_adaptive" : "type_cycle_random");
	CommitSelectedQuestion(result.selected_index);
	return true;
}

bool WordPracticeApp::PickNextQuestionByLegacyAdaptive() {
	return false;
}

bool WordPracticeApp::PickNextQuestionByTypeCycleRandom() {
	return false;
}

void WordPracticeApp::CommitSelectedQuestion(size_t index) {
	session_module_.SetCurrentQuestionIndex(index);
	const QuestionData *question = selection_module_.GetQuestion(index);
	if (question) {
		current_question_type_ = question->type;
	}
	PresentCurrentQuestion();
}

void WordPracticeApp::PresentCurrentQuestion() {
	const int64_t present_start_ms = NowMs();
	const QuestionData *question = selection_module_.GetQuestion(session_module_.CurrentQuestionIndex());
	if (!question) {
		return;
	}
	HideSettlementLearnedWordLabels();
	const auto &q = *question;
	const bool is_type56 = (q.type == 5 || q.type == 6);

	const std::string scene = SelectSceneIdByType(q.type);
	if (!scene.empty() && ctx_ && (!router_.HasScenes() || router_.CurrentId() != scene)) {
		size_t index = 0;
		for (size_t i = 0; i < scene_ids_.size(); ++i) {
			if (scene_ids_[i] == scene) {
				index = i;
				break;
			}
		}
		(void)router_.Activate(*ctx_, index);
	}

	current_choice_ = BuildChoiceState(q);
	current_audio_path_ = BuildQuestionAudioPath(current_choice_.audio_filename);
	if (!current_audio_path_.empty()) {
		ScheduleQuestionAudioAutoPlay();
	} else {
		CancelQuestionAudioAutoPlay();
	}

	if (label_question_type_) {
		label_question_type_->SetText(TypeTitle(q.type));
	}
	if (label_correct_count_) {
		label_correct_count_->SetText(std::to_string(session_module_.CorrectCount()));
	}
	if (label_wrong_count_) {
		label_wrong_count_->SetText(std::to_string(session_module_.WrongCount()));
	}
	if (label_alert_) {
		label_alert_->SetText("");
	}
	if (bottom_bar_) {
		bottom_bar_->SetText(TypeInstruction(q.type));
	}
	UpdateQuestionPromptPresentation(q.type, q.type == 6 ? "" : current_choice_.prompt);
	if (label_asr_result_) {
		if (q.type >= 7 && q.type <= 10) {
			UpdateAsrResultPresentation(q.type, "");
		} else {
			label_asr_result_->SetText("");
			label_asr_result_->SetVisible(false);
			if (label_asr_line2_) {
				label_asr_line2_->SetVisible(false);
				label_asr_line2_->SetText("");
			}
			if (label_asr_line3_) {
				label_asr_line3_->SetVisible(false);
				label_asr_line3_->SetText("");
			}
			if (asr_dash_line1_) asr_dash_line1_->SetVisible(false);
			if (asr_dash_line2_) asr_dash_line2_->SetVisible(false);
			if (asr_dash_line3_) asr_dash_line3_->SetVisible(false);
		}
	}
	if (q.type >= 7 && q.type <= 10) {
		if (label_press_aread_) {
			label_press_aread_->SetText("按住Start开始朗读");
		}
		if (label_press_d_skip_) {
			label_press_d_skip_->SetText("按D键跳过");
		}
	} else {
		if (label_press_aread_) {
			label_press_aread_->SetText("");
		}
		if (label_press_d_skip_) {
			label_press_d_skip_->SetText("");
		}
	}
	if (image_write_) {
		image_write_->SetText("word_practice_write.bin");
		image_write_->SetVisible(q.type == 5 || q.type == 6);
	}
	if (image_input_) {
		image_input_->SetText("word_practice_write.bin");
		image_input_->SetVisible(q.type >= 7 && q.type <= 10);
	}

	type56_words_.clear();
	type56_dialog_items_.clear();
	type56_input_answer_.clear();
	type56_show_correct_answer_ = false;
	type56_correct_answer_display_.clear();
	type56_selected_index_ = 0;
	type4_left_words_.clear();
	type4_left_audio_filenames_.clear();
	type4_right_words_.clear();
	type4_expected_right_index_.clear();
	type4_selected_right_by_left_.clear();
	type4_selected_left_index_ = 0;
	type4_last_spoken_left_index_ = -1;
	session_module_.SetAwaitingNextQuestion(false);
	if (is_type56) {
		type56_words_ = current_choice_.hints;
		type56_dialog_items_ = type56_words_;
	}
	RefreshType56Widgets();

	if (q.type == 4) {
		type4_left_words_ = current_choice_.pair_left;
		type4_left_audio_filenames_ = current_choice_.pair_left_audio;
		type4_right_words_ = current_choice_.pair_right;
		if (type4_left_words_.size() < 4) {
			type4_left_words_.resize(4);
		}
		if (type4_left_audio_filenames_.size() < 4) {
			type4_left_audio_filenames_.resize(4);
		}
		if (type4_right_words_.size() < 4) {
			type4_right_words_.resize(4);
		}

		type4_expected_right_index_.assign(4, -1);
		type4_selected_right_by_left_.assign(4, -1);

		const std::string expected_pairs = current_choice_.expected.empty() ? q.answer : current_choice_.expected;
		size_t start = 0;
		while (start < expected_pairs.size()) {
			size_t end = expected_pairs.find(';', start);
			if (end == std::string::npos) {
				end = expected_pairs.size();
			}
			std::string pair = Trim(expected_pairs.substr(start, end - start));
			start = end + 1;
			if (pair.empty()) {
				continue;
			}

			size_t sep = pair.find('-');
			if (sep == std::string::npos) {
				sep = pair.find(':');
			}
			if (sep == std::string::npos) {
				sep = pair.find('=');
			}
			if (sep == std::string::npos) {
				continue;
			}

			const std::string left_word = NormalizePairWord(pair.substr(0, sep));
			const std::string right_word = NormalizePairWord(pair.substr(sep + 1));
			if (left_word.empty() || right_word.empty()) {
				continue;
			}

			int left_index = -1;
			for (int i = 0; i < 4; ++i) {
				if (NormalizePairWord(type4_left_words_[static_cast<size_t>(i)]) == left_word) {
					left_index = i;
					break;
				}
			}

			int right_index = -1;
			for (int i = 0; i < 4; ++i) {
				if (NormalizePairWord(type4_right_words_[static_cast<size_t>(i)]) == right_word) {
					right_index = i;
					break;
				}
			}

			if (left_index >= 0 && right_index >= 0) {
				type4_expected_right_index_[static_cast<size_t>(left_index)] = right_index;
			}
		}

		type4_selected_left_index_ = 0;
		RefreshType4Widgets();
	} else {
		const bool uses_choice_option_text =
			(q.type == 1 || q.type == 2 || q.type == 3 || q.type == 11 || q.type == 12);
		if (q.type == 1) {
			auto set_option_image = [this](app_ui::ImageWidget *widget, const std::vector<std::string> &images, size_t index) {
				if (!widget) {
					return;
				}
				const std::string image_name = (index < images.size()) ? Trim(images[index]) : "";
				if (!image_name.empty()) {
					widget->SetText(ResolveBundledImagePath(image_name));
				} else {
					widget->SetText("");
				}
			};
			set_option_image(image_a_, current_choice_.option_images, 0);
			set_option_image(image_b_, current_choice_.option_images, 1);
			set_option_image(image_c_, current_choice_.option_images, 2);
			set_option_image(image_d_, current_choice_.option_images, 3);
		}

		if (label_a_) {
			const std::string option = current_choice_.options.size() > 0 ? current_choice_.options[0] : "";
			const std::string option_key = current_choice_.option_keys.size() > 0 ? current_choice_.option_keys[0] : "A";
			label_a_->SetText(q.type == 1 ? (option_key.empty() ? "A" : option_key)
								 : (uses_choice_option_text ? FormatOptionWithKey(option_key, option)
											  : (option.empty() ? "A" : option)));
			label_a_->SetFocused(false);
		}
		if (label_b_) {
			const std::string option = current_choice_.options.size() > 1 ? current_choice_.options[1] : "";
			const std::string option_key = current_choice_.option_keys.size() > 1 ? current_choice_.option_keys[1] : "B";
			label_b_->SetText(q.type == 1 ? (option_key.empty() ? "B" : option_key)
								 : (uses_choice_option_text ? FormatOptionWithKey(option_key, option)
											  : (option.empty() ? "B" : option)));
			label_b_->SetFocused(false);
		}
		if (label_c_) {
			const std::string option = current_choice_.options.size() > 2 ? current_choice_.options[2] : "";
			const std::string option_key = current_choice_.option_keys.size() > 2 ? current_choice_.option_keys[2] : "C";
			label_c_->SetText(q.type == 1 ? (option_key.empty() ? "C" : option_key)
								 : (uses_choice_option_text ? FormatOptionWithKey(option_key, option)
											  : (option.empty() ? "C" : option)));
			label_c_->SetFocused(false);
		}
		if (label_d_) {
			const std::string option = current_choice_.options.size() > 3 ? current_choice_.options[3] : "";
			const std::string option_key = current_choice_.option_keys.size() > 3 ? current_choice_.option_keys[3] : "D";
			label_d_->SetText(q.type == 1 ? (option_key.empty() ? "D" : option_key)
								 : (uses_choice_option_text ? FormatOptionWithKey(option_key, option)
											  : (option.empty() ? "D" : option)));
			label_d_->SetFocused(false);
		}

		if (label_up_) {
			label_up_->SetText(current_choice_.options.size() > 0 ? current_choice_.options[0] : "上");
			label_up_->SetFocused(false);
		}
		if (label_left_) {
			label_left_->SetText(current_choice_.options.size() > 1 ? current_choice_.options[1] : "左");
			label_left_->SetFocused(false);
		}
		if (label_down_) {
			label_down_->SetText(current_choice_.options.size() > 2 ? current_choice_.options[2] : "下");
			label_down_->SetFocused(false);
		}
		if (label_right_) {
			label_right_->SetText(current_choice_.options.size() > 3 ? current_choice_.options[3] : "右");
			label_right_->SetFocused(false);
		}
	}

	if (image_good_) image_good_->SetVisible(false);
	if (image_bad_) image_bad_->SetVisible(false);
	ESP_LOGW(kTag,
		"startup timing present_question type=%d question_id=%d present_ms=%lld audio=%d scene=%s",
		q.type,
		q.id,
		static_cast<long long>(NowMs() - present_start_ms),
		current_audio_path_.empty() ? 0 : 1,
		scene.c_str());
}

void WordPracticeApp::ShowSessionSummary() {
	if (!label_question_) {
		return;
	}
	SyncScoreLabels();
	const word_practice::Summary summary = result_module_.BuildSummary(session_module_);
	const bool pass = summary.passed;

	auto hide_widget = [](app_ui::Widget *widget) {
		if (!widget) {
			return;
		}
		widget->SetVisible(false);
	};

	// 结算页仅展示 public 控件内容，隐藏题型专属控件。
	hide_widget(image_a_);
	hide_widget(image_b_);
	hide_widget(image_c_);
	hide_widget(image_d_);
	hide_widget(label_a_);
	hide_widget(label_b_);
	hide_widget(label_c_);
	hide_widget(label_d_);
	hide_widget(label_up_);
	hide_widget(label_left_);
	hide_widget(label_down_);
	hide_widget(label_right_);
	hide_widget(textarea_input_answer_);
	hide_widget(dialog_select_board_);
	hide_widget(image_write_);
	hide_widget(image_input_);
	hide_widget(label_press_aread_);
	hide_widget(label_press_d_skip_);
	hide_widget(label_asr_result_);
	hide_widget(label_asr_line2_);
	hide_widget(label_asr_line3_);
	hide_widget(asr_dash_line1_);
	hide_widget(asr_dash_line2_);
	hide_widget(asr_dash_line3_);
	if (root_) {
		auto *speak_mic = dynamic_cast<app_ui::ImageWidget *>(root_->FindById(kWidgetSpeakMic));
		if (speak_mic) {
			speak_mic->SetVisible(false);
		}
	}
	hide_widget(label_question_line2_);
	hide_widget(label_question_line3_);
	hide_widget(question_dash_line1_);
	hide_widget(question_dash_line2_);
	hide_widget(question_dash_line3_);

	if (label_question_type_) {
		label_question_type_->SetText("结算");
	}

	if (label_question_) {
		ApplyPromptPresentation(
			10,
			true,
			label_question_static_rect_.w > 0 ? static_cast<int>(label_question_static_rect_.w) : kPromptDashWidthLong,
			kPromptDashGapPx,
			3,
			4,
			1,
			summary.summary_text,
			label_question_static_rect_,
			label_question_,
			label_question_line2_,
			label_question_line3_,
			question_dash_line1_,
			question_dash_line2_,
			question_dash_line3_,
			epd_);
	}

	if (image_public_speaker_) {
		image_public_speaker_->SetText("word_practice_speaker.bin");
		image_public_speaker_->SetVisible(true);
	}

	if (image_good_) {
		image_good_->SetText("word_practice_good.bin");
		image_good_->SetVisible(pass);
	}
	if (image_bad_) {
		image_bad_->SetText("word_practice_bad.bin");
		image_bad_->SetVisible(!pass);
	}

	if (label_alert_) {
		label_alert_->SetText(pass ? "恭喜过关" : "未过关，请继续练习");
	}
	RenderSettlementLearnedWordLabels();
	if (bottom_bar_) {
		bottom_bar_->SetText(pass ? "Start继续下一轮" : "Start重开本轮");
	}
}

void WordPracticeApp::SyncScoreLabels() {
	if (label_correct_count_) {
		label_correct_count_->SetText(std::to_string(session_module_.CorrectCount()));
	}
	if (label_wrong_count_) {
		label_wrong_count_->SetText(std::to_string(session_module_.WrongCount()));
	}
}

void WordPracticeApp::AddLearnedWordsFromText(const std::string &text) {
	std::vector<std::string> words = NormalizeSentenceWordsLower(text);
	if (words.empty()) {
		words = SplitHintWords(text);
	}
	for (const auto &word : words) {
		const std::string token = Trim(word);
		if (token.empty()) {
			continue;
		}
		const auto it = std::find(learned_words_this_round_.begin(), learned_words_this_round_.end(), token);
		if (it == learned_words_this_round_.end()) {
			learned_words_this_round_.push_back(token);
		}
	}
}

void WordPracticeApp::HideSettlementLearnedWordLabels() {
	for (auto *label : settlement_learned_word_labels_) {
		if (label) {
			label->SetVisible(false);
			label->SetText("");
		}
	}
}

void WordPracticeApp::RenderSettlementLearnedWordLabels() {
	HideSettlementLearnedWordLabels();
	if (!root_ || learned_words_this_round_.empty()) {
		return;
	}

	const app_ui::Rect question_rect = (label_question_static_rect_.w > 0 && label_question_static_rect_.h > 0)
		? label_question_static_rect_
		: (label_question_ ? label_question_->DeclaredRect() : app_ui::Rect{});
	if (question_rect.w <= 0) {
		return;
	}

	const app_ui::Rect alert_rect = label_alert_ ? label_alert_->DeclaredRect() : app_ui::Rect{0, 264, 0, 20};
	const int16_t start_x = question_rect.x;
	const int16_t max_x = static_cast<int16_t>(question_rect.x + question_rect.w);
	const int16_t max_y = static_cast<int16_t>(alert_rect.y > 0 ? alert_rect.y - 2 : 262);
	const char *font_name = label_question_ ? label_question_->FontName() : "wenquanyi_11pt";
	const int16_t line_height = static_cast<int16_t>(std::max(14, FontLineHeight(font_name)));

	int16_t x = start_x;
	int16_t y = static_cast<int16_t>(question_rect.y + question_rect.h + 6);
	int used = 0;

	for (const auto &word : learned_words_this_round_) {
		if (word.empty()) {
			continue;
		}
		const int16_t word_w = static_cast<int16_t>(std::max(20, static_cast<int>(epd_ ? epd_->MeasureUtf8Width(word, font_name) : static_cast<int>(word.size() * 8))));
		if (x + word_w > max_x) {
			x = start_x;
			y = static_cast<int16_t>(y + line_height + 2);
		}
		if (y + line_height > max_y) {
			break;
		}

		if (used >= static_cast<int>(settlement_learned_word_labels_.size())) {
			auto *label = dynamic_cast<app_ui::LabelWidget *>(root_->AddChild(std::make_unique<app_ui::LabelWidget>()));
			if (!label) {
				break;
			}
			label->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			label->SetFontName(font_name ? font_name : "wenquanyi_11pt");
			settlement_learned_word_labels_.push_back(label);
		}

		auto *word_label = settlement_learned_word_labels_[static_cast<size_t>(used)];
		if (word_label) {
			word_label->SetRectInParent({x, y, word_w, line_height});
			word_label->SetText(word);
			word_label->SetVisible(true);
		}
		++used;
		x = static_cast<int16_t>(x + word_w + 40);
	}
}

void WordPracticeApp::UpdateQuestionPromptPresentation(int question_type, const std::string &prompt) {
	question_prompt_profile_ = BuildQuestionPromptProfile(question_type);
	const bool show = question_prompt_profile_.visible;
	if (image_public_speaker_) {
		image_public_speaker_->SetVisible(show);
	}
	ApplyPromptPresentation(
		question_type,
		show,
		question_prompt_profile_.dash_width,
		question_prompt_profile_.dash_gap_px,
		question_prompt_profile_.max_lines,
		question_prompt_profile_.dash_black_len,
		question_prompt_profile_.dash_white_len,
		prompt,
		label_question_static_rect_,
		label_question_,
		label_question_line2_,
		label_question_line3_,
		question_dash_line1_,
		question_dash_line2_,
		question_dash_line3_,
		epd_);
}

void WordPracticeApp::UpdateAsrResultPresentation(int question_type, const std::string &result_text) {
	const QuestionPromptProfile profile = BuildQuestionPromptProfile(question_type);
	ApplyPromptPresentation(
		question_type,
		profile.visible,
		profile.dash_width,
		profile.dash_gap_px,
		profile.max_lines,
		profile.dash_black_len,
		profile.dash_white_len,
		result_text,
		label_asr_static_rect_,
		label_asr_result_,
		label_asr_line2_,
		label_asr_line3_,
		asr_dash_line1_,
		asr_dash_line2_,
		asr_dash_line3_,
		epd_);
}

WordPracticeApp::QuestionPromptProfile WordPracticeApp::BuildQuestionPromptProfile(int question_type) const {
	QuestionPromptProfile profile;
	profile.dash_gap_px = kPromptDashGapPx;
	profile.max_lines = 2;
	profile.dash_black_len = 4;
	profile.dash_white_len = 1;

	switch (question_type) {
		case 1:
		case 2:
		case 3:
		case 11:
		case 12:
		case 7:
		case 8:
			profile.visible = true;
			profile.dash_width = kPromptDashWidthShort;
			break;
		case 5:
		case 6:
			profile.visible = true;
			profile.dash_width = kPromptDashWidthLong;
			profile.max_lines = 3;
			break;
		case 9:
		case 10:
			profile.visible = true;
			profile.dash_width = kPromptDashWidthLong;
			profile.max_lines = 3;
			break;
		default:
			profile.visible = false;
			profile.dash_width = 0;
			break;
	}

	return profile;
}

void WordPracticeApp::HandleAnswer(AppButton button) {
	const QuestionData *question = selection_module_.GetQuestion(session_module_.CurrentQuestionIndex());
	if (session_module_.IsFinished() || !question) {
		return;
	}
	const auto &q = *question;

	const std::string picked = ButtonToken(button);
	if (picked.empty()) {
		return;
	}

	std::string answer_text = Trim(current_choice_.expected);
	const std::string expected_token = NormalizeAnswerToken(current_choice_.expected);
	if (expected_token.size() == 1 && expected_token[0] >= 'A' && expected_token[0] <= 'D') {
		const size_t option_index = static_cast<size_t>(expected_token[0] - 'A');
		if (option_index < current_choice_.options.size() && !current_choice_.options[option_index].empty()) {
			answer_text = current_choice_.options[option_index];
		} else {
			answer_text = expected_token;
		}
	}
	if (answer_text.empty()) {
		answer_text = Trim(q.answer);
	}

	const bool correct = NormalizeAnswerToken(picked) == NormalizeAnswerToken(current_choice_.expected);
	session_module_.RecordAnswer(correct);
	if (correct) {
		AddLearnedWordsFromText(answer_text);
		if (image_good_) {
			image_good_->SetText("word_practice_good.bin");
			image_good_->SetVisible(true);
		}
		if (image_bad_) {
			image_bad_->SetVisible(false);
		}
		if (label_alert_) {
			label_alert_->SetText("答对了，太棒了！");
		}
		if (bottom_bar_) {
			bottom_bar_->SetText("按方向键或ABCD进入下一题");
		}
	} else {
		if (image_bad_) {
			image_bad_->SetText("word_practice_bad.bin");
			image_bad_->SetVisible(true);
		}
		if (image_good_) {
			image_good_->SetVisible(false);
		}
		if (label_alert_) {
			label_alert_->SetText("答错了，正确答案是" + answer_text);
		}
		if (bottom_bar_) {
			bottom_bar_->SetText("按方向键或ABCD进入下一题");
		}
	}

	SaveAnswerStats(q, correct);
}

void WordPracticeApp::RefreshType4Widgets() {
	auto setup_type4_label = [](app_ui::LabelWidget *label, const char *left_prefix) {
		if (!label) {
			return;
		}
		app_ui::LabelProfile profile = label->Profile();
		profile.draw_border = true;
		profile.draw_rounded_border = true;
		profile.corner_radius = 12;
		profile.center_text_h = true;
		profile.center_text_v = true;
		profile.draw_left_prefix = (left_prefix != nullptr && left_prefix[0] != '\0');
		profile.center_text_full_rect = profile.draw_left_prefix;
		profile.left_prefix = profile.draw_left_prefix ? std::string(left_prefix) : std::string();
		profile.left_prefix_gap = 4;
		profile.text_offset_x = profile.draw_left_prefix ? 10 : 0;
		label->SetProfile(profile);
	};

	setup_type4_label(label_up_, nullptr);
	setup_type4_label(label_left_, nullptr);
	setup_type4_label(label_down_, nullptr);
	setup_type4_label(label_right_, nullptr);
	setup_type4_label(label_a_, "A");
	setup_type4_label(label_b_, "B");
	setup_type4_label(label_c_, "C");
	setup_type4_label(label_d_, "D");

	auto set_text_and_focus = [](app_ui::LabelWidget *label, const std::string &text, bool focused) {
		if (!label) {
			return;
		}
		label->SetText(text);
		label->SetFocused(focused);
	};

	std::array<app_ui::LabelWidget *, 4> left_labels = {label_up_, label_left_, label_down_, label_right_};
	std::array<app_ui::LabelWidget *, 4> right_labels = {label_a_, label_b_, label_c_, label_d_};

	for (int i = 0; i < 4; ++i) {
		const std::string left_text = (i < static_cast<int>(type4_left_words_.size())) ? type4_left_words_[static_cast<size_t>(i)] : "";
		const bool left_selected = (i == type4_selected_left_index_);
		set_text_and_focus(left_labels[static_cast<size_t>(i)], left_text, left_selected);
	}

	std::array<bool, 4> right_matched = {false, false, false, false};
	for (int i = 0; i < static_cast<int>(type4_selected_right_by_left_.size()) && i < 4; ++i) {
		const int matched = type4_selected_right_by_left_[static_cast<size_t>(i)];
		if (matched >= 0 && matched < 4) {
			right_matched[static_cast<size_t>(matched)] = true;
		}
	}

	for (int i = 0; i < 4; ++i) {
		const std::string right_text = (i < static_cast<int>(type4_right_words_.size())) ? type4_right_words_[static_cast<size_t>(i)] : "";
		auto *right_label = right_labels[static_cast<size_t>(i)];
		if (!right_label) {
			continue;
		}
		const Type4MeaningTextLayout layout = LayoutType4MeaningText(right_text, epd_);
		right_label->SetFontName(layout.font_name);
		set_text_and_focus(right_label, layout.text, right_matched[static_cast<size_t>(i)]);
	}

	PlayType4FocusedWordAudioIfNeeded();
}

void WordPracticeApp::PlayType4FocusedWordAudioIfNeeded() {
	if (current_question_type_ != 4) {
		return;
	}
	if (type4_selected_left_index_ < 0 ||
		type4_selected_left_index_ >= static_cast<int>(type4_left_audio_filenames_.size()) ||
		type4_selected_left_index_ == type4_last_spoken_left_index_) {
		return;
	}

	const std::string audio_filename = Trim(type4_left_audio_filenames_[static_cast<size_t>(type4_selected_left_index_)]);
	if (audio_filename.empty()) {
		type4_last_spoken_left_index_ = type4_selected_left_index_;
		return;
	}

	if (PlayAudioFromSd(BuildQuestionAudioPath(audio_filename))) {
		type4_last_spoken_left_index_ = type4_selected_left_index_;
	}
}

void WordPracticeApp::HandleType4Action(AppButton button) {
	const QuestionData *question = selection_module_.GetQuestion(session_module_.CurrentQuestionIndex());
	if (session_module_.IsFinished() || !question) {
		return;
	}
	const auto &q = *question;

	const int left_count = static_cast<int>(type4_left_words_.size());
	if (left_count <= 0) {
		return;
	}

	if (button == AppButton::Up) {
		type4_selected_left_index_ = (type4_selected_left_index_ + left_count - 1) % left_count;
		RefreshType4Widgets();
		return;
	}
	if (button == AppButton::Down) {
		type4_selected_left_index_ = (type4_selected_left_index_ + 1) % left_count;
		RefreshType4Widgets();
		return;
	}

	const std::string picked = ButtonToken(button);
	if (picked.empty()) {
		return;
	}

	const int selected_left = type4_selected_left_index_;
	if (selected_left < 0 || selected_left >= left_count) {
		return;
	}
	if (selected_left >= static_cast<int>(type4_selected_right_by_left_.size())) {
		return;
	}
	if (type4_selected_right_by_left_[static_cast<size_t>(selected_left)] >= 0) {
		if (bottom_bar_) {
			bottom_bar_->SetText("该单词已匹配，请继续选择未匹配单词");
		}
		return;
	}

	const std::string picked_token = NormalizeAnswerToken(picked);
	if (picked_token.size() != 1 || picked_token[0] < 'A' || picked_token[0] > 'D') {
		return;
	}
	const int picked_right = static_cast<int>(picked_token[0] - 'A');

	int expected_right = -1;
	if (selected_left >= 0 && selected_left < static_cast<int>(type4_expected_right_index_.size())) {
		expected_right = type4_expected_right_index_[static_cast<size_t>(selected_left)];
	}

	if (picked_right == expected_right && expected_right >= 0 && expected_right < 4) {
		type4_selected_right_by_left_[static_cast<size_t>(selected_left)] = picked_right;
		RefreshType4Widgets();

		bool all_matched = true;
		for (int i = 0; i < left_count && i < static_cast<int>(type4_selected_right_by_left_.size()); ++i) {
			if (type4_selected_right_by_left_[static_cast<size_t>(i)] < 0) {
				all_matched = false;
				break;
			}
		}

		if (all_matched) {
			session_module_.RecordAnswer(true);
			for (const auto &word : type4_left_words_) {
				AddLearnedWordsFromText(word);
			}
			if (image_good_) {
				image_good_->SetText("word_practice_good.bin");
				image_good_->SetVisible(true);
			}
			if (image_bad_) {
				image_bad_->SetVisible(false);
			}
			if (label_alert_) {
				label_alert_->SetText("答对了，太棒了！");
			}
			if (bottom_bar_) {
				bottom_bar_->SetText("按方向键或ABCD进入下一题");
			}

			SaveAnswerStats(q, true);
			return;
		}

		for (int i = 1; i <= left_count; ++i) {
			const int next = (selected_left + i) % left_count;
			if (next < static_cast<int>(type4_selected_right_by_left_.size()) &&
				type4_selected_right_by_left_[static_cast<size_t>(next)] < 0) {
				type4_selected_left_index_ = next;
				break;
			}
		}
		RefreshType4Widgets();
		if (bottom_bar_) {
			bottom_bar_->SetText("匹配正确，请继续");
		}
		return;
	}

	session_module_.RecordAnswer(false);
	if (image_bad_) {
		image_bad_->SetText("word_practice_bad.bin");
		image_bad_->SetVisible(true);
	}
	if (image_good_) {
		image_good_->SetVisible(false);
	}

	const std::string left_word = (selected_left < static_cast<int>(type4_left_words_.size()))
								   ? type4_left_words_[static_cast<size_t>(selected_left)]
								   : "该单词";
	std::string right_word = Trim(current_choice_.expected);
	if (expected_right >= 0 && expected_right < static_cast<int>(type4_right_words_.size())) {
		right_word = type4_right_words_[static_cast<size_t>(expected_right)];
	}
	if (label_alert_) {
		label_alert_->SetText("答错了，" + left_word + " 对应 " + right_word);
	}
	if (bottom_bar_) {
		bottom_bar_->SetText("按方向键或ABCD进入下一题");
	}

	SaveAnswerStats(q, false);
}

void WordPracticeApp::HandleSpeakAction(const ButtonEvent &event) {
	const QuestionData *question = selection_module_.GetQuestion(session_module_.CurrentQuestionIndex());
	if (session_module_.IsFinished() || !question) {
		return;
	}
	const auto &q = *question;

	if (event.id == AppButton::Start) {
		if (event.action == ButtonAction::PressDown) {
			if (!speak_recording_) {
				ESP_LOGI(kTag, "type7-10 start press-down: begin listening");
				AppService::GetInstance().StartListening();
				speak_recording_ = true;
			}
		} else if (event.action == ButtonAction::PressUp) {
			if (speak_recording_) {
				ESP_LOGI(kTag, "type7-10 start press-up: stop listening");
				AppService::GetInstance().StopListening();
				speak_recording_ = false;
			}
		}
		return;
	}

	if (!IsClickLike(event)) {
		return;
	}

	const AppButton button = event.id;
	if (button != AppButton::D) {
		return;
	}

	std::string answer_text = Trim(current_choice_.expected);
	if (answer_text.empty()) {
		answer_text = Trim(q.answer);
	}

	ESP_LOGI(kTag, "type7-10 manual skip by D");
	session_module_.RecordAnswer(false);
	if (image_bad_) {
		image_bad_->SetText("word_practice_bad.bin");
		image_bad_->SetVisible(true);
	}
	if (image_good_) {
		image_good_->SetVisible(false);
	}
	if (label_alert_) {
		label_alert_->SetText("回答错误");
	}
	UpdateAsrResultPresentation(current_question_type_, "");
	if (bottom_bar_) {
		bottom_bar_->SetText("按方向键或ABCD进入下一题");
	}

	SaveAnswerStats(q, false);
}

void WordPracticeApp::OnChatMessage(const char* role, const char* content) {
	if (!ctx_ || !ui_ready_ || !label_asr_result_ || !role || !content) {
		return;
	}
	if (current_question_type_ < 7 || current_question_type_ > 10) {
		return;
	}
	const QuestionData *question = selection_module_.GetQuestion(session_module_.CurrentQuestionIndex());
	if (session_module_.AwaitingNextQuestion() || !question) {
		return;
	}
	const auto &q = *question;
	if (::strcmp(role, "user") != 0) {
		return;
	}
	if (content[0] == '\0') {
		return;
	}

	std::string answer_text = Trim(current_choice_.expected);
	if (answer_text.empty()) {
		answer_text = Trim(q.answer);
	}

	bool correct = false;
	std::string display_asr = Trim(content);
	if (current_question_type_ == 7 || current_question_type_ == 8) {
		const std::string normalized_asr = NormalizeLettersOnlyLower(content);
		const std::string expected = NormalizeLettersOnlyLower(answer_text);
		correct = (!normalized_asr.empty() && !expected.empty() && normalized_asr == expected);
		ESP_LOGI(kTag, "type7-8 asr normalized='%s' expected='%s'", normalized_asr.c_str(), expected.c_str());
	} else {
		const auto asr_words = NormalizeSentenceWordsLower(content);
		const auto expected_words = NormalizeSentenceWordsLower(answer_text);
		const float coverage = ComputeWordCoverageRatio(asr_words, expected_words);
		correct = coverage >= 0.8f;
		ESP_LOGI(kTag, "type9-10 asr coverage=%.3f", static_cast<double>(coverage));
	}

	UpdateAsrResultPresentation(current_question_type_, display_asr);
	if (correct) {
		session_module_.RecordAnswer(true);
		AddLearnedWordsFromText(answer_text);
		if (image_good_) {
			image_good_->SetText("word_practice_good.bin");
			image_good_->SetVisible(true);
		}
		if (image_bad_) {
			image_bad_->SetVisible(false);
		}
		if (label_alert_) {
			label_alert_->SetText("答对了，太棒了！");
		}
		if (bottom_bar_) {
			bottom_bar_->SetText("按方向键或ABCD进入下一题");
		}
		SaveAnswerStats(q, true);
	} else {
		if (image_bad_) {
			image_bad_->SetText("word_practice_bad.bin");
			image_bad_->SetVisible(true);
		}
		if (image_good_) {
			image_good_->SetVisible(false);
		}
		UpdateAsrResultPresentation(current_question_type_, display_asr);
		if (label_alert_) {
			label_alert_->SetText("回答错误");
		}
		if (bottom_bar_) {
			bottom_bar_->SetText("按住Start录音，松开识别；D键跳过");
		}
	}
	SyncScoreLabels();
	Render(*ctx_);
}

void WordPracticeApp::RefreshType56Widgets() {
	const bool show = (current_question_type_ == 5 || current_question_type_ == 6);
	if (textarea_input_answer_) {
		textarea_input_answer_->SetVisible(show);
		textarea_input_answer_->SetFocused(false);
		textarea_input_answer_->SetText(show ? type56_input_answer_ : "");
	}
	if (dialog_select_board_) {
		dialog_select_board_->SetVisible(show);
		if (show) {
			const int count = static_cast<int>(type56_dialog_items_.size());
			int grid_rows = 1;
			int grid_cols = 1;
			const char *selected_font = "wenquanyi_11pt";
			if (count > 0) {
				const app_ui::Rect dialog_rect = dialog_select_board_->DeclaredRect();
				const int dialog_width = std::max(1, static_cast<int>(dialog_rect.w));
				const int max_rows = std::min(3, count);
				bool fitted = false;
				const std::array<const char *, 2> font_candidates = {"wenquanyi_11pt", "wenquanyi_9pt"};
				for (const char *font_name : font_candidates) {
					int max_word_width = 0;
					for (const auto &word : type56_dialog_items_) {
						const int w = static_cast<int>(epd_ ? epd_->MeasureUtf8Width(word, font_name) : static_cast<int>(word.size() * 8));
						max_word_width = std::max(max_word_width, w);
					}
					const int required_cell_width = std::max(1, max_word_width + 4);

					for (int rows = 1; rows <= max_rows; ++rows) {
						const int cols = std::max(1, (count + rows - 1) / rows);
						const int cell_width = dialog_width / cols;
						if (cell_width >= required_cell_width) {
							grid_rows = rows;
							grid_cols = cols;
							selected_font = font_name;
							fitted = true;
							break;
						}
					}
					if (fitted) {
						break;
					}
				}

				if (!fitted) {
					selected_font = "wenquanyi_9pt";
					grid_rows = std::min(3, count);
					grid_cols = std::max(1, (count + grid_rows - 1) / grid_rows);
				}
			}
			type56_grid_cols_ = grid_cols;
			dialog_select_board_->SetFontName(selected_font);

			app_ui::DialogProfile dialog_profile = dialog_select_board_->Profile();
			dialog_profile.mode = app_ui::DialogProfile::Mode::Grid;
			dialog_profile.grid_rows = std::max(1, grid_rows);
			dialog_profile.grid_cols = std::max(1, grid_cols);
			dialog_profile.navigation_enabled = false;
			dialog_profile.selection_highlight_enabled = !type56_show_correct_answer_;
			dialog_select_board_->SetProfile(dialog_profile);

			if (type56_selected_index_ < 0) {
				type56_selected_index_ = 0;
			}
			if (!type56_dialog_items_.empty() && type56_selected_index_ >= static_cast<int>(type56_dialog_items_.size())) {
				type56_selected_index_ = static_cast<int>(type56_dialog_items_.size()) - 1;
			}
			dialog_select_board_->SetItems(type56_dialog_items_);
			dialog_select_board_->SetSelectedIndex(type56_selected_index_);
		} else {
			type56_grid_cols_ = 1;
			dialog_select_board_->SetItems({});
			dialog_select_board_->SetSelectedIndex(0);
			dialog_select_board_->SetText("");
		}
	}
}

void WordPracticeApp::HandleType56Action(AppButton button) {
	const QuestionData *question = selection_module_.GetQuestion(session_module_.CurrentQuestionIndex());
	if (session_module_.IsFinished() || !question) {
		return;
	}
	const auto &q = *question;

	if (type56_words_.empty()) {
		type56_words_ = current_choice_.hints;
		type56_dialog_items_ = type56_words_;
		if (type56_selected_index_ >= static_cast<int>(type56_words_.size())) {
			type56_selected_index_ = 0;
		}
	}

	const int count = static_cast<int>(type56_words_.size());
	const int cols = std::max(1, type56_grid_cols_);
	if (count > 0) {
		if (type56_selected_index_ < 0) {
			type56_selected_index_ = 0;
		}
		if (type56_selected_index_ >= count) {
			type56_selected_index_ = count - 1;
		}
	}

	auto row_item_count = [count, cols](int row) -> int {
		const int start = row * cols;
		if (start >= count) {
			return 0;
		}
		return std::min(cols, count - start);
	};

	if (button == AppButton::Up && count > 0) {
		const int rows = std::max(1, (count + cols - 1) / cols);
		const int row = type56_selected_index_ / cols;
		const int col = type56_selected_index_ % cols;
		int target_row = (row - 1 + rows) % rows;
		for (int i = 0; i < rows; ++i) {
			if (col < row_item_count(target_row)) {
				type56_selected_index_ = target_row * cols + col;
				break;
			}
			target_row = (target_row - 1 + rows) % rows;
		}
		RefreshType56Widgets();
		return;
	}
	if (button == AppButton::Down && count > 0) {
		const int rows = std::max(1, (count + cols - 1) / cols);
		const int row = type56_selected_index_ / cols;
		const int col = type56_selected_index_ % cols;
		int target_row = (row + 1) % rows;
		for (int i = 0; i < rows; ++i) {
			if (col < row_item_count(target_row)) {
				type56_selected_index_ = target_row * cols + col;
				break;
			}
			target_row = (target_row + 1) % rows;
		}
		RefreshType56Widgets();
		return;
	}
	if (button == AppButton::Left && count > 0) {
		const int row = type56_selected_index_ / cols;
		const int row_start = row * cols;
		const int items_in_row = row_item_count(row);
		const int row_end = row_start + std::max(1, items_in_row) - 1;
		type56_selected_index_ = (type56_selected_index_ > row_start) ? (type56_selected_index_ - 1) : row_end;
		RefreshType56Widgets();
		return;
	}
	if (button == AppButton::Right && count > 0) {
		const int row = type56_selected_index_ / cols;
		const int row_start = row * cols;
		const int items_in_row = row_item_count(row);
		const int row_end = row_start + std::max(1, items_in_row) - 1;
		type56_selected_index_ = (type56_selected_index_ < row_end) ? (type56_selected_index_ + 1) : row_start;
		RefreshType56Widgets();
		return;
	}

	if (button == AppButton::B) {
		type56_show_correct_answer_ = false;
		type56_correct_answer_display_.clear();
		type56_dialog_items_ = type56_words_;
		std::string text = Trim(type56_input_answer_);
		if (!text.empty()) {
			const size_t split = text.find_last_of(' ');
			if (split == std::string::npos) {
				type56_input_answer_.clear();
			} else {
				type56_input_answer_ = Trim(text.substr(0, split));
			}
			RefreshType56Widgets();
		}
		return;
	}

	if (button == AppButton::C && count > 0 && type56_selected_index_ >= 0 && type56_selected_index_ < count) {
		type56_show_correct_answer_ = false;
		type56_correct_answer_display_.clear();
		type56_dialog_items_ = type56_words_;
		const std::string &picked_word = type56_words_[type56_selected_index_];
		if (!picked_word.empty()) {
			if (!type56_input_answer_.empty()) {
				type56_input_answer_ += " ";
			}
			type56_input_answer_ += picked_word;
			RefreshType56Widgets();
		}
		return;
	}

	if (button == AppButton::Start) {
		if (!current_audio_path_.empty() && !PlayAudioFromSd(current_audio_path_)) {
			if (label_alert_) {
				label_alert_->SetText("音频播放失败");
			}
		}
		return;
	}

	if (button != AppButton::D) {
		return;
	}

	const std::string expected_display = Trim(current_choice_.expected.empty() ? q.answer : current_choice_.expected);
	const std::string input_text = NormalizeType56ForCompare(type56_input_answer_);
	const std::string answer_text = NormalizeType56ForCompare(expected_display);
	const bool correct = (!input_text.empty() && input_text == answer_text);
	session_module_.RecordAnswer(correct);

	if (correct) {
		type56_show_correct_answer_ = false;
		type56_correct_answer_display_.clear();
		type56_dialog_items_ = type56_words_;
		AddLearnedWordsFromText(answer_text);
		if (image_good_) {
			image_good_->SetText("word_practice_good.bin");
			image_good_->SetVisible(true);
		}
		if (image_bad_) {
			image_bad_->SetVisible(false);
		}
		if (label_alert_) {
			label_alert_->SetText("答对了，太棒了！");
		}
		if (bottom_bar_) {
			bottom_bar_->SetText("按方向键或ABCD进入下一题");
		}
	} else {
		if (image_bad_) {
			image_bad_->SetText("word_practice_bad.bin");
			image_bad_->SetVisible(true);
		}
		if (image_good_) {
			image_good_->SetVisible(false);
		}
		type56_show_correct_answer_ = true;
		type56_correct_answer_display_ = expected_display;
		type56_dialog_items_.assign(1, expected_display);
		type56_selected_index_ = 0;
		RefreshType56Widgets();
		if (label_alert_) {
			label_alert_->SetText("回答错误");
		}
		if (bottom_bar_) {
			bottom_bar_->SetText("按方向键或ABCD进入下一题");
		}
	}

	if (current_question_type_ == 6) {
		UpdateQuestionPromptPresentation(current_question_type_, "");
	}

	SaveAnswerStats(q, correct);
}

bool WordPracticeApp::IsSessionPassed() const {
	return result_module_.IsPassed(session_module_);
}

std::string WordPracticeApp::SelectSceneIdByType(int question_type) const {
	return quiz_module_.SelectSceneId(question_type);
}

std::string WordPracticeApp::TypeTitle(int question_type) const {
	return quiz_module_.TypeTitle(question_type);
}

std::string WordPracticeApp::TypeInstruction(int question_type) const {
	return quiz_module_.TypeInstruction(question_type);
}
WordPracticeApp::ChoiceState WordPracticeApp::BuildChoiceState(const QuestionData &q) const {
	return quiz_module_.Generate(q);
}

std::string WordPracticeApp::NormalizeAnswerToken(std::string value) const {
	return quiz_module_.NormalizeAnswerToken(std::move(value));
}

std::string WordPracticeApp::NormalizePairWord(const std::string &value) const {
	return quiz_module_.NormalizePairWord(value);
}

std::string WordPracticeApp::ButtonToken(AppButton button) const {
	switch (button) {
		case AppButton::A:
			return "A";
		case AppButton::B:
			return "B";
		case AppButton::C:
			return "C";
		case AppButton::D:
			return "D";
		default:
			return {};
	}
}

std::string WordPracticeApp::DiscoverQuestionDbPath() const {
	return eteacher::database_manager::DiscoverQuestionDbPath(kTag);
}

std::string WordPracticeApp::DiscoverUserDbPath() const {
	return result_module_.ProgressDao().DiscoverUserDbPath();
}

bool WordPracticeApp::EnsureStatsTables(sqlite3 *db) const {
	return result_module_.ProgressDao().EnsureStatsTables(db);
}

int WordPracticeApp::QueryCurrentLevel(sqlite3 *db) const {
	(void)db;
	return result_module_.ProgressDao().QueryCurrentLevel();
}

word_practice::LearnedSnapshot WordPracticeApp::QueryLearned(sqlite3 *db,
										 int question_id,
										 const std::string &textbook) const {
	(void)db;
	return result_module_.ProgressDao().QueryLearned(question_id, textbook);
}

void WordPracticeApp::SaveAnswerStats(const QuestionData &q, bool correct) {
	const std::string textbook_name = current_choice_.textbook_name.empty() ? (q.stage.empty() ? "default" : q.stage) : current_choice_.textbook_name;
	result_module_.ProgressDao().SaveAnswerStats(session_module_, q, textbook_name, correct);
}

std::unique_ptr<AppBase> MakeWordPracticeApp() {
	return std::make_unique<WordPracticeApp>();
}

bool WordPracticeApp::PlayAudioFromSd(const std::string &audio_path) {
	if (audio_path.empty()) {
		return false;
	}

	std::string ogg_data;
	const std::string bundle_name = NormalizeAudioEntryName(audio_path);
	if (!bundle_name.empty() && ReadAudioBundleEntry(audio_path, bundle_name, &ogg_data)) {
		const bool has_ogg_page = BufferContainsToken(ogg_data, "OggS", 4);
		const bool has_opus_head = BufferContainsToken(ogg_data, "OpusHead", 8);
		if (!has_ogg_page || !has_opus_head) {
			WP_DIAG_LOGW(kTag,
				"audio payload invalid path=%s bundle=%s size=%u has_ogg_page=%d has_opus_head=%d preview=%s",
				audio_path.c_str(),
				bundle_name.c_str(),
				static_cast<unsigned>(ogg_data.size()),
				has_ogg_page ? 1 : 0,
				has_opus_head ? 1 : 0,
				BuildAudioPreviewHex(ogg_data, 16).c_str());
			return false;
		}
		WP_DIAG_LOGW(kTag,
			"audio payload ready path=%s bundle=%s size=%u preview=%s",
			audio_path.c_str(),
			bundle_name.c_str(),
			static_cast<unsigned>(ogg_data.size()),
			BuildAudioPreviewHex(ogg_data, 16).c_str());
		LogResolvedAssetAccess(
			"audio",
			"play",
			audio_path,
			audio_bundle_resolved_path_.empty() ? bundle_name : audio_bundle_resolved_path_,
			"ReadAudioBundleEntry+AppService::PlaySound",
			bundle_name.c_str());
		AppService::GetInstance().PlaySound(ogg_data);
		return true;
	}

	DB_LOGW(kTag, "Audio not found path=%s bundle=%s", audio_path.c_str(), bundle_name.c_str());
	WP_ASSET_LOGW(kTag, "Open audio failed: %s (bundle=%s)", audio_path.c_str(), bundle_name.c_str());
	return false;
}

bool WordPracticeApp::EnsureAudioBundleIndexLoaded(const std::string &audio_path) {
	const std::string bundle_path = ResolveAudioBundlePath(audio_path);
	if (audio_bundle_index_loaded_ && audio_bundle_resolved_path_ == bundle_path) {
		return audio_bundle_index_available_;
	}
	audio_bundle_index_loaded_ = true;
	audio_bundle_index_available_ = false;
	audio_bundle_resolved_path_.clear();
	audio_bundle_entries_.clear();

	File sd_file;
	sd_file = SD.open(bundle_path.c_str(), FILE_READ);
	if (!sd_file) {
		DB_LOGW(kTag, "Open audio bundle failed path=%s", bundle_path.c_str());
		WP_ASSET_LOGW(kTag, "Open audio bundle failed: %s", bundle_path.c_str());
		return false;
	}
	LogResolvedAssetAccess(
		"audio",
		"open",
		bundle_path,
		bundle_path,
		"SD.open(FILE_READ)",
		"audio_bundle");

	std::array<uint8_t, 28> header{};
	const size_t header_read = sd_file.readBytes(reinterpret_cast<char *>(header.data()), static_cast<int>(header.size()));
	if (header_read != header.size()) {
		sd_file.close();
		WP_ASSET_LOGW(kTag, "Read audio bundle header failed");
		return false;
	}
	if (std::memcmp(header.data(), kAudioBundleMagic, sizeof(kAudioBundleMagic)) != 0) {
		sd_file.close();
		WP_ASSET_LOGW(kTag, "Invalid audio bundle magic: %s", bundle_path.c_str());
		return false;
	}

	uint32_t entry_count = 0;
	uint32_t record_size = 0;
	std::memcpy(&entry_count, header.data() + 12, sizeof(entry_count));
	std::memcpy(&record_size, header.data() + 16, sizeof(record_size));
	if (record_size < 156) {
		sd_file.close();
		WP_ASSET_LOGW(kTag, "Invalid audio bundle record size: %u", static_cast<unsigned>(record_size));
		return false;
	}

	std::vector<uint8_t> record(record_size);
	for (uint32_t index = 0; index < entry_count; ++index) {
		const size_t read_size = sd_file.readBytes(reinterpret_cast<char *>(record.data()), static_cast<int>(record.size()));
		if (read_size != record.size()) {
			sd_file.close();
			audio_bundle_entries_.clear();
			WP_ASSET_LOGW(kTag, "Read audio bundle record failed index=%u", static_cast<unsigned>(index));
			return false;
		}
		const char *name_data = reinterpret_cast<const char *>(record.data());
		const size_t name_length = strnlen(name_data, 128);
		if (name_length == 0) {
			continue;
		}
		AudioBundleEntry entry;
		std::memcpy(&entry.offset, record.data() + 128, sizeof(entry.offset));
		std::memcpy(&entry.size, record.data() + 136, sizeof(entry.size));
		audio_bundle_entries_.emplace(std::string(name_data, name_length), entry);
	}

	sd_file.close();
	audio_bundle_resolved_path_ = bundle_path;
	audio_bundle_index_available_ = !audio_bundle_entries_.empty();
	LogResolvedAssetAccess(
		"audio",
		"index",
		bundle_path,
		bundle_path,
		"SD.readBytes(bundle_index)",
		"audio_bundle_index");
	return audio_bundle_index_available_;
}

bool WordPracticeApp::ReadAudioBundleEntry(const std::string &audio_path, const std::string &audio_name, std::string *ogg_data) {
	if (ogg_data == nullptr || audio_name.empty() || !EnsureAudioBundleIndexLoaded(audio_path)) {
		return false;
	}
	const auto it = audio_bundle_entries_.find(audio_name);
	if (it == audio_bundle_entries_.end() || it->second.size == 0) {
		DB_LOGW(kTag, "Audio bundle entry missing: %s indexed=%u", audio_name.c_str(), static_cast<unsigned>(audio_bundle_entries_.size()));
		WP_ASSET_LOGW(kTag, "Audio bundle entry missing: %s indexed=%u", audio_name.c_str(), static_cast<unsigned>(audio_bundle_entries_.size()));
		return false;
	}

	if (it->second.offset > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) ||
		it->second.size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
		it->second.size > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
		DB_LOGW(kTag, "Audio bundle entry out of range: %s offset=%llu size=%llu",
			audio_name.c_str(),
			static_cast<unsigned long long>(it->second.offset),
			static_cast<unsigned long long>(it->second.size));
		return false;
	}

	File sd_file;
	if (audio_bundle_resolved_path_.empty()) {
		DB_LOGW(kTag, "Reopen audio bundle failed for entry: %s", audio_name.c_str());
		return false;
	}
	sd_file = SD.open(audio_bundle_resolved_path_.c_str(), FILE_READ);
	if (!sd_file) {
		DB_LOGW(kTag, "Reopen audio bundle failed for entry: %s", audio_name.c_str());
		return false;
	}
	if (!sd_file.seek(static_cast<uint32_t>(it->second.offset), SeekSet)) {
		sd_file.close();
		DB_LOGW(kTag, "Seek audio bundle entry failed: %s offset=%llu",
			audio_name.c_str(),
			static_cast<unsigned long long>(it->second.offset));
		return false;
	}
	ogg_data->assign(static_cast<size_t>(it->second.size), '\0');
	const size_t read_size = sd_file.readBytes(ogg_data->data(), static_cast<int>(ogg_data->size()));
	sd_file.close();
	if (read_size != ogg_data->size()) {
		ogg_data->clear();
		DB_LOGW(kTag, "Read audio bundle entry failed: %s expected=%u actual=%u",
			audio_name.c_str(),
			static_cast<unsigned>(it->second.size),
			static_cast<unsigned>(read_size));
		WP_ASSET_LOGW(kTag, "Read audio bundle entry failed: %s expected=%u actual=%u",
			audio_name.c_str(),
			static_cast<unsigned>(it->second.size),
			static_cast<unsigned>(read_size));
		return false;
	}
	LogResolvedAssetAccess(
		"audio",
		"read",
		audio_name,
		audio_bundle_resolved_path_,
		"SD.seek+SD.readBytes",
		"audio_bundle_entry");
	return true;
}

std::string WordPracticeApp::ResolveBundledImagePath(const std::string &image_name) {
	const std::string entry_name = NormalizeImageEntryName(BasenameFromPath(image_name));
	if (entry_name.empty()) {
		WP_ASSET_LOGW(kTag, "Image entry missing: %s", image_name.c_str());
		return {};
	}
	const std::string package_proxy_path = eteacher::app_ui::BuildWordsImageProxyPath(entry_name);
	LogResolvedAssetAccess(
		"image",
		"resolve",
		image_name,
		package_proxy_path,
		"image-package-proxy+ImageWidget::SetText",
		"image_package_proxy");
	return package_proxy_path;
}

void WordPracticeApp::ScheduleQuestionAudioAutoPlay() {
	if (current_audio_path_.empty()) {
		return;
	}

	if (question_audio_timer_ == nullptr) {
		esp_timer_create_args_t timer_args = {
			.callback = &WordPracticeApp::QuestionAudioTimerCallback,
			.arg = this,
			.dispatch_method = ESP_TIMER_TASK,
			.name = "wp_audio_delay",
			.skip_unhandled_events = true,
		};
		if (esp_timer_create(&timer_args, &question_audio_timer_) != ESP_OK) {
			question_audio_timer_ = nullptr;
			return;
		}
	}

	(void)esp_timer_stop(question_audio_timer_);
	(void)esp_timer_start_once(question_audio_timer_, 1000000);
}

void WordPracticeApp::CancelQuestionAudioAutoPlay() {
	if (question_audio_timer_ == nullptr) {
		return;
	}
	(void)esp_timer_stop(question_audio_timer_);
	(void)esp_timer_delete(question_audio_timer_);
	question_audio_timer_ = nullptr;
}

void WordPracticeApp::QuestionAudioTimerCallback(void *arg) {
	auto *self = static_cast<WordPracticeApp *>(arg);
	if (!self || self->current_audio_path_.empty()) {
		return;
	}
	(void)self->PlayAudioFromSd(self->current_audio_path_);
}
