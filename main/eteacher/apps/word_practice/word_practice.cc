#include "eteacher/apps/word_practice/word_practice.h"

#include <algorithm>
#include <array>
#include <cerrno>
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
#include "eteacher/apps/word_practice/word_practice_config.h"
#include "eteacher/apps/word_practice/word_practice_time_utils.h"
#include "eteacher/apps/word_practice/word_practice_ui.h"
#include "eteacher/apps/word_practice/word_practice_utils.h"
#include "eteacher/app_ui/common_ui_utils.h"
#include "eteacher/database_manager/database_debug.h"
#include "eteacher/app_service/app_service.h"
#include "eteacher/database_manager/sqlite_db_api.h"

#define WP_APP_DBLOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define WP_DIAG_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define WP_ASSET_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define WP_ASSET_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)
#define WP_PERSIST_LOGI(tag, format, ...) esp_log_write(ESP_LOG_INFO, tag, format, ##__VA_ARGS__)
#define WP_PERSIST_LOGW(tag, format, ...) esp_log_write(ESP_LOG_WARN, tag, format, ##__VA_ARGS__)

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
constexpr uint32_t kWidgetLabelMyStage = 0x152EA4ADu;
constexpr uint32_t kWidgetButtonStageSetting = 0xA2491E37u;
constexpr uint32_t kWidgetLabelMyLevel = 0x286DF25Du;
constexpr uint32_t kWidgetImageSunMoonStar = 0x4E42D10Au;
constexpr uint32_t kWidgetCheckboxNoRead = 0x4C6933A3u;
constexpr uint32_t kWidgetCheckboxHasRead = 0x3C197D08u;
constexpr uint32_t kWidgetLabelReadSetting = 0x2C4D6279u;
constexpr uint32_t kWidgetLabelTodayMission = 0xC210C036u;
constexpr uint32_t kWidgetProgressTodayMission = 0x06460615u;
constexpr uint32_t kWidgetProgressPractice = 0x89DAD822u;
constexpr uint32_t kWidgetHomeFrameTop = 0x01F331C9u;
constexpr uint32_t kWidgetHomeFrameBottom = 0x40AB301Au;
constexpr uint32_t kWidgetButtonMissionSetting = 0xA7B12F6Fu;
constexpr uint32_t kWidgetLabelWordPreview = 0xE1A01541u;
constexpr uint32_t kWidgetLabelMissionProgress = 0x8FC5D946u;
constexpr uint32_t kWidgetLabelProgressPercent = 0xEFA86CF3u;
constexpr uint32_t kWidgetDialogSettingResult = 0x1ADDAADAu;
constexpr uint32_t kWidgetListviewSelect = 0xD2702AA1u;
constexpr uint32_t kWidgetButtonConfirm = 0x234566C2u;
constexpr uint32_t kWidgetButtonCancle = 0x087A890Eu;
constexpr char kAudioBundleMagic[] = {'O', 'G', 'G', 'B', 'I', 'N', '1', '\0'};
constexpr int kType4MeaningMaxWidth = 110;
constexpr size_t kImageChoiceOptionCount = 3;
constexpr size_t kStandardChoiceOptionMinCount = 3;
constexpr const char *kHomeSceneId = "page_9abe";
constexpr const char *kUserJsonPath = "/sdcard/user/user.json";
constexpr int kLevelIconCellSize = 20;
constexpr int kLevelIconMaxPerRow = 5;
constexpr int kLevelIconMaxCount = 25;
constexpr int kDefaultTodayMissionCount = 15;
constexpr int kDefaultTodayPracticeWordCount = 15;
constexpr std::array<int, 12> kDefaultStageLevelupCount = {10, 12, 14, 16, 18, 20, 24, 28, 32, 36, 40, 48};

std::string TodayDateString() {
	return word_practice::CurrentCalendarDateString();
}

int ClampPercent(int value) {
	return std::max(0, std::min(100, value));
}

int JsonIntOrDefault(cJSON *object, const char *key, int fallback);
bool JsonBoolOrDefault(cJSON *object, const char *key, bool fallback);
std::string JsonStringOrDefault(cJSON *object, const char *key, const std::string &fallback);

int PositiveOrFallback(int value, int fallback) {
	return value > 0 ? value : fallback;
}

void EnsureIntVectorSize(std::vector<int> *values, size_t size, int fallback) {
	if (values == nullptr) {
		return;
	}
	if (values->size() < size) {
		values->resize(size, fallback);
	}
	for (auto &value : *values) {
		if (value <= 0) {
			value = fallback;
		}
	}
}

int ParseStageIndex(const std::string &value) {
	std::string lower = value;
	while (!lower.empty() && std::isspace(static_cast<unsigned char>(lower.front())) != 0) {
		lower.erase(lower.begin());
	}
	while (!lower.empty() && std::isspace(static_cast<unsigned char>(lower.back())) != 0) {
		lower.pop_back();
	}
	std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	if (lower.rfind("stage", 0) == 0) {
		const int parsed = std::atoi(lower.substr(5).c_str());
		return std::max(1, std::min(12, parsed));
	}
	const int parsed = std::atoi(lower.c_str());
	return std::max(1, std::min(12, parsed <= 0 ? 1 : parsed));
}

std::string StageKey(int stage_index) {
	return "stage" + std::to_string(std::max(1, std::min(12, stage_index)));
}

std::string StageCursorStateKey(int stage_index) {
	return "word_practice." + StageKey(stage_index) + "_cursor";
}

std::string MasteredProfileSqlCondition(const char *alias = nullptr) {
	const std::string prefix = (alias != nullptr && alias[0] != '\0') ? std::string(alias) + "." : std::string();
	const std::string mastered_column = prefix + "mastered";
	return "((" + mastered_column + " = " + std::to_string(static_cast<int>(word_practice::MasteredState::UserMastered)) + ")"
		" OR ((" + mastered_column + " IS NULL OR " + mastered_column + " <> " + std::to_string(static_cast<int>(word_practice::MasteredState::Suppressed)) + ")"
		" AND " + prefix + "recall_score >= 3"
		" AND " + prefix + "output_score >= 3"
		" AND " + prefix + "strength >= 60"
		" AND " + prefix + "lapse_count <= 3))";
}

std::string ReadFileToString(const char *path) {
	if (path == nullptr || path[0] == '\0') {
		return {};
	}
	FILE *fp = std::fopen(path, "rb");
	if (!fp) {
		return {};
	}
	if (std::fseek(fp, 0, SEEK_END) != 0) {
		std::fclose(fp);
		return {};
	}
	const long size = std::ftell(fp);
	if (size < 0) {
		std::fclose(fp);
		return {};
	}
	std::rewind(fp);
	std::string content(static_cast<size_t>(size), '\0');
	const size_t read_size = size > 0 ? std::fread(content.data(), 1, static_cast<size_t>(size), fp) : 0;
	std::fclose(fp);
	if (read_size != static_cast<size_t>(size)) {
		return {};
	}
	return content;
}

const word_practice::SelectedWord *FindSelectedWord(const std::vector<word_practice::SelectedWord> &selected_words,
						    int word_id) {
	for (const auto &selected_word : selected_words) {
		if (selected_word.word_id == word_id) {
			return &selected_word;
		}
	}
	return nullptr;
}

const word_practice::VocabularySeed *FindLoadedVocabularySeed(
	const std::vector<word_practice::VocabularySeed> &loaded_seeds,
	int word_id) {
	for (const auto &seed : loaded_seeds) {
		if (seed.word_id == word_id) {
			return &seed;
		}
	}
	return nullptr;
}

std::string BuildJsonLogPreview(const std::string &content, size_t max_length = 160) {
	std::string preview;
	preview.reserve(std::min(max_length, content.size()));
	for (char ch : content) {
		if (preview.size() >= max_length) {
			break;
		}
		if (ch == '\r' || ch == '\n' || ch == '\t') {
			preview.push_back(' ');
		} else {
			preview.push_back(ch);
		}
	}
	if (content.size() > preview.size()) {
		preview += "...";
	}
	return preview;
}

bool EnsureDirectoryExists(const char *path) {
	if (path == nullptr || path[0] == '\0') {
		return false;
	}
	std::string partial;
	for (const char ch : std::string(path)) {
		partial.push_back(ch);
		if (ch != '/') {
			continue;
		}
		if (partial.size() <= 1) {
			continue;
		}
		struct stat st {};
		if (::stat(partial.c_str(), &st) == 0) {
			if (!S_ISDIR(st.st_mode)) {
				return false;
			}
			continue;
		}
		if (::mkdir(partial.c_str(), 0777) != 0 && errno != EEXIST) {
			return false;
		}
	}
	struct stat st {};
	return ::stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool WriteStringToFile(const char *path, const std::string &content) {
	if (path == nullptr || path[0] == '\0') {
		return false;
	}
	std::string parent(path);
	const size_t slash = parent.find_last_of('/');
	if (slash != std::string::npos) {
		parent.resize(slash + 1);
		if (!EnsureDirectoryExists(parent.c_str())) {
			WP_PERSIST_LOGW(kTag,
				"write json ensure dir failed path=%s parent=%s errno=%d",
				path,
				parent.c_str(),
				errno);
			return false;
		}
	}
	const std::string temp_path = std::string(path) + ".tmp";
	FILE *fp = std::fopen(temp_path.c_str(), "wb");
	if (!fp) {
		WP_PERSIST_LOGW(kTag, "write json open failed path=%s temp=%s errno=%d", path, temp_path.c_str(), errno);
		return false;
	}
	const size_t written = content.empty() ? 0 : std::fwrite(content.data(), 1, content.size(), fp);
	const int flush_rc = std::fflush(fp);
	const int close_rc = std::fclose(fp);
	if (written != content.size() || flush_rc != 0 || close_rc != 0) {
		WP_PERSIST_LOGW(kTag,
			"write json write failed path=%s temp=%s written=%u expected=%u flush_rc=%d close_rc=%d errno=%d",
			path,
			temp_path.c_str(),
			static_cast<unsigned>(written),
			static_cast<unsigned>(content.size()),
			flush_rc,
			close_rc,
			errno);
		(void)std::remove(temp_path.c_str());
		return false;
	}
	if (std::remove(path) != 0 && errno != ENOENT) {
		WP_PERSIST_LOGW(kTag,
			"write json remove old failed path=%s temp=%s errno=%d",
			path,
			temp_path.c_str(),
			errno);
		(void)std::remove(temp_path.c_str());
		return false;
	}
	if (std::rename(temp_path.c_str(), path) != 0) {
		WP_PERSIST_LOGW(kTag,
			"write json rename failed path=%s temp=%s errno=%d",
			path,
			temp_path.c_str(),
			errno);
		(void)std::remove(temp_path.c_str());
		return false;
	}
	return true;
}

int JsonIntOrDefault(cJSON *object, const char *key, int fallback) {
	if (!object || !key) {
		return fallback;
	}
	cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (cJSON_IsNumber(item)) {
		return item->valueint;
	}
	if (cJSON_IsString(item) && item->valuestring) {
		return std::atoi(item->valuestring);
	}
	return fallback;
}

bool JsonBoolOrDefault(cJSON *object, const char *key, bool fallback) {
	if (!object || !key) {
		return fallback;
	}
	cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (cJSON_IsBool(item)) {
		return cJSON_IsTrue(item);
	}
	if (cJSON_IsNumber(item)) {
		return item->valueint != 0;
	}
	return fallback;
}

std::string JsonStringOrDefault(cJSON *object, const char *key, const std::string &fallback = {}) {
	if (!object || !key) {
		return fallback;
	}
	cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
	if (cJSON_IsString(item) && item->valuestring) {
		return item->valuestring;
	}
	if (cJSON_IsNumber(item)) {
		return std::to_string(item->valueint);
	}
	return fallback;
}

void LoadIntArrayFromJson(cJSON *parent, const char *key, std::vector<int> *values, int fallback) {
	if (values == nullptr) {
		return;
	}
	values->assign(12, fallback);
	if (!parent || !key) {
		return;
	}
	cJSON *array = cJSON_GetObjectItemCaseSensitive(parent, key);
	if (!cJSON_IsArray(array)) {
		return;
	}
	const int count = std::min(12, cJSON_GetArraySize(array));
	for (int i = 0; i < count; ++i) {
		cJSON *item = cJSON_GetArrayItem(array, i);
		if (cJSON_IsNumber(item)) {
			(*values)[static_cast<size_t>(i)] = item->valueint;
		}
	}
}

std::string JoinPreviewWords(const std::vector<std::string> &words, size_t max_count) {
	std::string text;
	const size_t count = std::min(max_count, words.size());
	for (size_t i = 0; i < count; ++i) {
		if (!text.empty()) {
			text += " / ";
		}
		text += words[i];
	}
	if (words.size() > count) {
		text += " ...";
	}
	return text;
}

std::string SkillLabel(word_practice::TrainingSkill skill) {
	switch (skill) {
		case word_practice::TrainingSkill::Recognition:
			return "识别";
		case word_practice::TrainingSkill::Recall:
			return "回忆";
		case word_practice::TrainingSkill::Output:
			return "输出";
		case word_practice::TrainingSkill::AdvancedSpeak:
			return "高级朗读";
		default:
			return "巩固";
	}
}

std::string StageLabel(int stage) {
	switch (stage) {
		case 0:
			return "未学习";
		case 1:
			return "初识别";
		case 2:
			return "已识别";
		case 3:
			return "可回忆";
		case 4:
			return "可输出";
		case 5:
			return "已稳定";
		default:
			return "学习中";
	}
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

const word_practice::BatchWordPlan *FindBatchPlan(const word_practice::LearningBatch &batch, int word_id) {
	for (const auto &plan : batch.items) {
		if (plan.selected_word.word_id == word_id) {
			return &plan;
		}
	}
	return nullptr;
}

word_practice::WordMasteryProfile *FindMasteryProfile(std::vector<word_practice::WordMasteryProfile> *profiles,
										 int word_id) {
	if (profiles == nullptr) {
		return nullptr;
	}
	for (auto &profile : *profiles) {
		if (profile.word_id == word_id) {
			return &profile;
		}
	}
	return nullptr;
}

using word_practice::utils::BasenameFromPath;
using word_practice::utils::BuildQuestionAudioPath;
using word_practice::utils::NormalizeAudioEntryName;
using word_practice::utils::NormalizeLettersOnlyLower;
using word_practice::utils::NormalizeSentenceWordsLower;
using word_practice::utils::SplitHintWords;
using word_practice::utils::StageNumberToTag;
using word_practice::utils::Trim;

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

bool IsExampleAudioProxyPath(const std::string &audio_path) {
	return audio_path.rfind(eteacher::app_ui::GetExampleAudioDir(), 0) == 0 ||
		   audio_path.rfind("/sdcard/resource/audio/example/", 0) == 0;
}

const char *ResolveAudioBundlePath(const std::string &audio_path) {
	return IsExampleAudioProxyPath(audio_path) ? eteacher::app_ui::GetExampleAudioBundlePath() :
									  eteacher::app_ui::GetWordsAudioBundlePath();
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

int64_t NowSec() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000000ULL);
}

int64_t NowMs() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000ULL);
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
	label_my_stage_ = nullptr;
	label_my_level_ = nullptr;
	label_read_setting_ = nullptr;
	label_today_mission_ = nullptr;
	label_word_preview_ = nullptr;
	label_mission_progress_ = nullptr;
	bottom_bar_ = nullptr;
	textarea_input_answer_ = nullptr;
	dialog_select_board_ = nullptr;
	dialog_setting_result_ = nullptr;
	listview_select_ = nullptr;
	button_confirm_ = nullptr;
	button_cancle_ = nullptr;
	button_stage_setting_ = nullptr;
	button_mission_setting_ = nullptr;
	checkbox_has_read_ = nullptr;
	checkbox_no_read_ = nullptr;
	progress_today_mission_ = nullptr;
	image_good_ = nullptr;
	image_bad_ = nullptr;
	image_public_speaker_ = nullptr;
	image_sun_moon_star_ = nullptr;
	image_a_ = nullptr;
	image_b_ = nullptr;
	image_c_ = nullptr;
	image_d_ = nullptr;
	image_write_ = nullptr;
	image_input_ = nullptr;
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
	image_sun_moon_star_static_rect_ = {};

	session_module_.ResetForNewRound(pass_target_questions_);
	question_scheduler_.Reset();
	selected_words_.clear();
	mastery_profiles_.clear();
	learning_batch_ = {};
	current_scheduled_question_ = {};
	current_question_slot_ = {};
	current_textbook_name_ = "default";
	current_round_goal_text_.clear();
	last_attempt_feedback_text_.clear();
	current_question_type_ = 1;
	current_choice_ = {};
	completed_rounds_for_textbook_ = 0;
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
	wrong_words_this_round_.clear();
	wrong_word_ids_this_round_.clear();
	last_session_wrong_word_ids_.clear();
	settlement_learned_word_labels_.clear();
	home_level_icon_labels_.clear();
	current_audio_path_.clear();
	question_prompt_profile_ = {};
	audio_bundle_entries_.clear();
	audio_bundle_index_loaded_ = false;
	audio_bundle_index_available_ = false;
	CancelQuestionAudioAutoPlay();
	current_speak_asr_failure_count_ = 0;
	current_learning_mode_ = word_practice::LearningMode::Normal;
	round_completion_recorded_ = false;
	ui_mode_ = UiMode::HomePreview;
	overlay_mode_ = OverlayMode::None;
	dialog_focus_ = DialogFocus::Confirm;
	last_session_summary_ = {};
	session_started_at_sec_ = 0;
	session_mastered_words_before_ = 0;
	session_progress_before_ = 0;
	ResetCycleScoreState();

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
		(void)ActivateScene(ctx, kHomeSceneId);
		ShowLoadingPreview();
		Render(ctx);
	}

	(void)eteacher::database_manager::EnsureSqliteRuntimeReady(kTag);
	(void)eteacher::database_manager::EnsureSqliteSdMounted(kTag);
	(void)LoadUserJson();
	eteacher::app_ui::SetWordResourceStage(CurrentStageIndex());
	current_user_id_ = std::max(0, user_json_.user_id);
	mastery_dao_.SetUserId(current_user_id_);
	result_module_.SetUserId(current_user_id_);
	enable_speak_questions_ = user_json_.enable_read_questions;
	word_selection_config_ = practice_flow_controller_.BuildRoundPlan(user_json_.today_mission.today_practice_word).selection_config;

	const int64_t startup_begin_ms = NowMs();
	WP_PERSIST_LOGW(kTag,
		"startup timing ui_setup_ms=%d",
		static_cast<int>(startup_begin_ms - enter_start_ms));
	const std::string preferred_textbook_name = StageNumberToTag(CurrentStageIndex());
	if (!preferred_textbook_name.empty()) {
		current_textbook_name_ = preferred_textbook_name;
	}
	RefreshHomePreview();
	const int64_t after_pick_ms = NowMs();
	WP_PERSIST_LOGW(kTag,
		"startup timing total_ms=%d ui_setup_ms=%d home_preview_ms=%d",
		static_cast<int>(after_pick_ms - enter_start_ms),
		static_cast<int>(startup_begin_ms - enter_start_ms),
		static_cast<int>(after_pick_ms - startup_begin_ms));

	Render(ctx);
}

void WordPracticeApp::SetWidgetVisibleById(uint32_t widget_id, bool visible) {
	if (root_ == nullptr) {
		return;
	}
	if (auto *widget = root_->FindById(widget_id)) {
		widget->SetVisible(visible);
	}
}

void WordPracticeApp::ShowLoadingPreview() {
	auto hide_widget = [](app_ui::Widget *widget) {
		if (widget) {
			widget->SetVisible(false);
		}
	};

	hide_widget(label_question_type_);
	hide_widget(label_correct_count_);
	hide_widget(label_wrong_count_);
	hide_widget(label_alert_);
	hide_widget(label_question_);
	hide_widget(label_asr_result_);
	hide_widget(label_press_aread_);
	hide_widget(label_press_d_skip_);
	hide_widget(label_my_stage_);
	hide_widget(label_my_level_);
	hide_widget(label_read_setting_);
	hide_widget(label_today_mission_);
	hide_widget(label_word_preview_);
	hide_widget(label_mission_progress_);
	hide_widget(progress_today_mission_);
	hide_widget(textarea_input_answer_);
	hide_widget(dialog_select_board_);
	hide_widget(dialog_setting_result_);
	hide_widget(listview_select_);
	hide_widget(button_confirm_);
	hide_widget(button_cancle_);
	hide_widget(button_stage_setting_);
	hide_widget(button_mission_setting_);
	hide_widget(checkbox_has_read_);
	hide_widget(checkbox_no_read_);
	hide_widget(image_good_);
	hide_widget(image_bad_);
	hide_widget(image_public_speaker_);
	hide_widget(image_sun_moon_star_);
	hide_widget(image_a_);
	hide_widget(image_b_);
	hide_widget(image_c_);
	hide_widget(image_d_);
	hide_widget(image_write_);
	hide_widget(image_input_);
	hide_widget(label_a_);
	hide_widget(label_b_);
	hide_widget(label_c_);
	hide_widget(label_d_);
	hide_widget(label_up_);
	hide_widget(label_left_);
	hide_widget(label_down_);
	hide_widget(label_right_);
	hide_widget(label_question_line2_);
	hide_widget(label_question_line3_);
	hide_widget(question_dash_line1_);
	hide_widget(question_dash_line2_);
	hide_widget(question_dash_line3_);
	hide_widget(label_asr_line2_);
	hide_widget(label_asr_line3_);
	hide_widget(asr_dash_line1_);
	hide_widget(asr_dash_line2_);
	hide_widget(asr_dash_line3_);

	SetWidgetVisibleById(kWidgetHomeFrameTop, false);
	SetWidgetVisibleById(kWidgetHomeFrameBottom, false);
	SetWidgetVisibleById(kWidgetPublicTeacher, false);
	SetWidgetVisibleById(kWidgetPublicCup, false);
	SetWidgetVisibleById(kWidgetPublicCorrect, false);
	SetWidgetVisibleById(kWidgetPublicWrong, false);
	SetWidgetVisibleById(kWidgetPublicSpeaker, false);
	SetWidgetVisibleById(kWidgetSpeakMic, false);
	HideSettlementLearnedWordLabels();
	HideHomeLevelIconLabels();

	if (bottom_bar_) {
		bottom_bar_->SetText("词库加载中...");
		bottom_bar_->SetVisible(true);
	}
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
	InvalidateQuestionPoolCache();
	ResetLoadedQuestionDataCache();
	ResetMasteryProfileCache();
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
	if (overlay_mode_ == OverlayMode::Settlement) {
		HandleSettlementAction(ctx, event);
		Render(ctx);
		return;
	}
	if (ui_mode_ == UiMode::HomePreview) {
		HandleHomePreviewAction(ctx, event);
		Render(ctx);
		return;
	}
	const bool is_speak_type = quiz_module_.IsSpeakType(current_question_type_);
	if (!is_speak_type && !IsClickLike(event)) {
		return;
	}

	if (session_module_.IsFinished()) {
		ShowSessionSummary();
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

bool WordPracticeApp::ShouldInterceptSelectExit() const {
	return overlay_mode_ == OverlayMode::Settlement;
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

bool WordPracticeApp::ActivateScene(AppContext &ctx, const std::string &scene_id) {
	if (scene_id.empty()) {
		return false;
	}
	for (size_t index = 0; index < scene_ids_.size(); ++index) {
		if (scene_ids_[index] == scene_id) {
			return router_.Activate(ctx, index);
		}
	}
	return false;
}

void WordPracticeApp::ShowHomePreview(AppContext &ctx) {
	ui_mode_ = UiMode::HomePreview;
	overlay_mode_ = OverlayMode::None;
	dialog_focus_ = DialogFocus::Confirm;
	(void)ActivateScene(ctx, kHomeSceneId);
	HideSelectionDialog();
	RefreshHomePreview();
	Render(ctx);
}

void WordPracticeApp::HideSelectionDialog() {
	if (dialog_setting_result_) {
		dialog_setting_result_->SetVisible(false);
	}
	if (listview_select_) {
		listview_select_->SetVisible(false);
	}
	if (button_confirm_) {
		button_confirm_->SetVisible(false);
	}
	if (button_cancle_) {
		button_cancle_->SetVisible(false);
	}
	overlay_mode_ = OverlayMode::None;
}

void WordPracticeApp::RefreshSelectionDialog() {
	if (!dialog_setting_result_) {
		return;
	}
	if (overlay_mode_ == OverlayMode::Settlement) {
		app_ui::DialogProfile profile = dialog_setting_result_->Profile();
		profile.mode = app_ui::DialogProfile::Mode::Prompt;
		profile.navigation_enabled = false;
		profile.selection_highlight_enabled = true;
		profile.prompt_button_height = 26;
		profile.prompt_max_lines = 14;
		profile.text_offset_x = 6;
		profile.text_offset_y = 6;
		profile.confirm_label = "重新开始 >";
		profile.cancel_label = "Select退出 >";
		dialog_setting_result_->SetProfile(profile);
		dialog_setting_result_->SetSelectedIndex(dialog_focus_ == DialogFocus::Confirm ? 0 : 1);
		dialog_setting_result_->SetText(BuildSettlementDialogText(last_session_summary_));
		dialog_setting_result_->SetVisible(true);
		if (listview_select_) {
			listview_select_->SetVisible(false);
		}
		if (button_confirm_) {
			button_confirm_->SetVisible(false);
		}
		if (button_cancle_) {
			button_cancle_->SetVisible(false);
		}
		return;
	}
	HideSelectionDialog();
}

void WordPracticeApp::StartPracticeRound(AppContext &ctx) {
	const int64_t round_start_ms = NowMs();
	HideSelectionDialog();
	ui_mode_ = UiMode::Practicing;
	session_started_at_sec_ = static_cast<int>(NowSec());
	session_mastered_words_before_ = QueryMasteredWordCount();
	session_progress_before_ = user_json_.today_progress_percent;
	ResetCycleScoreState();
	wrong_words_this_round_.clear();
	wrong_word_ids_this_round_.clear();
	learned_words_this_round_.clear();
	last_session_wrong_word_ids_.clear();
	const word_practice::PracticeRoundPlan round_plan = practice_flow_controller_.BuildRoundPlan(user_json_.today_mission.today_practice_word);
	word_selection_config_ = round_plan.selection_config;
	ResetRoundState();
	const int stage_index = CurrentStageIndex();
	const int stage_cursor_index = std::max(0, std::min(11, stage_index - 1));
	const int next_new_word_id = LoadStageCursorState(stage_index);
	if (CanReuseQuestionPool(stage_index, stage_cursor_index, next_new_word_id)) {
		WP_APP_DBLOGW(kTag,
			"reuse question pool stage_index=%d cursor=%d selected_words=%d seeds=%d available_words=%d",
			stage_index,
			next_new_word_id,
			static_cast<int>(selected_words_.size()),
			static_cast<int>(question_seed_pool_.size()),
			static_cast<int>(available_question_types_by_word_.size()));
	} else {
		LoadQuestionPool();
	}
	if (!PickNextQuestion()) {
		ui_mode_ = UiMode::HomePreview;
		if (label_alert_) {
			label_alert_->SetText("今日词库为空");
		}
		ShowHomePreview(ctx);
		return;
	}
	WP_PERSIST_LOGW(kTag,
		"start round timing total_ms=%d selected_words=%d seeds=%d available_words=%d answered=%d",
		static_cast<int>(NowMs() - round_start_ms),
		static_cast<int>(selected_words_.size()),
		static_cast<int>(question_seed_pool_.size()),
		static_cast<int>(available_question_types_by_word_.size()),
		session_module_.TotalAnswered());
	Render(ctx);
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
		const word_practice::BatchProgressSummary summary = batch_progress_tracker_.BuildSummary();
		msg += "B:" + std::to_string(summary.completed_items) + "/" + std::to_string(std::max(1, summary.total_items));
		msg += " Q:" + std::to_string(std::min(session_module_.TotalAnswered() + 1, session_module_.PassTargetQuestions())) + "/" + std::to_string(session_module_.PassTargetQuestions());
		ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
	}
}

void WordPracticeApp::BindWidgets(app_ui::Widget *root) {
	if (!root) {
		return;
	}
	settlement_learned_word_labels_.clear();
	home_level_icon_labels_.clear();

	label_question_type_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelQuestionType));
	label_correct_count_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelCorrectCount));
	label_wrong_count_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelWrongCount));
	label_alert_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelAlert));
	label_question_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelQuestion));
	label_asr_result_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelAsrResult));
	label_press_aread_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelPressARead));
	label_press_d_skip_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelPressDSkip));
	label_my_stage_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelMyStage));
	label_my_level_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelMyLevel));
	label_read_setting_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelReadSetting));
	label_today_mission_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelTodayMission));
	label_word_preview_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelWordPreview));
	label_mission_progress_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelMissionProgress));
	label_progress_percent_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelProgressPercent));
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
	dialog_setting_result_ = dynamic_cast<app_ui::DialogWidget *>(root->FindById(kWidgetDialogSettingResult));
	listview_select_ = dynamic_cast<app_ui::ListViewWidget *>(root->FindById(kWidgetListviewSelect));
	button_confirm_ = dynamic_cast<app_ui::ButtonWidget *>(root->FindById(kWidgetButtonConfirm));
	button_cancle_ = dynamic_cast<app_ui::ButtonWidget *>(root->FindById(kWidgetButtonCancle));
	button_stage_setting_ = dynamic_cast<app_ui::ButtonWidget *>(root->FindById(kWidgetButtonStageSetting));
	button_mission_setting_ = dynamic_cast<app_ui::ButtonWidget *>(root->FindById(kWidgetButtonMissionSetting));
	checkbox_has_read_ = dynamic_cast<app_ui::CheckboxWidget *>(root->FindById(kWidgetCheckboxHasRead));
	checkbox_no_read_ = dynamic_cast<app_ui::CheckboxWidget *>(root->FindById(kWidgetCheckboxNoRead));
	progress_today_mission_ = dynamic_cast<app_ui::ProgressWidget *>(root->FindById(kWidgetProgressTodayMission));
	progress_practice_ = dynamic_cast<app_ui::ProgressWidget *>(root->FindById(kWidgetProgressPractice));
	if (progress_practice_) {
		auto profile = progress_practice_->Profile();
		profile.draw_rounded_border = true;
		profile.corner_radius = std::max<int16_t>(0, progress_practice_->DeclaredRect().h / 2);
		progress_practice_->SetProfile(profile);
		progress_practice_->SetVisible(false);
	}
	if (label_progress_percent_) {
		label_progress_percent_->SetVisible(false);
		label_progress_percent_->SetText(std::to_string(std::max(0, user_json_.today_progress_percent)) + "%");
	}
	if (listview_select_) {
		app_ui::ListViewProfile profile = listview_select_->Profile();
		profile.rows = 5;
		profile.cols = 1;
		profile.selection_enabled = true;
		profile.focus_highlight_enabled = true;
		profile.activation_enabled = false;
		listview_select_->SetProfile(profile);
	}
	if (button_stage_setting_) {
		button_stage_setting_->SetText("阶段设置");
	}
	if (button_mission_setting_) {
		button_mission_setting_->SetText("任务设置");
	}
	if (button_confirm_) {
		button_confirm_->SetText("确定");
		button_confirm_->SetVisible(false);
	}
	if (button_cancle_) {
		button_cancle_->SetText("取消");
		button_cancle_->SetVisible(false);
	}
	if (dialog_setting_result_) {
		dialog_setting_result_->SetVisible(false);
	}
	if (listview_select_) {
		listview_select_->SetVisible(false);
	}
	image_good_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageGood));
	image_bad_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageBad));
	image_public_speaker_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetPublicSpeaker));
	image_sun_moon_star_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageSunMoonStar));
	if (image_public_speaker_) {
		image_public_speaker_static_rect_ = image_public_speaker_->DeclaredRect();
	}
	if (image_sun_moon_star_) {
		image_sun_moon_star_static_rect_ = image_sun_moon_star_->DeclaredRect();
		image_sun_moon_star_->SetVisible(false);
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

bool WordPracticeApp::LoadUserJson() {
	user_json_ = {};
	user_json_.stage_levelup_count.assign(kDefaultStageLevelupCount.begin(), kDefaultStageLevelupCount.end());
	user_json_.today_mission.today_mission_count = kDefaultTodayMissionCount;
	user_json_.today_mission.today_practice_word = kDefaultTodayPracticeWordCount;
	const std::string content = ReadFileToString(kUserJsonPath);
	if (content.empty()) {
		ESP_LOGW(kTag, "user.json missing or empty path=%s", kUserJsonPath);
		SyncUserProgressState();
		return SaveUserJson();
	}

	cJSON *root = cJSON_Parse(content.c_str());
	if (!root) {
		ESP_LOGW(kTag, "user.json parse failed path=%s preview=%s", kUserJsonPath, BuildJsonLogPreview(content).c_str());
		SyncUserProgressState();
		return SaveUserJson();
	}

	cJSON *users = cJSON_GetObjectItemCaseSensitive(root, "users");
	if (cJSON_IsObject(users)) {
		user_json_.user_id = std::max(0, JsonIntOrDefault(users, "user_id", user_json_.user_id));
		user_json_.name = JsonStringOrDefault(users, "name", user_json_.name);
		user_json_.current_stage = JsonStringOrDefault(users, "current_stage", user_json_.current_stage);
	}

	cJSON *learning_preferences = cJSON_GetObjectItemCaseSensitive(root, "learning_preferences");
	if (cJSON_IsObject(learning_preferences)) {
		user_json_.enable_read_questions = JsonBoolOrDefault(
			learning_preferences,
			"enable_read_questions",
			user_json_.enable_read_questions);
		user_json_.today_mission.today_mission_count = PositiveOrFallback(
			JsonIntOrDefault(learning_preferences, "today_mission_count", user_json_.today_mission.today_mission_count),
			user_json_.today_mission.today_mission_count);
		user_json_.today_mission.today_practice_word = PositiveOrFallback(
			JsonIntOrDefault(learning_preferences, "today_practice_word", user_json_.today_mission.today_practice_word),
			user_json_.today_mission.today_practice_word);
	}

	cJSON *devices = cJSON_GetObjectItemCaseSensitive(root, "devices");
	if (cJSON_IsObject(devices)) {
		user_json_.device.device_id = JsonStringOrDefault(devices, "device_id");
		user_json_.device.firmware = JsonStringOrDefault(devices, "firmware");
	}

	cJSON *settings = cJSON_GetObjectItemCaseSensitive(root, "settings");
	if (cJSON_IsObject(settings)) {
		user_json_.enable_read_questions = JsonBoolOrDefault(settings, "enable_read_questions", user_json_.enable_read_questions);
	}

	cJSON *practice_stats = cJSON_GetObjectItemCaseSensitive(root, "practice_stats");
	if (cJSON_IsObject(practice_stats)) {
		user_json_.practice_stats.continuous_days = std::max(1, JsonIntOrDefault(practice_stats, "continuous_days", user_json_.practice_stats.continuous_days));
		user_json_.practice_stats.last_practice_date = JsonStringOrDefault(practice_stats, "last_practice_date", user_json_.practice_stats.last_practice_date);
	}

	LoadIntArrayFromJson(root, "stage_levelup_count", &user_json_.stage_levelup_count, 20);
	for (size_t i = 0; i < std::min(user_json_.stage_levelup_count.size(), kDefaultStageLevelupCount.size()); ++i) {
		if (user_json_.stage_levelup_count[i] <= 0) {
			user_json_.stage_levelup_count[i] = kDefaultStageLevelupCount[i];
		}
	}
	const int mission_target_words = std::max(1, user_json_.today_mission.today_mission_count);
		WP_PERSIST_LOGW(
		kTag,
		"user.json loaded path=%s name=%s stage=%s mission_count=%d practice_words=%d completed=%d target=%d speak=%d preview=%s",
		kUserJsonPath,
		user_json_.name.c_str(),
		user_json_.current_stage.c_str(),
		user_json_.today_mission.today_mission_count,
		user_json_.today_mission.today_practice_word,
		user_json_.today_mission.completed_words,
		mission_target_words,
		user_json_.enable_read_questions ? 1 : 0,
		BuildJsonLogPreview(content).c_str());
	cJSON_Delete(root);
	SyncUserProgressState();
	return true;
}

bool WordPracticeApp::SaveUserJson() const {
	cJSON *root = cJSON_CreateObject();
	if (!root) {
		return false;
	}
	cJSON *users = cJSON_CreateObject();
	cJSON_AddNumberToObject(users, "user_id", user_json_.user_id);
	cJSON_AddStringToObject(users, "name", user_json_.name.c_str());
	cJSON_AddStringToObject(users, "current_stage", user_json_.current_stage.c_str());
	cJSON_AddItemToObject(root, "users", users);

	cJSON *devices = cJSON_CreateObject();
	cJSON_AddStringToObject(devices, "device_id", user_json_.device.device_id.c_str());
	cJSON_AddStringToObject(devices, "firmware", user_json_.device.firmware.c_str());
	cJSON_AddItemToObject(root, "devices", devices);

	cJSON *settings = cJSON_CreateObject();
	cJSON_AddBoolToObject(settings, "enable_read_questions", user_json_.enable_read_questions);
	cJSON_AddItemToObject(root, "settings", settings);

	cJSON *learning_preferences = cJSON_CreateObject();
	cJSON_AddBoolToObject(learning_preferences, "enable_read_questions", user_json_.enable_read_questions);
	cJSON_AddNumberToObject(learning_preferences, "today_mission_count", std::max(1, user_json_.today_mission.today_mission_count));
	cJSON_AddNumberToObject(learning_preferences, "today_practice_word", std::max(1, user_json_.today_mission.today_practice_word));
	cJSON_AddItemToObject(root, "learning_preferences", learning_preferences);

	cJSON *practice_stats = cJSON_CreateObject();
	cJSON_AddNumberToObject(practice_stats, "continuous_days", user_json_.practice_stats.continuous_days);
	cJSON_AddStringToObject(practice_stats, "last_practice_date", user_json_.practice_stats.last_practice_date.c_str());
	cJSON_AddItemToObject(root, "practice_stats", practice_stats);

	cJSON *levelup_array = cJSON_CreateArray();
	for (int value : user_json_.stage_levelup_count) {
		cJSON_AddItemToArray(levelup_array, cJSON_CreateNumber(value));
	}
	cJSON_AddItemToObject(root, "stage_levelup_count", levelup_array);

	char *printed = cJSON_Print(root);
	const std::string output = printed ? printed : "{}";
	if (printed) {
		cJSON_free(printed);
	}
	cJSON_Delete(root);
	const bool ok = WriteStringToFile(kUserJsonPath, output);
	ESP_LOGI(
		kTag,
		"user.json save %s path=%s mission_count=%d practice_words=%d completed=%d target=%d speak=%d preview=%s",
		ok ? "ok" : "failed",
		kUserJsonPath,
		user_json_.today_mission.today_mission_count,
		user_json_.today_mission.today_practice_word,
		user_json_.today_mission.completed_words,
		std::max(1, user_json_.today_mission.today_mission_count),
		user_json_.enable_read_questions ? 1 : 0,
		BuildJsonLogPreview(output).c_str());
	return ok;
}

int WordPracticeApp::CurrentStageIndex() const {
	return ParseStageIndex(user_json_.current_stage);
}

int WordPracticeApp::ComputeDisplayLevel() const {
	const int stage_index = std::max(1, CurrentStageIndex()) - 1;
	const int quantity = user_json_.mastered_words;
	const int threshold = (stage_index >= 0 && stage_index < static_cast<int>(user_json_.stage_levelup_count.size()))
		? std::max(1, user_json_.stage_levelup_count[static_cast<size_t>(stage_index)])
		: 10;
	const int derived_level = std::max(0, quantity / threshold);
	return derived_level;
}

int WordPracticeApp::QueryMasteredWordCount() const {
	const std::string user_db = result_module_.ProgressDao().DiscoverUserDbPath();
	if (user_db.empty()) {
		return user_json_.mastered_words;
	}
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return user_json_.mastered_words;
	}
	(void)mastery_dao_.EnsureTables(db);
	const std::string sql = "SELECT COUNT(1) FROM word_learning_profile WHERE user_id=? AND " + MasteredProfileSqlCondition() + ";";
	sqlite3_stmt *stmt = nullptr;
	int mastered_words = user_json_.mastered_words;
	if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK && stmt) {
		sqlite3_bind_int(stmt, 1, current_user_id_);
		if (sqlite3_step(stmt) == SQLITE_ROW) {
			mastered_words = std::max(0, sqlite3_column_int(stmt, 0));
		}
	}
	if (stmt) {
		sqlite3_finalize(stmt);
	}
	sqlite3_close(db);
	return mastered_words;
}

int WordPracticeApp::LoadStageCursorState(int stage_index) const {
	return result_module_.ProgressDao().QueryAppStateInt(StageCursorStateKey(stage_index), 0);
}

void WordPracticeApp::SaveStageCursorState(int stage_index, int cursor_value) {
	const int normalized_cursor = std::max(0, cursor_value);
	if (!result_module_.ProgressDao().SaveAppStateInt(StageCursorStateKey(stage_index), normalized_cursor)) {
		WP_PERSIST_LOGW(kTag, "save stage cursor failed stage=%d cursor=%d", stage_index, normalized_cursor);
	}
}

void WordPracticeApp::SyncUserProgressState() {
	EnsureIntVectorSize(&user_json_.stage_levelup_count, 12, 20);
	user_json_.mastered_words = QueryMasteredWordCount();
	const std::string textbook_name = [&]() {
		const std::string preferred_textbook_name = StageNumberToTag(CurrentStageIndex());
		return preferred_textbook_name.empty() ? std::string("default") : preferred_textbook_name;
	}();
	user_json_.level = ComputeDisplayLevel();
	const word_practice::DailyProgressState daily_progress = result_module_.ProgressDao().QueryDailyProgress(textbook_name);
	user_json_.today_mission.completed_words = std::max(0, daily_progress.completed_words);
	user_json_.today_progress_percent = std::max(0, daily_progress.progress_percent);
}

std::string WordPracticeApp::BuildTodayMissionText() const {
	return "今日任务：答对 " + std::to_string(std::max(1, user_json_.today_mission.today_mission_count)) +
		" 个单词，今日练习 " + std::to_string(std::max(1, user_json_.today_mission.today_practice_word)) + " 个单词";
}

std::string WordPracticeApp::BuildWordPreviewText() const {
	std::vector<std::string> words;
	words.reserve(selected_words_.size());
	for (const auto &selected : selected_words_) {
		if (!Trim(selected.word).empty()) {
			words.push_back(Trim(selected.word));
		}
	}
	if (words.empty()) {
		return "今日暂无练习单词";
	}
	return JoinPreviewWords(words, 16);
}

void WordPracticeApp::HideHomeLevelIconLabels() {
	for (auto *label : home_level_icon_labels_) {
		if (label) {
			label->SetVisible(false);
			label->SetText("");
		}
	}
}

void WordPracticeApp::RenderHomeLevelIconLabels() {
	HideHomeLevelIconLabels();
	if (!root_ || image_sun_moon_star_static_rect_.w <= 0 || image_sun_moon_star_static_rect_.h <= 0) {
		return;
	}
	const int level = ComputeDisplayLevel();
	int suns = level / 100;
	int moons = (level % 100) / 10;
	int stars = level % 10;
	std::vector<std::string> icons;
	icons.reserve(static_cast<size_t>(suns + moons + stars));
	for (int i = 0; i < suns; ++i) {
		icons.push_back("☀");
	}
	for (int i = 0; i < moons; ++i) {
		icons.push_back("☾");
	}
	for (int i = 0; i < stars; ++i) {
		icons.push_back("★");
	}
	if (icons.empty()) {
		icons.push_back("☆");
	}
	if (static_cast<int>(icons.size()) > kLevelIconMaxCount) {
		icons.resize(static_cast<size_t>(kLevelIconMaxCount));
		icons.back() = "+";
	}
	for (size_t index = 0; index < icons.size(); ++index) {
		if (index >= home_level_icon_labels_.size()) {
			auto *label = dynamic_cast<app_ui::LabelWidget *>(root_->AddChild(std::make_unique<app_ui::LabelWidget>()));
			if (!label) {
				break;
			}
			label->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			label->SetFontName("wenquanyi_11pt");
			home_level_icon_labels_.push_back(label);
		}
		auto *icon_label = home_level_icon_labels_[index];
		if (!icon_label) {
			continue;
		}
		const int row = static_cast<int>(index) / kLevelIconMaxPerRow;
		const int col = static_cast<int>(index) % kLevelIconMaxPerRow;
		icon_label->SetRectInParent({
			static_cast<int16_t>(image_sun_moon_star_static_rect_.x + col * kLevelIconCellSize),
			static_cast<int16_t>(image_sun_moon_star_static_rect_.y + row * kLevelIconCellSize),
			static_cast<int16_t>(kLevelIconCellSize),
			static_cast<int16_t>(kLevelIconCellSize)});
		icon_label->SetText(icons[index]);
		icon_label->SetVisible(true);
	}
}

void WordPracticeApp::RefreshHomePreview() {
	SyncUserProgressState();
	SetWidgetVisibleById(kWidgetHomeFrameTop, true);
	SetWidgetVisibleById(kWidgetHomeFrameBottom, true);
	if (label_question_type_) {
		label_question_type_->SetVisible(false);
	}
	if (label_correct_count_) {
		label_correct_count_->SetVisible(false);
	}
	if (label_wrong_count_) {
		label_wrong_count_->SetVisible(false);
	}
	if (label_question_) {
		label_question_->SetVisible(false);
	}
	if (label_alert_) {
		label_alert_->SetVisible(false);
	}
	if (label_asr_result_) {
		label_asr_result_->SetVisible(false);
	}
	if (label_press_aread_) {
		label_press_aread_->SetVisible(false);
	}
	if (label_press_d_skip_) {
		label_press_d_skip_->SetVisible(false);
	}
	if (label_a_) {
		label_a_->SetVisible(false);
	}
	if (label_b_) {
		label_b_->SetVisible(false);
	}
	if (label_c_) {
		label_c_->SetVisible(false);
	}
	if (label_d_) {
		label_d_->SetVisible(false);
	}
	if (label_up_) {
		label_up_->SetVisible(false);
	}
	if (label_left_) {
		label_left_->SetVisible(false);
	}
	if (label_down_) {
		label_down_->SetVisible(false);
	}
	if (label_right_) {
		label_right_->SetVisible(false);
	}
	if (label_question_line2_) {
		label_question_line2_->SetVisible(false);
	}
	if (label_question_line3_) {
		label_question_line3_->SetVisible(false);
	}
	if (question_dash_line1_) {
		question_dash_line1_->SetVisible(false);
	}
	if (question_dash_line2_) {
		question_dash_line2_->SetVisible(false);
	}
	if (question_dash_line3_) {
		question_dash_line3_->SetVisible(false);
	}
	if (label_asr_line2_) {
		label_asr_line2_->SetVisible(false);
	}
	if (label_asr_line3_) {
		label_asr_line3_->SetVisible(false);
	}
	if (asr_dash_line1_) {
		asr_dash_line1_->SetVisible(false);
	}
	if (asr_dash_line2_) {
		asr_dash_line2_->SetVisible(false);
	}
	if (asr_dash_line3_) {
		asr_dash_line3_->SetVisible(false);
	}
	if (textarea_input_answer_) {
		textarea_input_answer_->SetVisible(false);
	}
	if (dialog_select_board_) {
		dialog_select_board_->SetVisible(false);
	}
	if (label_my_level_) {
		label_my_level_->SetVisible(true);
		label_my_level_->SetText("我的等级：" + std::to_string(ComputeDisplayLevel()));
	}
	if (label_my_stage_) {
		label_my_stage_->SetVisible(true);
		label_my_stage_->SetText("我的阶段：" + StageKey(CurrentStageIndex()));
	}
	if (label_read_setting_) {
		label_read_setting_->SetVisible(true);
		label_read_setting_->SetText("设置已迁移到系统设置");
	}
	if (label_today_mission_) {
		label_today_mission_->SetVisible(true);
		label_today_mission_->SetText(BuildTodayMissionText());
	}
	if (label_word_preview_) {
		label_word_preview_->SetVisible(true);
		label_word_preview_->SetText(BuildWordPreviewText());
	}
	if (label_mission_progress_) {
		label_mission_progress_->SetVisible(true);
		std::string progress_text = "今日目标完成：" + std::to_string(user_json_.today_progress_percent) + "%";
		if (user_json_.today_progress_percent > 100) {
			progress_text += "（超额完成）";
		}
		label_mission_progress_->SetText(progress_text);
	}
	if (checkbox_has_read_) {
		checkbox_has_read_->SetVisible(false);
	}
	if (checkbox_no_read_) {
		checkbox_no_read_->SetVisible(false);
	}
	if (button_stage_setting_) {
		button_stage_setting_->SetVisible(false);
	}
	if (button_mission_setting_) {
		button_mission_setting_->SetVisible(false);
	}
	if (button_confirm_) {
		button_confirm_->SetVisible(false);
	}
	if (button_cancle_) {
		button_cancle_->SetVisible(false);
	}
	if (dialog_setting_result_) {
		dialog_setting_result_->SetVisible(false);
	}
	if (listview_select_) {
		listview_select_->SetVisible(false);
	}
	if (progress_today_mission_) {
		progress_today_mission_->SetVisible(true);
		auto profile = progress_today_mission_->Profile();
		profile.value = static_cast<uint8_t>(ClampPercent(user_json_.today_progress_percent));
		profile.max_value = 100;
		progress_today_mission_->SetProfile(profile);
	}
	UpdatePracticeProgressWidget(false);
	if (image_public_speaker_) {
		image_public_speaker_->SetVisible(false);
	}
	if (root_ != nullptr) {
		auto hide_public_image = [this](uint32_t widget_id) {
			if (root_ == nullptr) {
				return;
			}
			if (auto *image = dynamic_cast<app_ui::ImageWidget *>(root_->FindById(widget_id))) {
				image->SetVisible(false);
			}
		};
		hide_public_image(kWidgetPublicTeacher);
		hide_public_image(kWidgetPublicCup);
		hide_public_image(kWidgetPublicCorrect);
		hide_public_image(kWidgetPublicWrong);
		hide_public_image(kWidgetPublicSpeaker);
	}
	if (image_good_) {
		image_good_->SetVisible(false);
	}
	if (image_bad_) {
		image_bad_->SetVisible(false);
	}
	if (image_a_) {
		image_a_->SetVisible(false);
	}
	if (image_b_) {
		image_b_->SetVisible(false);
	}
	if (image_c_) {
		image_c_->SetVisible(false);
	}
	if (image_d_) {
		image_d_->SetVisible(false);
	}
	if (image_write_) {
		image_write_->SetVisible(false);
	}
	if (image_input_) {
		image_input_->SetVisible(false);
	}
	HideSettlementLearnedWordLabels();
	HideHomeLevelIconLabels();
	RenderHomeLevelIconLabels();
	if (bottom_bar_) {
		bottom_bar_->SetText("Start开始练习");
	}
}

void WordPracticeApp::HandleHomePreviewAction(AppContext &ctx, const ButtonEvent &event) {
	if (!IsClickLike(event)) {
		return;
	}
	if (event.id == AppButton::Start) {
		StartPracticeRound(ctx);
	}
}

WordPracticeApp::SessionSummaryData WordPracticeApp::BuildSessionSummaryData() const {
	SessionSummaryData summary;
	const word_practice::BatchProgressSummary batch_summary = batch_progress_tracker_.BuildSummary();
	const word_practice::SessionEvaluation evaluation = EvaluateSession();
	summary.total_questions = cycle_correct_count_ + cycle_wrong_count_;
	summary.wrong_questions = cycle_wrong_count_;
	summary.skipped_questions = cycle_skip_count_;
	summary.accuracy_percent = evaluation.correct_answers + evaluation.wrong_answers > 0
		? static_cast<int>(evaluation.accuracy * 100.0f)
		: 0;
	summary.duration_seconds = std::max(0, static_cast<int>(NowSec()) - session_started_at_sec_);
	summary.new_word_total = batch_summary.new_total;
	summary.new_word_mastered = batch_summary.new_completed;
	summary.review_word_total = batch_summary.review_total + batch_summary.weak_total;
	summary.review_word_correct = batch_summary.review_completed + batch_summary.weak_completed;
	summary.mastered_before = session_mastered_words_before_;
	summary.mastered_after = QueryMasteredWordCount();
	summary.progress_before = session_progress_before_;
	summary.progress_after = std::max(0, user_json_.today_progress_percent);
	summary.level_up = ComputeDisplayLevel() > user_json_.level;
	std::unordered_set<std::string> seen;
	for (const auto &word : wrong_words_this_round_) {
		const std::string token = Trim(word);
		if (!token.empty() && seen.insert(token).second) {
			summary.wrong_words.push_back(token);
		}
	}
	const std::string today = TodayDateString();
	if (user_json_.practice_stats.last_practice_date.empty()) {
		summary.continuous_days = std::max(1, user_json_.practice_stats.continuous_days);
	} else if (user_json_.practice_stats.last_practice_date == today) {
		summary.continuous_days = std::max(1, user_json_.practice_stats.continuous_days);
	} else {
		summary.continuous_days = std::max(1, user_json_.practice_stats.continuous_days + 1);
	}
	return summary;
}

std::string WordPracticeApp::BuildSettlementDialogText(const SessionSummaryData &summary) const {
	const int minutes = summary.duration_seconds / 60;
	const int seconds = summary.duration_seconds % 60;
	const char *encourage = summary.accuracy_percent >= 90 ? "太强了！" : (summary.accuracy_percent >= 70 ? "不错！" : "再接再厉");
	std::string text = "🎉 本次学习完成！\n";
	text += "正确率：" + std::to_string(summary.accuracy_percent) + "%\n";
	text += "用时：" + std::to_string(minutes) + "分" + std::to_string(seconds) + "秒\n";
	text += std::string(summary.accuracy_percent >= 90 ? "90%+ 👉 " : (summary.accuracy_percent >= 70 ? "70-90 👉 " : "<70 👉 ")) + encourage + "\n";
	text += "动画显示区域\n\n";
	text += "📘 学习数据\n\n";
	text += "新词学习：" + std::to_string(summary.new_word_total) + "（掌握 " + std::to_string(summary.new_word_mastered) + "）\n";
	text += "复习单词：" + std::to_string(summary.review_word_total) + "（正确 " + std::to_string(summary.review_word_correct) + "）\n";
	text += "总题数：" + std::to_string(summary.total_questions) + "\n";
	text += "错误：" + std::to_string(summary.wrong_questions) + "\n";
	text += "跳过：" + std::to_string(summary.skipped_questions) + "\n\n";
	text += "📊 进度提升\n\n";
	text += "掌握词汇量：" + std::to_string(summary.mastered_before) + " → " + std::to_string(summary.mastered_after) + "\n";
	text += "本日进度：" + std::to_string(summary.progress_before) + "% → " + std::to_string(summary.progress_after) + "%\n\n";
	text += summary.level_up ? "🏅 升级1颗星\n\n" : "🏅 本轮未升级\n\n";
	text += "🔥 连续学习：第 " + std::to_string(summary.continuous_days) + " 天\n";
	text += "+1 连击！\n\n";
	text += "今日目标完成：" + std::to_string(summary.progress_after) + "%\n\n";
	text += "❌ 本次错词（" + std::to_string(summary.wrong_words.size()) + "个）错词总结：\n\n";
	if (summary.wrong_words.empty()) {
		text += "无\n";
	} else {
		for (const auto &word : summary.wrong_words) {
			text += word + "\n";
		}
	}
	return text;
}

void WordPracticeApp::HandleSettlementAction(AppContext &ctx, const ButtonEvent &event) {
	if (!IsClickLike(event)) {
		return;
	}
	if (event.id == AppButton::Left) {
		dialog_focus_ = DialogFocus::Confirm;
		RefreshSelectionDialog();
		return;
	}
	if (event.id == AppButton::Right) {
		dialog_focus_ = DialogFocus::Cancel;
		RefreshSelectionDialog();
		return;
	}
	if (event.id == AppButton::B || event.id == AppButton::Select) {
		ShowHomePreview(ctx);
		return;
	}
	if (event.id == AppButton::Start) {
		StartPracticeRound(ctx);
	}
}

bool WordPracticeApp::CanReuseQuestionPool(int stage_index, int stage_cursor_index, int next_new_word_id) const {
	return question_pool_cache_valid_ &&
		cached_question_pool_user_id_ == current_user_id_ &&
		cached_question_pool_stage_index_ == stage_index &&
		cached_question_pool_stage_cursor_index_ == stage_cursor_index &&
		cached_question_pool_new_word_cursor_ == next_new_word_id &&
		cached_question_pool_enable_speak_questions_ == enable_speak_questions_ &&
		cached_question_pool_selection_config_.total_word_count == word_selection_config_.total_word_count &&
		!selected_words_.empty() &&
		!question_seed_pool_.empty() &&
		!available_question_types_by_word_.empty() &&
		!mastery_profiles_.empty();
}

void WordPracticeApp::UpdateQuestionPoolCacheState(int stage_index, int stage_cursor_index, int next_new_word_id) {
	question_pool_cache_valid_ = !selected_words_.empty() &&
		!question_seed_pool_.empty() &&
		!available_question_types_by_word_.empty() &&
		!mastery_profiles_.empty();
	cached_question_pool_user_id_ = current_user_id_;
	cached_question_pool_stage_index_ = stage_index;
	cached_question_pool_stage_cursor_index_ = stage_cursor_index;
	cached_question_pool_new_word_cursor_ = next_new_word_id;
	cached_question_pool_selection_config_ = word_selection_config_;
	cached_question_pool_enable_speak_questions_ = enable_speak_questions_;
}

void WordPracticeApp::InvalidateQuestionPoolCache() {
	question_pool_cache_valid_ = false;
	cached_question_pool_user_id_ = -1;
	cached_question_pool_stage_index_ = 0;
	cached_question_pool_stage_cursor_index_ = -1;
	cached_question_pool_new_word_cursor_ = -1;
	cached_question_pool_selection_config_ = {};
	cached_question_pool_enable_speak_questions_ = enable_speak_questions_;
}

void WordPracticeApp::ResetLoadedQuestionDataCache() {
	seed_cache_.clear();
	seed_cache_stage_index_ = 0;
}

void WordPracticeApp::ResetMasteryProfileCache() {
	mastery_profile_cache_.clear();
	mastery_profile_cache_user_id_ = -1;
	mastery_profile_cache_textbook_name_.clear();
}

void WordPracticeApp::RebuildMasteryProfileCache() {
	mastery_profile_cache_.clear();
	mastery_profile_cache_.reserve(mastery_profiles_.size());
	for (const auto &profile : mastery_profiles_) {
		mastery_profile_cache_[profile.word_id] = profile;
	}
	mastery_profile_cache_user_id_ = current_user_id_;
	mastery_profile_cache_textbook_name_ = current_textbook_name_;
}

void WordPracticeApp::UpdateMasteryProfileCache(const word_practice::WordMasteryProfile &profile) {
	if (mastery_profile_cache_user_id_ != current_user_id_ || mastery_profile_cache_textbook_name_ != current_textbook_name_) {
		ResetMasteryProfileCache();
		mastery_profile_cache_user_id_ = current_user_id_;
		mastery_profile_cache_textbook_name_ = current_textbook_name_;
	}
	mastery_profile_cache_[profile.word_id] = profile;
}

bool WordPracticeApp::TryAppendSeedFromCache(const word_practice::SelectedWord &selected_word) {
	const auto it = seed_cache_.find(selected_word.word_id);
	if (it == seed_cache_.end()) {
		return false;
	}
	word_practice::VocabularySeed cached_seed = it->second;
	cached_seed.is_review = selected_word.is_review;
	if (!Trim(selected_word.word).empty()) {
		cached_seed.word = Trim(selected_word.word);
	}
	if (!Trim(selected_word.image).empty()) {
		cached_seed.image = Trim(selected_word.image);
	}
	question_seed_pool_.push_back(std::move(cached_seed));
	return true;
}

const word_practice::WordMasteryProfile *WordPracticeApp::FindCachedMasteryProfile(int word_id) const {
	const auto it = mastery_profile_cache_.find(word_id);
	return it == mastery_profile_cache_.end() ? nullptr : &it->second;
}

bool WordPracticeApp::WarmQuestionCandidates(size_t target_seed_count, const char *reason) {
	if (selected_words_.empty()) {
		question_seed_pool_.clear();
		available_question_types_by_word_.clear();
		next_seed_pool_load_index_ = 0;
		return false;
	}

	const size_t desired_seed_count = std::min(target_seed_count, selected_words_.size());
	const size_t seed_count_before = question_seed_pool_.size();
	const size_t available_before = available_question_types_by_word_.size();
	const int stage_index = CurrentStageIndex();
	if (seed_cache_stage_index_ != stage_index) {
		ResetLoadedQuestionDataCache();
		seed_cache_stage_index_ = stage_index;
	}
	const int64_t warm_start_ms = NowMs();
	int loaded_count = 0;
	int failed_count = 0;
	int cache_hit_count = 0;

	std::unordered_set<int> loaded_word_ids;
	loaded_word_ids.reserve(question_seed_pool_.size());
	for (const auto &seed : question_seed_pool_) {
		loaded_word_ids.insert(seed.word_id);
	}

	while (question_seed_pool_.size() < desired_seed_count && next_seed_pool_load_index_ < selected_words_.size()) {
		std::vector<word_practice::SelectedWord> pending_words;
		pending_words.reserve(desired_seed_count - question_seed_pool_.size());
		while (question_seed_pool_.size() + pending_words.size() < desired_seed_count &&
		       next_seed_pool_load_index_ < selected_words_.size()) {
			const word_practice::SelectedWord &selected_word = selected_words_[next_seed_pool_load_index_++];
			if (loaded_word_ids.count(selected_word.word_id)) {
				continue;
			}
			if (TryAppendSeedFromCache(selected_word)) {
				++loaded_count;
				++cache_hit_count;
				loaded_word_ids.insert(selected_word.word_id);
				continue;
			}
			pending_words.push_back(selected_word);
		}
		if (pending_words.empty()) {
			continue;
		}
		std::vector<word_practice::VocabularySeed> loaded_seeds =
			question_seed_module_.LoadVocabularySeedsForWords(pending_words, stage_index);
		failed_count += static_cast<int>(pending_words.size() - loaded_seeds.size());
		for (auto &loaded_seed : loaded_seeds) {
			if (loaded_word_ids.count(loaded_seed.word_id)) {
				continue;
			}
			seed_cache_[loaded_seed.word_id] = loaded_seed;
			loaded_word_ids.insert(loaded_seed.word_id);
			question_seed_pool_.push_back(std::move(loaded_seed));
			++loaded_count;
		}
	}

	const int64_t build_available_start_ms = NowMs();
	available_question_types_by_word_ = question_seed_module_.BuildAvailableQuestionTypesByWord(
		question_seed_pool_,
		mastery_profiles_,
		enable_speak_questions_,
		current_learning_mode_);
	const int64_t build_available_ms = NowMs() - build_available_start_ms;

	WP_APP_DBLOGW(kTag,
		"warm question candidates reason=%s target=%d loaded_now=%d cache_hits=%d failed_now=%d seeds=%d available_words=%d next_index=%d build_available_ms=%d total_ms=%d",
		reason != nullptr ? reason : "unknown",
		static_cast<int>(desired_seed_count),
		loaded_count,
		cache_hit_count,
		failed_count,
		static_cast<int>(question_seed_pool_.size()),
		static_cast<int>(available_question_types_by_word_.size()),
		static_cast<int>(next_seed_pool_load_index_),
		static_cast<int>(build_available_ms),
		static_cast<int>(NowMs() - warm_start_ms));

	return question_seed_pool_.size() != seed_count_before ||
		available_question_types_by_word_.size() != available_before;
}

void WordPracticeApp::LoadQuestionPool() {
	const int64_t load_start_ms = NowMs();
	InvalidateQuestionPoolCache();
	selected_words_.clear();
	question_seed_pool_.clear();
	available_question_types_by_word_.clear();
	next_seed_pool_load_index_ = 0;
	mastery_profiles_.clear();
	ResetMasteryProfileCache();
	learning_batch_ = {};
	batch_progress_tracker_.Reset(learning_batch_, current_learning_mode_);
	current_scheduled_question_ = {};
	current_question_slot_ = {};
	current_round_goal_text_.clear();
	last_attempt_feedback_text_.clear();
	current_learning_mode_ = word_practice::LearningMode::Normal;
	if (!eteacher::database_manager::EnsureSqliteRuntimeReady(kTag)) {
		ESP_LOGE(kTag, "sqlite runtime init failed");
		return;
	}
	(void)eteacher::database_manager::EnsureSqliteSdMounted(kTag);
	WP_APP_DBLOGW(kTag,
		"load question pool start requested_total=%d selection_mode=total_words",
		word_selection_config_.total_word_count);
	const int stage_index = CurrentStageIndex();
	if (seed_cache_stage_index_ != 0 && seed_cache_stage_index_ != stage_index) {
		ResetLoadedQuestionDataCache();
	}
	seed_cache_stage_index_ = stage_index;
	eteacher::app_ui::SetWordResourceStage(stage_index);
	current_textbook_name_ = "default";
	const std::string preferred_textbook_name = StageNumberToTag(stage_index);
	if (!preferred_textbook_name.empty()) {
		current_textbook_name_ = preferred_textbook_name;
	}
	const int stage_cursor_index = std::max(0, std::min(11, stage_index - 1));
	const int64_t select_words_start_ms = NowMs();
	const int persisted_next_new_word_id = LoadStageCursorState(stage_index);
	int next_new_word_id = persisted_next_new_word_id;
	const std::vector<word_practice::SelectedWord> selected_words =
		selection_module_.SelectWordsFromVocabulary(
			word_selection_config_,
			current_user_id_,
			stage_index,
			current_textbook_name_,
			next_new_word_id,
			&next_new_word_id);
	const int64_t select_words_end_ms = NowMs();
	WP_APP_DBLOGW(kTag, "load question pool selected_words=%d", static_cast<int>(selected_words.size()));
	const int64_t build_pool_start_ms = NowMs();
	selected_words_ = selected_words;
	sqlite3 *shared_user_db = nullptr;
	const int64_t shared_db_open_start_ms = NowMs();
	const std::string shared_user_db_path = mastery_dao_.DiscoverUserDbPath();
	if (!shared_user_db_path.empty()) {
		if (sqlite3_open_v2(shared_user_db_path.c_str(), &shared_user_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK ||
			shared_user_db == nullptr) {
			if (shared_user_db != nullptr) {
				sqlite3_close(shared_user_db);
				shared_user_db = nullptr;
			}
		} else {
			(void)mastery_dao_.EnsureTables(shared_user_db);
			(void)result_module_.ProgressDao().EnsureStatsTables(shared_user_db);
		}
	}
	const int64_t shared_db_open_end_ms = NowMs();
	const int64_t save_cursor_start_ms = NowMs();
	if (shared_user_db != nullptr && next_new_word_id != persisted_next_new_word_id) {
		(void)result_module_.ProgressDao().SaveAppStateInt(shared_user_db, StageCursorStateKey(stage_index), next_new_word_id);
	} else if (next_new_word_id != persisted_next_new_word_id) {
		SaveStageCursorState(stage_index, next_new_word_id);
	}
	const int64_t save_cursor_end_ms = NowMs();
	const int64_t profile_load_start_ms = NowMs();
	mastery_profiles_ = shared_user_db != nullptr
		? mastery_dao_.LoadProfiles(shared_user_db, selected_words, current_textbook_name_)
		: mastery_dao_.LoadProfiles(selected_words, current_textbook_name_);
	const int64_t profile_load_end_ms = NowMs();
	const int64_t decay_start_ms = NowMs();
	const int decayed_profile_count = shared_user_db != nullptr
		? mastery_dao_.ApplyDueDecayIfNeeded(shared_user_db, &mastery_profiles_)
		: mastery_dao_.ApplyDueDecayIfNeeded(&mastery_profiles_);
	const int64_t decay_end_ms = NowMs();
	if (shared_user_db != nullptr) {
		sqlite3_close(shared_user_db);
	}
	const int64_t rebuild_cache_start_ms = NowMs();
	RebuildMasteryProfileCache();
	const int64_t rebuild_cache_end_ms = NowMs();
	const int64_t learning_mode_start_ms = NowMs();
	current_learning_mode_ = question_seed_module_.DetermineLearningMode(selected_words_, mastery_profiles_);
	const int64_t learning_mode_end_ms = NowMs();
	const int64_t seed_load_start_ms = NowMs();
	(void)WarmQuestionCandidates(word_practice::config::kInitialQuestionSeedWarmupCount, "startup");
	const int64_t seed_load_end_ms = NowMs();
	const int64_t available_types_start_ms = NowMs();
	while (available_question_types_by_word_.empty() && next_seed_pool_load_index_ < selected_words_.size()) {
		const size_t next_target = question_seed_pool_.size() + word_practice::config::kIncrementalQuestionSeedWarmupCount;
		if (!WarmQuestionCandidates(next_target, "startup_expand")) {
			break;
		}
	}
	const int64_t available_types_end_ms = NowMs();
	const int64_t build_pool_end_ms = NowMs();
	std::array<int, 13> pool_type_count = {};
	for (const auto &entry : available_question_types_by_word_) {
		for (int question_type : entry.second) {
			if (question_type >= 1 && question_type <= 12) {
				++pool_type_count[static_cast<size_t>(question_type)];
			}
		}
	}
	WP_DIAG_LOGW(kTag,
		"load question pool available_words=%d by type t1=%d t2=%d t3=%d t4=%d t5=%d t6=%d t7=%d t8=%d t9=%d t10=%d t11=%d t12=%d",
		static_cast<int>(available_question_types_by_word_.size()),
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
	if (!question_seed_pool_.empty()) {
		const std::string seed_stage = StageNumberToTag(question_seed_pool_.front().stage);
		if (!seed_stage.empty()) {
			current_textbook_name_ = seed_stage;
		}
	}
	current_question_slot_ = {};
	const int64_t batch_build_start_ms = NowMs();
	learning_batch_ = batch_planner_.Build(selected_words_, mastery_profiles_, current_learning_mode_);
	const int64_t batch_build_end_ms = NowMs();
	batch_progress_tracker_.Reset(learning_batch_, current_learning_mode_);
	question_scheduler_.Reset();
	current_scheduled_question_ = {};
	current_round_goal_text_ = BuildRoundGoalText();
	last_attempt_feedback_text_.clear();
	round_completion_recorded_ = false;
	UpdateQuestionPoolCacheState(stage_index, stage_cursor_index, next_new_word_id);
	WP_APP_DBLOGW(kTag, "vocabulary question seed pool loaded: seeds=%d available_words=%d",
		static_cast<int>(question_seed_pool_.size()),
		static_cast<int>(available_question_types_by_word_.size()));
	ESP_LOGW(kTag,
		"startup timing load_question_pool total_ms=%d select_words_ms=%d save_cursor_ms=%d shared_db_open_ms=%d profile_load_ms=%d decay_ms=%d rebuild_cache_ms=%d learning_mode_ms=%d seed_load_ms=%d available_types_ms=%d batch_build_ms=%d build_pool_ms=%d decayed_profiles=%d selected_words=%d available_words=%d batch_items=%d",
		static_cast<int>(build_pool_end_ms - load_start_ms),
		static_cast<int>(select_words_end_ms - select_words_start_ms),
		static_cast<int>(save_cursor_end_ms - save_cursor_start_ms),
		static_cast<int>(shared_db_open_end_ms - shared_db_open_start_ms),
		static_cast<int>(profile_load_end_ms - profile_load_start_ms),
		static_cast<int>(decay_end_ms - decay_start_ms),
		static_cast<int>(rebuild_cache_end_ms - rebuild_cache_start_ms),
		static_cast<int>(learning_mode_end_ms - learning_mode_start_ms),
		static_cast<int>(seed_load_end_ms - seed_load_start_ms),
		static_cast<int>(available_types_end_ms - available_types_start_ms),
		static_cast<int>(batch_build_end_ms - batch_build_start_ms),
		static_cast<int>(build_pool_end_ms - build_pool_start_ms),
		decayed_profile_count,
		static_cast<int>(selected_words.size()),
		static_cast<int>(available_question_types_by_word_.size()),
		static_cast<int>(learning_batch_.items.size()));
}

bool WordPracticeApp::PickNextQuestion() {
	if (available_question_types_by_word_.empty()) {
		const size_t next_target = std::max(
			question_seed_pool_.size() + word_practice::config::kIncrementalQuestionSeedWarmupCount,
			static_cast<size_t>(word_practice::config::kInitialQuestionSeedWarmupCount));
		(void)WarmQuestionCandidates(next_target, "schedule_empty");
		if (available_question_types_by_word_.empty()) {
			session_scheduler_exhausted_ = true;
			UpdateSessionState(true);
			return false;
		}
	}

	if (session_module_.IsFinished()) {
		return true;
	}

	const word_practice::ScheduledQuestion scheduled = question_scheduler_.ScheduleNext(
		available_question_types_by_word_,
		learning_batch_,
		mastery_profiles_,
		batch_progress_tracker_,
		current_learning_mode_,
		session_module_.TotalAnswered(),
		session_module_.PassTargetQuestions());
	WP_APP_DBLOGW(kTag,
		"pick next question answered=%d has_value=%d word_id=%d type=%d reason=%d skill=%d",
		session_module_.TotalAnswered(),
		scheduled.has_value ? 1 : 0,
		scheduled.word_id,
		scheduled.question_type,
		static_cast<int>(scheduled.reason_type),
		static_cast<int>(scheduled.target_skill));
	if (!scheduled.has_value) {
		const size_t next_target = question_seed_pool_.size() + word_practice::config::kIncrementalQuestionSeedWarmupCount;
		if (next_seed_pool_load_index_ < selected_words_.size() && WarmQuestionCandidates(next_target, "schedule_retry")) {
			return PickNextQuestion();
		}
		session_scheduler_exhausted_ = true;
		UpdateSessionState(true);
		return session_module_.IsFinished();
	}
	session_scheduler_exhausted_ = false;
	const word_practice::BatchWordPlan *selected_plan = FindBatchPlan(learning_batch_, scheduled.word_id);
	const word_practice::WordMasteryProfile *selected_profile = FindCachedMasteryProfile(scheduled.word_id);
	WP_APP_DBLOGW(kTag,
		"pick next question selected type=%d word_id=%d",
		scheduled.question_type,
		scheduled.word_id);
	ESP_LOGW(kTag,
		"schedule detail word_id=%d word=%s kind=%d reason=%s skill=%s chosen_type=%d stage=%d scores=(%d,%d,%d) checkpoints=(%d,%d) progress=%d shown=%d correct=%d",
		scheduled.word_id,
		selected_plan != nullptr ? selected_plan->selected_word.word.c_str() : "",
		selected_plan != nullptr ? static_cast<int>(selected_plan->kind) : -1,
		word_practice::ToString(scheduled.reason_type),
		word_practice::ToString(scheduled.target_skill),
		scheduled.question_type,
		selected_profile != nullptr ? selected_profile->stage : -1,
		selected_profile != nullptr ? selected_profile->strength : -1,
		selected_profile != nullptr ? selected_profile->recall_score : -1,
		selected_profile != nullptr ? selected_profile->output_score : -1,
		batch_progress_tracker_.HasRecognitionCheckpoint(scheduled.word_id) ? 1 : 0,
		batch_progress_tracker_.HasRecallCheckpoint(scheduled.word_id) ? 1 : 0,
		static_cast<int>(batch_progress_tracker_.ProgressState(scheduled.word_id)),
		selected_plan != nullptr ? selected_plan->shown_count : -1,
		selected_plan != nullptr ? selected_plan->correct_count : -1);
	current_scheduled_question_ = scheduled;
	if (!CommitScheduledQuestion(scheduled)) {
		auto it_types = available_question_types_by_word_.find(scheduled.word_id);
		if (it_types != available_question_types_by_word_.end()) {
			it_types->second.erase(std::remove(it_types->second.begin(), it_types->second.end(), scheduled.question_type), it_types->second.end());
			if (it_types->second.empty()) {
				available_question_types_by_word_.erase(it_types);
			}
		}
		current_scheduled_question_ = {};
		return PickNextQuestion();
	}
	return true;
}

bool WordPracticeApp::CommitScheduledQuestion(const word_practice::ScheduledQuestion &scheduled) {
	if (!scheduled.has_value) {
		return false;
	}
	const int64_t commit_start_ms = NowMs();
	if (FindLoadedVocabularySeed(question_seed_pool_, scheduled.word_id) == nullptr) {
		const word_practice::SelectedWord *selected_word = FindSelectedWord(selected_words_, scheduled.word_id);
		if (selected_word == nullptr) {
			ESP_LOGW(kTag, "generate question missing selected word word_id=%d", scheduled.word_id);
			return false;
		}
		if (!TryAppendSeedFromCache(*selected_word)) {
			word_practice::VocabularySeed loaded_seed;
			const int stage_index = CurrentStageIndex();
			if (!question_seed_module_.LoadVocabularySeedForWord(*selected_word, stage_index, &loaded_seed)) {
				ESP_LOGW(kTag, "load seed on demand failed word_id=%d type=%d", scheduled.word_id, scheduled.question_type);
				return false;
			}
			seed_cache_[loaded_seed.word_id] = loaded_seed;
			question_seed_pool_.push_back(std::move(loaded_seed));
		}
	}
	if (!question_seed_module_.GenerateQuestionOnDemand(
			question_seed_pool_,
			mastery_profiles_,
			scheduled.word_id,
			scheduled.question_type,
			enable_speak_questions_,
			current_learning_mode_,
			&current_question_slot_.current)) {
		ESP_LOGW(kTag, "generate question on demand failed word_id=%d type=%d", scheduled.word_id, scheduled.question_type);
		current_question_slot_ = {};
		return false;
	}
	current_question_slot_.has_value = true;
	current_question_type_ = current_question_slot_.current.type;
	if (current_scheduled_question_.has_value) {
		batch_progress_tracker_.MarkPresented(current_scheduled_question_.word_id);
	}
	PresentCurrentQuestion();
	ESP_LOGW(kTag,
		"commit scheduled question word_id=%d type=%d total_ms=%d",
		scheduled.word_id,
		scheduled.question_type,
		static_cast<int>(NowMs() - commit_start_ms));
	return true;
}

void WordPracticeApp::PresentCurrentQuestion() {
	const int64_t present_start_ms = NowMs();
	const QuestionData *question = current_question_slot_.has_value ? &current_question_slot_.current : nullptr;
	if (!question) {
		return;
	}
	current_question_presented_at_ms_ = present_start_ms;
	HideSettlementLearnedWordLabels();
	const auto &q = *question;
	const bool is_type56 = (q.type == 5 || q.type == 6);

	const std::string scene = quiz_module_.SelectSceneId(q.type);
	const int64_t activate_scene_start_ms = NowMs();
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
	const int64_t activate_scene_end_ms = NowMs();

	const int64_t build_choice_start_ms = NowMs();
	current_choice_ = quiz_module_.Generate(q);
	const int64_t build_choice_end_ms = NowMs();
	current_audio_path_ = BuildQuestionAudioPath(current_choice_.audio_filename);
	if (!current_audio_path_.empty()) {
		ScheduleQuestionAudioAutoPlay();
	} else {
		CancelQuestionAudioAutoPlay();
	}

	if (label_question_type_) {
		label_question_type_->SetText(quiz_module_.TypeTitle(q.type));
	}
	UpdatePracticeProgressWidget(true);
	SyncScoreLabels();
	if (label_alert_) {
		const std::string reason_text = BuildQuestionReasonText(current_scheduled_question_);
		if (session_module_.TotalAnswered() == 0 && !current_round_goal_text_.empty()) {
			label_alert_->SetText(current_round_goal_text_ + (reason_text.empty() ? "" : ("\n" + reason_text)));
		} else {
			label_alert_->SetText(reason_text);
		}
	}
	if (bottom_bar_) {
		bottom_bar_->SetText(quiz_module_.TypeInstruction(q.type));
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
	current_speak_asr_failure_count_ = 0;
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

			const std::string left_word = quiz_module_.NormalizePairWord(pair.substr(0, sep));
			const std::string right_word = quiz_module_.NormalizePairWord(pair.substr(sep + 1));
			if (left_word.empty() || right_word.empty()) {
				continue;
			}

			int left_index = -1;
			for (int i = 0; i < 4; ++i) {
				if (quiz_module_.NormalizePairWord(type4_left_words_[static_cast<size_t>(i)]) == left_word) {
					left_index = i;
					break;
				}
			}

			int right_index = -1;
			for (int i = 0; i < 4; ++i) {
				if (quiz_module_.NormalizePairWord(type4_right_words_[static_cast<size_t>(i)]) == right_word) {
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
		if (label_a_) label_a_->SetVisible(true);
		if (label_b_) label_b_->SetVisible(true);
		if (label_c_) label_c_->SetVisible(true);
		if (label_d_) label_d_->SetVisible(true);
		if (label_up_) label_up_->SetVisible(false);
		if (label_left_) label_left_->SetVisible(false);
		if (label_down_) label_down_->SetVisible(false);
		if (label_right_) label_right_->SetVisible(false);
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
			if (image_a_) image_a_->SetVisible(true);
			if (image_b_) image_b_->SetVisible(true);
			if (image_c_) image_c_->SetVisible(true);
			if (image_d_) image_d_->SetVisible(true);
		} else {
			if (image_a_) image_a_->SetVisible(false);
			if (image_b_) image_b_->SetVisible(false);
			if (image_c_) image_c_->SetVisible(false);
			if (image_d_) image_d_->SetVisible(false);
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
		"present question word_id=%d qtype=%d question_id=%d total_ms=%d activate_scene_ms=%d build_choice_ms=%d audio=%d scene_len=%d prompt_len=%d options=%d",
		current_scheduled_question_.word_id,
		q.type,
		q.id,
		static_cast<int>(NowMs() - present_start_ms),
		static_cast<int>(activate_scene_end_ms - activate_scene_start_ms),
		static_cast<int>(build_choice_end_ms - build_choice_start_ms),
		current_audio_path_.empty() ? 0 : 1,
		static_cast<int>(scene.size()),
		static_cast<int>(current_choice_.prompt.size()),
		static_cast<int>(current_choice_.options.size()));
}

void WordPracticeApp::ShowSessionSummary(bool finalize_round) {
	if (!label_question_) {
		return;
	}
	const word_practice::SessionEvaluation evaluation = EvaluateSession();
	WP_PERSIST_LOGW(kTag,
		"show session summary answered=%d correct=%d wrong=%d success=%d round_recorded=%d overlay=%d",
		session_module_.TotalAnswered(),
		cycle_correct_count_,
		cycle_wrong_count_,
		evaluation.success ? 1 : 0,
		round_completion_recorded_ ? 1 : 0,
		static_cast<int>(overlay_mode_));
	if (finalize_round) {
		MaybeRecordRoundCompletion();
	}
	SyncScoreLabels();
	const bool pass = evaluation.success;
	last_session_wrong_word_ids_.clear();
	for (int word_id : wrong_word_ids_this_round_) {
		if (word_id <= 0) {
			continue;
		}
		if (std::find(last_session_wrong_word_ids_.begin(), last_session_wrong_word_ids_.end(), word_id) ==
			last_session_wrong_word_ids_.end()) {
			last_session_wrong_word_ids_.push_back(word_id);
		}
	}
	last_session_summary_ = BuildSessionSummaryData();
	if (finalize_round) {
		user_json_.mastered_words = last_session_summary_.mastered_after;
		if (last_session_summary_.level_up) {
			user_json_.level = ComputeDisplayLevel();
		}
		user_json_.practice_stats.continuous_days = std::max(1, last_session_summary_.continuous_days);
		user_json_.practice_stats.last_practice_date = TodayDateString();
		SyncUserProgressState();
		(void)SaveUserJson();
	}

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
		label_question_->SetText("结算页面");
	}

	if (image_public_speaker_) {
		image_public_speaker_->SetVisible(false);
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
		label_alert_->SetText(pass ? "本轮已完成。按 Start 重新开始一轮，按 Select 退出 word_practice。" : "本轮已记录，按 Start 重新开始一轮，按 Select 退出 word_practice。\n动画显示区域：预留");
	}
	UpdatePracticeProgressWidget(false);
	RenderSettlementLearnedWordLabels();
	overlay_mode_ = OverlayMode::Settlement;
	dialog_focus_ = DialogFocus::Confirm;
	RefreshSelectionDialog();
	if (bottom_bar_) {
		bottom_bar_->SetText("Start重新开始一轮  Select退出应用");
	}
}

void WordPracticeApp::ResetRoundState() {
	session_module_.ResetForNewRound(pass_target_questions_);
	question_scheduler_.Reset();
	current_scheduled_question_ = {};
	current_question_slot_ = {};
	current_round_goal_text_.clear();
	last_attempt_feedback_text_.clear();
	round_completion_recorded_ = false;
	session_scheduler_exhausted_ = false;
	learned_words_this_round_.clear();
	wrong_words_this_round_.clear();
	wrong_word_ids_this_round_.clear();
	HideSettlementLearnedWordLabels();
	settlement_learned_word_labels_.clear();
	current_speak_asr_failure_count_ = 0;
}

void WordPracticeApp::UpdateSessionState(bool scheduler_exhausted) {
	if (session_module_.IsFinished()) {
		return;
	}
	if (scheduler_exhausted) {
		session_scheduler_exhausted_ = true;
	}
	const word_practice::SessionEvaluation evaluation = EvaluateSession();
	WP_PERSIST_LOGW(kTag,
		"session evaluation finished=%d success=%d completion=%.2f accuracy=%.2f completed_words=%d/%d min_questions=%d gates=(words:%d completion:%d accuracy:%d minimum:%d skills:%d answer_limit:%d progress_gate:%d scheduler_exhausted:%d)",
		evaluation.finished ? 1 : 0,
		evaluation.success ? 1 : 0,
		evaluation.completion,
		evaluation.accuracy,
		evaluation.completed_words,
		evaluation.target_words,
		evaluation.minimum_questions,
		evaluation.detail.pass_words ? 1 : 0,
		evaluation.detail.pass_completion ? 1 : 0,
		evaluation.detail.pass_accuracy ? 1 : 0,
		evaluation.detail.pass_minimum_questions ? 1 : 0,
		evaluation.detail.pass_skill_coverage ? 1 : 0,
		evaluation.detail.finish_by_answer_limit ? 1 : 0,
		evaluation.detail.finish_by_progress_gate ? 1 : 0,
		evaluation.detail.scheduler_exhausted ? 1 : 0);
	if (evaluation.finished) {
		session_module_.FinishNow();
	}
}

int WordPracticeApp::DisplayedPracticeProgressPercent() const {
	const int progress = std::max(0, user_json_.today_progress_percent);
	if (progress < 100) {
		return progress;
	}
	return (progress / 100) * 100;
}

void WordPracticeApp::ResetCycleScoreState() {
	cycle_correct_count_ = 0;
	cycle_wrong_count_ = 0;
	cycle_skip_count_ = 0;
}

void WordPracticeApp::RecordCycleAnswer(bool correct) {
	session_module_.RecordAnswer(correct);
	if (correct) {
		++cycle_correct_count_;
	} else {
		++cycle_wrong_count_;
	}
}

void WordPracticeApp::RecordCycleSkip() {
	session_module_.RecordSkip();
	++cycle_skip_count_;
}

void WordPracticeApp::UpdatePracticeProgressWidget(bool visible) {
	if (!progress_practice_) {
		return;
	}
	const int displayed_percent = DisplayedPracticeProgressPercent();
	if (label_progress_percent_) {
		label_progress_percent_->SetVisible(visible);
		label_progress_percent_->SetText(std::to_string(displayed_percent) + "%");
	}
	progress_practice_->SetVisible(visible);
	if (!visible) {
		return;
	}
	auto profile = progress_practice_->Profile();
	profile.value = static_cast<uint8_t>(ClampPercent(displayed_percent));
	profile.max_value = 100;
	progress_practice_->SetProfile(profile);
}

std::string WordPracticeApp::BuildRoundGoalText() const {
	std::string goal =
		"本轮目标：推进 " + std::to_string(learning_batch_.planned_new_words) +
		" 个新词，巩固 " + std::to_string(learning_batch_.planned_review_words) +
		" 个旧词，修正 " + std::to_string(learning_batch_.planned_weak_words) + " 个弱词";
	if (current_learning_mode_ == word_practice::LearningMode::ColdStart) {
		goal += "\n冷启动轮：先识别，再回忆。";
	} else if (current_learning_mode_ == word_practice::LearningMode::IntensiveReview) {
		goal += "\n强化复习轮：优先弱词和到期复习词。";
	}
	return goal;
}

std::string WordPracticeApp::BuildQuestionReasonText(const word_practice::ScheduledQuestion &scheduled) const {
	if (!scheduled.has_value) {
		return {};
	}
	const std::string skill_text = SkillLabel(scheduled.target_skill);
	switch (scheduled.reason_type) {
		case word_practice::QuestionReasonType::NewWord:
			return "本题原因：本轮新词，当前训练" + skill_text;
		case word_practice::QuestionReasonType::ReviewDue:
			return "本题原因：这个词到复习时间了，当前训练" + skill_text;
		case word_practice::QuestionReasonType::MistakeFollowup:
			return "本题原因：刚才出现错误，立即做纠错巩固";
		case word_practice::QuestionReasonType::WeakReinforce:
			return "本题原因：这个词较弱，继续强化" + skill_text;
		case word_practice::QuestionReasonType::BatchTarget:
		default:
			return "本题原因：完成本轮批次目标，当前训练" + skill_text;
	}
}

std::string WordPracticeApp::BuildWordFeedbackText(const word_practice::WordMasteryProfile &before,
					   const word_practice::WordMasteryProfile &after,
					   word_practice::BatchWordKind kind,
					   word_practice::TrainingSkill skill,
					   bool correct) const {
	if (correct) {
		if (after.stage > before.stage) {
			return "答对了，这个词已进入" + StageLabel(after.stage) + "阶段。";
		}
		if (kind == word_practice::BatchWordKind::WeakWord) {
			return "答对了，这个词已完成本轮修正。";
		}
		if (kind == word_practice::BatchWordKind::ReviewWord) {
			return "答对了，这个词已完成本轮巩固。";
		}
		if (kind == word_practice::BatchWordKind::NewWord && skill == word_practice::TrainingSkill::Recognition) {
			return "答对了，这个词识别更稳了，下一步练回忆。";
		}
		if (kind == word_practice::BatchWordKind::NewWord && skill == word_practice::TrainingSkill::Recall) {
			return "答对了，这个新词已完成本轮推进。";
		}
		return "答对了，这个词正在稳步推进。";
	}

	if (after.stage < before.stage) {
		return "答错了，这个词已降回" + StageLabel(after.stage) + "阶段，后续会继续强化。";
	}
	switch (skill) {
		case word_practice::TrainingSkill::Recognition:
			return "答错了，这个词需要继续强化识别。";
		case word_practice::TrainingSkill::Recall:
			return "答错了，这个词需要继续强化回忆。";
		case word_practice::TrainingSkill::Output:
			return "答错了，这个词输出还不稳定，后续会降负担继续练。";
		case word_practice::TrainingSkill::AdvancedSpeak:
			return "答错了，这个词的高级朗读还不稳定，后续会先降级继续巩固。";
		default:
			return "答错了，这个词会在后续继续强化。";
	}
}

void WordPracticeApp::MaybeRecordRoundCompletion() {
	if (!session_module_.IsFinished() || round_completion_recorded_) {
		return;
	}
	const bool pass = EvaluateSession().success;
	if (result_module_.ProgressDao().RecordRoundCompletion(current_textbook_name_, pass)) {
		round_completion_recorded_ = true;
		++completed_rounds_for_textbook_;
		WP_PERSIST_LOGW(kTag,
			"record round completion ok textbook=%s passed=%d completed_rounds=%d",
			current_textbook_name_.c_str(),
			pass ? 1 : 0,
			completed_rounds_for_textbook_);
	} else {
		WP_PERSIST_LOGW(kTag,
			"record round completion failed textbook=%s passed=%d",
			current_textbook_name_.c_str(),
			pass ? 1 : 0);
	}
}

bool WordPracticeApp::RecordCurrentAttempt(sqlite3 *db, const QuestionData &q, bool correct) {
	if (db == nullptr) {
		WP_PERSIST_LOGW(kTag, "record attempt skipped: db is null qtype=%d", q.type);
		return false;
	}
	if (!current_scheduled_question_.has_value || current_scheduled_question_.word_id <= 0) {
		WP_PERSIST_LOGW(kTag, "record attempt skipped: no scheduled question qtype=%d", q.type);
		return false;
	}
	word_practice::WordMasteryProfile *profile = FindMasteryProfile(&mastery_profiles_, current_scheduled_question_.word_id);
	if (profile == nullptr) {
		WP_PERSIST_LOGW(kTag,
			"record attempt skipped: profile missing word_id=%d qtype=%d textbook=%s",
			current_scheduled_question_.word_id,
			q.type,
			current_textbook_name_.c_str());
		return false;
	}
	word_practice::QuestionAttemptRecord attempt;
	attempt.word_id = current_scheduled_question_.word_id;
	attempt.question_type = q.type;
	attempt.target_skill = current_scheduled_question_.target_skill;
	attempt.reason_type = current_scheduled_question_.reason_type;
	attempt.textbook_name = current_choice_.textbook_name.empty() ? current_textbook_name_ : current_choice_.textbook_name;
	attempt.question_reason = current_scheduled_question_.reason_text;
	attempt.correct = correct;
	attempt.response_time_ms = current_question_presented_at_ms_ > 0
		? static_cast<int>(std::max<int64_t>(0, NowMs() - current_question_presented_at_ms_))
		: 0;
	attempt.practiced_at = word_practice::CurrentPersistentEpochSeconds();
	const word_practice::BatchWordKind kind = [&]() {
		const word_practice::BatchWordPlan *plan = FindBatchPlan(learning_batch_, current_scheduled_question_.word_id);
		return plan != nullptr ? plan->kind : word_practice::BatchWordKind::ReviewWord;
	}();
	const word_practice::WordMasteryProfile before = *profile;
	const bool apply_attempt_ok = mastery_dao_.ApplyAttempt(db, profile, attempt);
	if (!apply_attempt_ok) {
		WP_PERSIST_LOGW(
			kTag,
			"apply attempt failed word_id=%d textbook=%s qtype=%d skill=%s reason=%s",
			attempt.word_id,
			attempt.textbook_name.c_str(),
			attempt.question_type,
			word_practice::ToString(attempt.target_skill),
			word_practice::ToString(attempt.reason_type));
		*profile = before;
		return false;
	} else {
		WP_PERSIST_LOGI(
			kTag,
			"apply attempt ok word_id=%d textbook=%s qtype=%d stage=%d->%d strength=%d mastered=%d response_ms=%d",
			attempt.word_id,
			attempt.textbook_name.c_str(),
			attempt.question_type,
			before.stage,
			profile->stage,
			profile->strength,
			static_cast<int>(profile->mastered),
			attempt.response_time_ms);
	}
	UpdateMasteryProfileCache(*profile);
	batch_progress_tracker_.MarkOutcome(
		attempt.word_id,
		kind,
		attempt.target_skill,
		attempt.correct);
		question_scheduler_.RecordResult(current_scheduled_question_);
	last_attempt_feedback_text_ = BuildWordFeedbackText(before, *profile, kind, attempt.target_skill, correct);
	if (!correct) {
		const std::string wrong_word = !Trim(current_choice_.source_word).empty() ? Trim(current_choice_.source_word) : Trim(q.answer);
		if (!wrong_word.empty()) {
			wrong_words_this_round_.push_back(wrong_word);
		}
		wrong_word_ids_this_round_.push_back(attempt.word_id);
	}
	const word_practice::BatchProgressSummary summary = batch_progress_tracker_.BuildSummary();
	WP_PERSIST_LOGW(kTag,
		"attempt result word_id=%d qtype=%d correct=%d kind=%d reason=%s skill=%s stage=%d->%d strength=%d->%d recall=%d->%d output=%d->%d mastered=%d response_ms=%d batch=%d/%d new=%d/%d review=%d/%d weak=%d/%d complete=%d coverage=(%d,%d,%d)",
		attempt.word_id,
		attempt.question_type,
		attempt.correct ? 1 : 0,
		static_cast<int>(kind),
		word_practice::ToString(attempt.reason_type),
		word_practice::ToString(attempt.target_skill),
		before.stage,
		profile->stage,
		before.strength,
		profile->strength,
		before.recall_score,
		profile->recall_score,
		before.output_score,
		profile->output_score,
		static_cast<int>(profile->mastered),
		attempt.response_time_ms,
		summary.completed_items,
		summary.total_items,
		summary.new_completed,
		summary.new_total,
		summary.review_completed,
		summary.review_total,
		summary.weak_completed,
		summary.weak_total,
		summary.batch_completed ? 1 : 0,
		summary.skill_coverage.recognition_done ? 1 : 0,
		summary.skill_coverage.recall_done ? 1 : 0,
		summary.skill_coverage.output_attempted ? 1 : 0);
	UpdateSessionState();
	return true;
}

void WordPracticeApp::SyncScoreLabels() {
	if (label_correct_count_) {
		label_correct_count_->SetText(std::to_string(cycle_correct_count_));
	}
	if (label_wrong_count_) {
		label_wrong_count_->SetText(std::to_string(cycle_wrong_count_));
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
	const QuestionData *question = current_question_slot_.has_value ? &current_question_slot_.current : nullptr;
	if (session_module_.IsFinished() || !question) {
		return;
	}
	const auto &q = *question;

	const std::string picked = ButtonToken(button);
	if (picked.empty()) {
		return;
	}

	std::string answer_text = Trim(current_choice_.expected);
	const std::string expected_token = quiz_module_.NormalizeAnswerToken(current_choice_.expected);
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

	const bool correct = quiz_module_.NormalizeAnswerToken(picked) == quiz_module_.NormalizeAnswerToken(current_choice_.expected);
	RecordCycleAnswer(correct);
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
	const QuestionData *question = current_question_slot_.has_value ? &current_question_slot_.current : nullptr;
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

	const std::string picked_token = quiz_module_.NormalizeAnswerToken(picked);
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
			RecordCycleAnswer(true);
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

	RecordCycleAnswer(false);
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
	const QuestionData *question = current_question_slot_.has_value ? &current_question_slot_.current : nullptr;
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
	current_speak_asr_failure_count_ = practice_flow_controller_.MaxSpeakRetryCount();
	RecordCycleSkip();
	question_scheduler_.RecordSkip(current_scheduled_question_);
	if (image_bad_) {
		image_bad_->SetVisible(false);
	}
	if (image_good_) {
		image_good_->SetVisible(false);
	}
	if (label_alert_) {
		label_alert_->SetText("本题已跳过");
	}
	UpdateAsrResultPresentation(current_question_type_, "");
	if (bottom_bar_) {
		bottom_bar_->SetText("按方向键或ABCD进入下一题");
	}
	UpdateSessionState();
	SyncScoreLabels();
}

void WordPracticeApp::OnChatMessage(const char* role, const char* content) {
	if (!ctx_ || !ui_ready_ || !label_asr_result_ || !role || !content) {
		return;
	}
	if (current_question_type_ < 7 || current_question_type_ > 10) {
		return;
	}
	const QuestionData *question = current_question_slot_.has_value ? &current_question_slot_.current : nullptr;
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
		correct = coverage >= word_practice::config::kSpeakSentenceCoverageThreshold;
		ESP_LOGI(kTag, "type9-10 asr coverage=%.3f", static_cast<double>(coverage));
	}

	UpdateAsrResultPresentation(current_question_type_, display_asr);
	if (correct) {
		current_speak_asr_failure_count_ = 0;
		RecordCycleAnswer(true);
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
		++current_speak_asr_failure_count_;
		if (image_bad_) {
			image_bad_->SetText("word_practice_bad.bin");
			image_bad_->SetVisible(true);
		}
		if (image_good_) {
			image_good_->SetVisible(false);
		}
		if (practice_flow_controller_.ShouldAutoFailSpeakQuestion(current_speak_asr_failure_count_)) {
			UpdateAsrResultPresentation(current_question_type_, answer_text);
			if (label_alert_) {
				label_alert_->SetText("已连续识别失败3次，正确读音如上，本题记错");
			}
			if (bottom_bar_) {
				bottom_bar_->SetText("按方向键或ABCD进入下一题");
			}
			RecordCycleAnswer(false);
			SaveAnswerStats(q, false);
		} else {
			UpdateAsrResultPresentation(current_question_type_, display_asr);
			if (label_alert_) {
				label_alert_->SetText("回答错误");
			}
			if (bottom_bar_) {
				bottom_bar_->SetText(
					std::string("按住Start录音，松开识别；剩余") +
					std::to_string(practice_flow_controller_.RemainingSpeakRetries(current_speak_asr_failure_count_)) +
					"次，D键跳过");
			}
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
	const QuestionData *question = current_question_slot_.has_value ? &current_question_slot_.current : nullptr;
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
	RecordCycleAnswer(correct);

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

word_practice::SessionEvaluation WordPracticeApp::EvaluateSession() const {
	const word_practice::BatchProgressSummary batch_summary = batch_progress_tracker_.BuildSummary();
	std::vector<word_practice::WordMasteryProfile> profiles = mastery_profiles_;
	if (!mastery_profile_cache_.empty()) {
		for (auto &profile : profiles) {
			const auto it = mastery_profile_cache_.find(profile.word_id);
			if (it != mastery_profile_cache_.end()) {
				profile = it->second;
			}
		}
	}
	return word_practice::SessionEvaluator::Evaluate(
		session_module_,
		learning_batch_,
		batch_summary,
		profiles,
		session_scheduler_exhausted_);
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

void WordPracticeApp::SaveAnswerStats(const QuestionData &q, bool correct) {
	InvalidateQuestionPoolCache();
	const std::string textbook_name = current_choice_.textbook_name.empty() ? (q.stage.empty() ? "default" : q.stage) : current_choice_.textbook_name;
	const word_practice::BatchProgressSummary before_summary = batch_progress_tracker_.BuildSummary();
	const std::string user_db = result_module_.ProgressDao().DiscoverUserDbPath();
	if (user_db.empty()) {
		WP_PERSIST_LOGW(kTag, "answer stats skipped: user db path missing word_id=%d qtype=%d", current_scheduled_question_.word_id, q.type);
		return;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || db == nullptr) {
		WP_PERSIST_LOGW(kTag, "answer stats open db failed path=%s msg=%s", user_db.c_str(), db ? sqlite3_errmsg(db) : "null");
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return;
	}
	if (!eteacher::database_manager::ConfigureWriteConnection(db, kTag) ||
		!mastery_dao_.EnsureTables(db) ||
		!result_module_.ProgressDao().EnsureStatsTables(db) ||
		!eteacher::database_manager::BeginTransaction(db, kTag)) {
		WP_PERSIST_LOGW(kTag, "answer stats transaction setup failed path=%s word_id=%d qtype=%d", user_db.c_str(), current_scheduled_question_.word_id, q.type);
		sqlite3_close(db);
		return;
	}

	const bool attempt_ok = RecordCurrentAttempt(db, q, correct);
	const word_practice::BatchProgressSummary batch_summary = batch_progress_tracker_.BuildSummary();
	const int updated_daily_completed_words = std::max(0, user_json_.today_mission.completed_words) + (correct ? 1 : 0);
	const int mission_target_words = std::max(1, user_json_.today_mission.today_mission_count);
	const bool daily_progress_ok = result_module_.ProgressDao().UpdateDailyProgress(
		db,
		textbook_name,
		updated_daily_completed_words,
		mission_target_words);
	const bool answer_stats_ok = result_module_.ProgressDao().SaveAnswerStats(
		db,
		session_module_,
		current_scheduled_question_.word_id,
		q,
		textbook_name,
		correct,
		session_module_.IsFinished(),
		EvaluateSession().success);
	bool round_completion_ok = true;
	if (session_module_.IsFinished() && !round_completion_recorded_) {
		round_completion_ok = result_module_.ProgressDao().RecordRoundCompletion(db, current_textbook_name_, EvaluateSession().success);
	}
	const bool persist_ok = attempt_ok && daily_progress_ok && answer_stats_ok && round_completion_ok;
	if (persist_ok && eteacher::database_manager::CommitTransaction(db, kTag)) {
		if (session_module_.IsFinished() && !round_completion_recorded_) {
			round_completion_recorded_ = true;
			++completed_rounds_for_textbook_;
		}
	} else {
		WP_PERSIST_LOGW(
			kTag,
			"answer stats transaction failed word_id=%d qtype=%d attempt=%d daily=%d answer=%d round=%d",
			current_scheduled_question_.word_id,
			q.type,
			attempt_ok ? 1 : 0,
			daily_progress_ok ? 1 : 0,
			answer_stats_ok ? 1 : 0,
			round_completion_ok ? 1 : 0);
		eteacher::database_manager::RollbackTransaction(db, kTag);
	}
	sqlite3_close(db);
	SyncUserProgressState();
	WP_PERSIST_LOGW(kTag,
		"answer stats word_id=%d qtype=%d correct=%d persist=%d score=%d correct_count=%d wrong_count=%d skip_count=%d answered=%d awaiting=%d finished=%d batch=%d/%d->%d/%d today=%d/%d progress=%d%% easy_confirm=%d",
		current_scheduled_question_.word_id,
		q.type,
		correct ? 1 : 0,
		persist_ok ? 1 : 0,
		session_module_.Score(),
		session_module_.CorrectCount(),
		session_module_.WrongCount(),
		session_module_.SkipCount(),
		session_module_.TotalAnswered(),
		session_module_.AwaitingNextQuestion() ? 1 : 0,
		session_module_.IsFinished() ? 1 : 0,
		before_summary.completed_items,
		before_summary.total_items,
		batch_summary.completed_items,
		batch_summary.total_items,
		user_json_.today_mission.completed_words,
		mission_target_words,
		user_json_.today_progress_percent,
		batch_summary.skill_coverage_ok ? 1 : 0);
	WP_PERSIST_LOGW(kTag,
		"answer persistence word_id=%d textbook=%s daily_progress_ok=%d feedback=%s",
		current_scheduled_question_.word_id,
		textbook_name.c_str(),
		daily_progress_ok ? 1 : 0,
		last_attempt_feedback_text_.empty() ? "(none)" : last_attempt_feedback_text_.c_str());
	(void)SaveUserJson();
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
