#include "eteacher/apps/word_practice/word_practice.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <memory>
#include <random>
#include <string_view>

#include <SD.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/apps/word_practice/word_practice_ui.h"
#include "eteacher/database_manager/database_debug.h"
#include "eteacher/app_service/app_service.h"
#include "eteacher/database_manager/sqlite_db_api.h"

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
constexpr const char *kQuestionAudioDir = "/resource/audio/wrods/";

bool IsClickLike(const ButtonEvent &event) {
	return event.action == ButtonAction::Click;
}

bool IsNextQuestionTriggerButton(AppButton button) {
	return button == AppButton::Up || button == AppButton::Down || button == AppButton::Left ||
		   button == AppButton::Right || button == AppButton::A || button == AppButton::B ||
		   button == AppButton::C || button == AppButton::D;
}

std::string BuildQuestionAudioPath(const std::string &audio_filename) {
	std::string name = audio_filename;
	while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())) != 0) {
		name.erase(name.begin());
	}
	while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())) != 0) {
		name.pop_back();
	}
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
	if (name[0] == '/') {
		return name;
	}
	return std::string(kQuestionAudioDir) + name;
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

std::string JsonString(cJSON *obj, const char *key) {
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

int ParseTypeToken(const std::string &value) {
	std::string text = Trim(value);
	if (text.empty()) {
		return 1;
	}

	int type = 0;
	std::sscanf(text.c_str(), "%d", &type);
	if (type < 1 || type > 10) {
		return 1;
	}
	return type;
}

int64_t NowSec() {
	return static_cast<int64_t>(esp_timer_get_time() / 1000000ULL);
}

std::string TodayDate() {
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

bool StepDone(sqlite3_stmt *stmt) {
	const int rc = sqlite3_step(stmt);
	return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

constexpr int kPromptDashWidthShort = 60;
constexpr int kPromptDashWidthLong = 300;
constexpr int kPromptDashGapPx = 2;
constexpr int16_t kQuestionBaseX = 55;
constexpr int16_t kQuestionBaseY = 82;
constexpr int16_t kSpeakerBaseX = 20;
constexpr int16_t kSpeakerBaseY = 82;

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

int FontLineHeight(const char *font_name) {
	const auto *font = eteacher::font_manager::GetBuiltinFont(font_name ? font_name : "wenquanyi_11pt");
	if (!font || !font->Ready()) {
		return 16;
	}
	return static_cast<int>(font->Header().ascent + font->Header().descent);
}

}  // namespace

MenuMeta WordPracticeApp::GetMenuMeta() const {
	return MenuMeta{"word_practice", "单词练习", "10题过关 Start进入 Select退出"};
}

void WordPracticeApp::OnEnter(AppContext &ctx) {
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
	label_a_ = nullptr;
	label_b_ = nullptr;
	label_c_ = nullptr;
	label_d_ = nullptr;
	label_question_line2_ = nullptr;
	question_dash_line1_ = nullptr;
	question_dash_line2_ = nullptr;
	label_up_ = nullptr;
	label_left_ = nullptr;
	label_down_ = nullptr;
	label_right_ = nullptr;

	current_index_ = 0;
	current_question_type_ = 1;
	current_choice_ = {};
	correct_count_ = 0;
	wrong_count_ = 0;
	total_answered_ = 0;
	score_ = 0;
	awaiting_next_question_ = false;
	type4_left_words_.clear();
	type4_right_words_.clear();
	type4_expected_right_index_.clear();
	type4_selected_right_by_left_.clear();
	type4_selected_left_index_ = 0;
	type56_words_.clear();
	type56_selected_index_ = 0;
	type56_input_answer_.clear();
	recent_types_.clear();
	textbook_name_ = "default";
	current_audio_path_.clear();
	question_prompt_profile_ = {};
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
			bottom_bar_->SetText("题库加载中...");
		}
		if (label_alert_) {
			label_alert_->SetText("");
		}
		Render(ctx);
	}

	LoadQuestionPool();
	if (!PickNextQuestion()) {
		ctx.board.GetDisplay()->SetChatMessage("system", "WordPractice: 题库为空");
		return;
	}

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
	const bool is_speak_type = (current_question_type_ >= 7 && current_question_type_ <= 10);
	if (!is_speak_type && !IsClickLike(event)) {
		return;
	}

	if (event.id == AppButton::Start && event.action == ButtonAction::Click && total_answered_ >= pass_target_questions_) {
		correct_count_ = 0;
		wrong_count_ = 0;
		total_answered_ = 0;
		score_ = 0;
		awaiting_next_question_ = false;
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
		!(current_question_type_ == 5 || current_question_type_ == 6 || is_speak_type)) {
		if (!PlayAudioFromSd(current_audio_path_)) {
			if (label_alert_) {
				label_alert_->SetText("音频播放失败");
			}
		}
		Render(ctx);
		return;
	}

	if (awaiting_next_question_) {
		if (IsNextQuestionTriggerButton(event.id)) {
			awaiting_next_question_ = false;
			if (total_answered_ < pass_target_questions_) {
				(void)PickNextQuestion();
			}
		}
		if (total_answered_ >= pass_target_questions_) {
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

	if (total_answered_ >= pass_target_questions_) {
		ShowSessionSummary();
	}
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
		msg += "Q:" + std::to_string(total_answered_ + 1) + "/" + std::to_string(pass_target_questions_);
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
		app_ui::TextAreaProfile profile;
		profile.decoration_mode = app_ui::TextAreaProfile::DecorationMode::UnderlineDashed;
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
	image_a_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageA));
	image_b_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageB));
	image_c_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageC));
	image_d_ = dynamic_cast<app_ui::ImageWidget *>(root->FindById(kWidgetImageD));

	label_question_line2_ = nullptr;
	question_dash_line1_ = nullptr;
	question_dash_line2_ = nullptr;
	if (root_) {
		auto *line2 = dynamic_cast<app_ui::LabelWidget *>(root_->AddChild(std::make_unique<app_ui::LabelWidget>()));
		if (line2) {
			line2->SetLayoutMode(app_ui::Widget::LayoutMode::Fixed);
			line2->SetFontName("wenquanyi_11pt");
			line2->SetVisible(false);
		}
		label_question_line2_ = line2;

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
	if (image_good_) image_good_->SetText("word_practice_good.bin");
	if (image_bad_) image_bad_->SetText("word_practice_bad.bin");
	if (image_good_) image_good_->SetVisible(false);
	if (image_bad_) image_bad_->SetVisible(false);

	if (label_question_type_) label_question_type_->SetFontName("wenquanyi_11pt");
	if (label_correct_count_) label_correct_count_->SetFontName("wenquanyi_11pt");
	if (label_wrong_count_) label_wrong_count_->SetFontName("wenquanyi_11pt");
	if (label_alert_) label_alert_->SetFontName("wenquanyi_11pt");
	if (label_question_) label_question_->SetFontName("wenquanyi_11pt");
	if (label_asr_result_) label_asr_result_->SetFontName("wenquanyi_11pt");
	if (label_press_aread_) label_press_aread_->SetFontName("wenquanyi_11pt");
	if (label_press_d_skip_) label_press_d_skip_->SetFontName("wenquanyi_11pt");
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
	question_pool_.clear();

	if (!eteacher::database_manager::EnsureSqliteRuntimeReady(kTag)) {
		ESP_LOGE(kTag, "sqlite runtime init failed");
		return;
	}
	(void)eteacher::database_manager::EnsureSqliteSdMounted(kTag);

	const std::string db_path = DiscoverQuestionDbPath();
	if (db_path.empty()) {
		ESP_LOGE(kTag, "question db path not found");
		return;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK || !db) {
		ESP_LOGE(kTag, "open question db failed: %s", db ? sqlite3_errmsg(db) : "null");
		if (db) {
			sqlite3_close(db);
		}
		return;
	}

	const char *sql =
		"SELECT id, question_type, stage, difficulty, content_json, answer "
		"FROM question_bank;";

	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		ESP_LOGE(kTag, "prepare question query failed: %s", sqlite3_errmsg(db));
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
		return;
	}

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		QuestionData row;
		row.id = sqlite3_column_int(stmt, 0);
		row.type = sqlite3_column_int(stmt, 1);
		if (row.type < 1 || row.type > 10) {
			const char *type_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
			row.type = ParseTypeToken(type_text ? type_text : "");
		}
		row.stage = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))
						? reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))
						: "";
		row.difficulty = sqlite3_column_int(stmt, 3);
		row.content_json = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4))
							   ? reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4))
							   : "";
		row.answer = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5))
						 ? reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5))
						 : "";
		if (row.difficulty <= 0) {
			row.difficulty = 1;
		}
		question_pool_.push_back(std::move(row));
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);

	ESP_LOGI(kTag, "question pool loaded: %d", static_cast<int>(question_pool_.size()));
}

bool WordPracticeApp::PickNextQuestion() {
	if (question_pool_.empty()) {
		return false;
	}

	if (total_answered_ >= pass_target_questions_) {
		return true;
	}

	const std::string user_db = DiscoverUserDbPath();
	sqlite3 *udb = nullptr;
	int current_level = 1;
	if (!user_db.empty() && sqlite3_open_v2(user_db.c_str(), &udb, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK &&
		udb) {
		(void)EnsureStatsTables(udb);
		current_level = QueryCurrentLevel(udb);
	}
	if (current_level < 1) {
		current_level = 1;
	}

	static const std::array<const char *, 5> kStages = {"primary", "middle", "high", "cet4", "cet6"};
	const size_t stage_idx = static_cast<size_t>(std::min(5, std::max(1, current_level)) - 1);
	const std::string stage = kStages[stage_idx];
	const int target_difficulty = std::min(6, 1 + total_answered_ / 2);
	const int recent_type = recent_types_.empty() ? -1 : recent_types_.back();

	std::mt19937 rng(static_cast<uint32_t>(esp_random()));
	std::uniform_real_distribution<float> jitter(-0.15f, 0.15f);
	const int expected_type = 1 + (total_answered_ % 7);
	bool has_expected_type = false;
	for (const auto &q : question_pool_) {
		if (q.type == expected_type) {
			has_expected_type = true;
			break;
		}
	}

	float best_score = -10000.0f;
	size_t best_index = 0;

	for (size_t i = 0; i < question_pool_.size(); ++i) {
		const auto &q = question_pool_[i];
		if (has_expected_type && q.type != expected_type) {
			continue;
		}

		float level_bonus = (q.stage == stage) ? 2.2f : ((q.stage.empty()) ? 1.2f : -1.0f);
		float difficulty_bonus = 1.2f - 0.35f * static_cast<float>(std::abs(q.difficulty - target_difficulty));

		auto learned = QueryLearned(udb, q.id, q.stage.empty() ? "default" : q.stage);
		const int total = learned.correct + learned.wrong;
		const float accuracy = total > 0 ? static_cast<float>(learned.correct) / static_cast<float>(total) : 0.0f;
		const float weak_bonus = total > 0 ? (1.0f - accuracy) * 2.0f : 0.8f;

		float type_cycle = 0.0f;
		if (recent_type == q.type) {
			type_cycle -= 1.2f;
		} else {
			type_cycle += 0.9f;
		}

		const int64_t age_sec = std::max<int64_t>(0, NowSec() - learned.last_seen_at);
		const float exposure_decay = (learned.last_seen_at <= 0) ? 0.7f : std::min(1.8f, static_cast<float>(age_sec) / 3600.0f * 0.2f);

		const float score = level_bonus + weak_bonus + difficulty_bonus + type_cycle + exposure_decay + jitter(rng);
		if (score > best_score) {
			best_score = score;
			best_index = i;
		}
	}

	if (udb) {
		sqlite3_close(udb);
	}

	current_index_ = best_index;
	current_question_type_ = question_pool_[current_index_].type;
	recent_types_.push_back(current_question_type_);
	if (recent_types_.size() > 6) {
		recent_types_.erase(recent_types_.begin());
	}

	PresentCurrentQuestion();
	return true;
}

void WordPracticeApp::PresentCurrentQuestion() {
	if (current_index_ >= question_pool_.size()) {
		return;
	}
	const auto &q = question_pool_[current_index_];
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
	textbook_name_ = current_choice_.textbook_name.empty() ? (q.stage.empty() ? "default" : q.stage) : current_choice_.textbook_name;
	if (!current_audio_path_.empty()) {
		ScheduleQuestionAudioAutoPlay();
	} else {
		CancelQuestionAudioAutoPlay();
	}

	if (label_question_type_) {
		label_question_type_->SetText(TypeTitle(q.type));
	}
	if (label_correct_count_) {
		label_correct_count_->SetText(std::to_string(correct_count_));
	}
	if (label_wrong_count_) {
		label_wrong_count_->SetText(std::to_string(wrong_count_));
	}
	if (label_alert_) {
		label_alert_->SetText("");
	}
	if (bottom_bar_) {
		bottom_bar_->SetText(TypeInstruction(q.type));
	}
	UpdateQuestionPromptPresentation(q.type, current_choice_.prompt);
	if (label_asr_result_) {
		label_asr_result_->SetText("");
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

	type56_words_.clear();
	type56_input_answer_.clear();
	type56_selected_index_ = 0;
	type4_left_words_.clear();
	type4_right_words_.clear();
	type4_expected_right_index_.clear();
	type4_selected_right_by_left_.clear();
	type4_selected_left_index_ = 0;
	awaiting_next_question_ = false;
	if (is_type56) {
		type56_words_ = current_choice_.hints;
		if (type56_words_.empty()) {
			type56_words_ = current_choice_.options;
		}
	}
	RefreshType56Widgets();

	if (q.type == 4) {
		type4_left_words_ = current_choice_.pair_left;
		type4_right_words_ = current_choice_.pair_right;
		if (type4_left_words_.size() < 4) {
			type4_left_words_.resize(4);
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
		const bool is_translation_type = (q.type == 2 || q.type == 3);
		if (q.type == 1) {
			auto set_option_image = [](app_ui::ImageWidget *widget, const std::vector<std::string> &images, size_t index) {
				if (!widget) {
					return;
				}
				const std::string image_name = (index < images.size()) ? Trim(images[index]) : "";
				if (!image_name.empty()) {
					widget->SetText("words/" + image_name);
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
			if (q.type == 1) {
				label_a_->SetText("A");
			} else {
				label_a_->SetText(is_translation_type ? FormatOptionWithKey(option_key, option) : (option.empty() ? "A" : option));
			}
			label_a_->SetFocused(false);
		}
		if (label_b_) {
			const std::string option = current_choice_.options.size() > 1 ? current_choice_.options[1] : "";
			const std::string option_key = current_choice_.option_keys.size() > 1 ? current_choice_.option_keys[1] : "B";
			if (q.type == 1) {
				label_b_->SetText("B");
			} else {
				label_b_->SetText(is_translation_type ? FormatOptionWithKey(option_key, option) : (option.empty() ? "B" : option));
			}
			label_b_->SetFocused(false);
		}
		if (label_c_) {
			const std::string option = current_choice_.options.size() > 2 ? current_choice_.options[2] : "";
			const std::string option_key = current_choice_.option_keys.size() > 2 ? current_choice_.option_keys[2] : "C";
			if (q.type == 1) {
				label_c_->SetText("C");
			} else {
				label_c_->SetText(is_translation_type ? FormatOptionWithKey(option_key, option) : (option.empty() ? "C" : option));
			}
			label_c_->SetFocused(false);
		}
		if (label_d_) {
			const std::string option = current_choice_.options.size() > 3 ? current_choice_.options[3] : "";
			const std::string option_key = current_choice_.option_keys.size() > 3 ? current_choice_.option_keys[3] : "D";
			if (q.type == 1) {
				label_d_->SetText("D");
			} else {
				label_d_->SetText(is_translation_type ? FormatOptionWithKey(option_key, option) : (option.empty() ? "D" : option));
			}
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
}

void WordPracticeApp::ShowSessionSummary() {
	if (!label_question_) {
		return;
	}
	const bool pass = IsSessionPassed();
	std::string summary = pass ? "恭喜过关!" : "未过关，继续练习";
	summary += " 分数:" + std::to_string(score_);
	summary += " 正确:" + std::to_string(correct_count_);
	summary += " 错误:" + std::to_string(wrong_count_);
	UpdateQuestionPromptPresentation(current_question_type_, summary);
	if (label_alert_) {
		label_alert_->SetText("");
	}
	if (bottom_bar_) {
		bottom_bar_->SetText(pass ? "Start继续下一轮" : "Start重开本轮");
	}
}

void WordPracticeApp::UpdateQuestionPromptPresentation(int question_type, const std::string &prompt) {
	question_prompt_profile_ = BuildQuestionPromptProfile(question_type);
	const bool show = question_prompt_profile_.visible;
	const std::string prompt_text = Trim(prompt);
	if (image_public_speaker_) {
		image_public_speaker_->SetRectInParent({kSpeakerBaseX, kSpeakerBaseY, 20, 20});
		image_public_speaker_->SetVisible(show);
	}
	if (label_question_) {
		label_question_->SetVisible(show);
	}
	if (label_question_line2_) {
		label_question_line2_->SetVisible(false);
		label_question_line2_->SetText("");
	}
	if (question_dash_line1_) {
		question_dash_line1_->SetVisible(false);
	}
	if (question_dash_line2_) {
		question_dash_line2_->SetVisible(false);
	}

	if (!show || !label_question_) {
		return;
	}

	int dash_width = question_prompt_profile_.dash_width;
	if (question_type == 5 || question_type == 6 || question_type == 9 || question_type == 10) {
		const int label_width = static_cast<int>(label_question_->RectInParent().w);
		if (label_width > 0) {
			dash_width = label_width;
		}
	}
	if (dash_width <= 0) {
		label_question_->SetText(prompt_text);
		return;
	}

	const DashLineProfile dash_profile = {
		static_cast<int16_t>(question_prompt_profile_.dash_black_len),
		static_cast<int16_t>(question_prompt_profile_.dash_white_len),
	};
	if (auto *dash1 = dynamic_cast<DashedLineWidget *>(question_dash_line1_)) {
		dash1->SetProfile(dash_profile);
	}
	if (auto *dash2 = dynamic_cast<DashedLineWidget *>(question_dash_line2_)) {
		dash2->SetProfile(dash_profile);
	}

	const char *font_name = label_question_->FontName();
	const int line_height = std::max(1, FontLineHeight(font_name));
	const app_ui::Rect base_rect = {kQuestionBaseX, kQuestionBaseY, static_cast<int16_t>(dash_width), static_cast<int16_t>(line_height)};
	const int full_width = static_cast<int>(epd_ ? epd_->MeasureUtf8Width(prompt_text, font_name) : 0);

	size_t used_bytes = 0;
	std::string line1;
	std::string line2;
	if (full_width <= dash_width) {
		line1 = prompt_text;
		used_bytes = prompt_text.size();
	} else {
		line1 = FitUtf8TextToWidth(prompt_text, dash_width, font_name, epd_, &used_bytes);
		if (used_bytes < prompt_text.size()) {
			line2 = FitUtf8TextToWidth(prompt_text.substr(used_bytes), dash_width, font_name, epd_, nullptr);
		}
	}

	const int draw_line1_width = dash_width;
	const int draw_line2_width = dash_width;

	label_question_->SetRectInParent({base_rect.x, base_rect.y, static_cast<int16_t>(draw_line1_width), static_cast<int16_t>(line_height)});
	label_question_->SetText(line1);

	if (auto *dash1 = dynamic_cast<DashedLineWidget *>(question_dash_line1_)) {
		dash1->SetRectInParent({base_rect.x, static_cast<int16_t>(base_rect.y + line_height + question_prompt_profile_.dash_gap_px),
			static_cast<int16_t>(draw_line1_width), 1});
		dash1->SetVisible(true);
	}

	if (!line2.empty()) {
		const int16_t line2_y = static_cast<int16_t>(base_rect.y + line_height + question_prompt_profile_.dash_gap_px + 1 + question_prompt_profile_.dash_gap_px);
		if (label_question_line2_) {
			label_question_line2_->SetRectInParent({base_rect.x, line2_y, static_cast<int16_t>(draw_line2_width), static_cast<int16_t>(line_height)});
			label_question_line2_->SetText(line2);
			label_question_line2_->SetVisible(true);
		}

		if (auto *dash2 = dynamic_cast<DashedLineWidget *>(question_dash_line2_)) {
			dash2->SetRectInParent({base_rect.x, static_cast<int16_t>(line2_y + line_height + question_prompt_profile_.dash_gap_px),
				static_cast<int16_t>(draw_line2_width), 1});
			dash2->SetVisible(true);
		}
	}
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
		case 7:
		case 8:
			profile.visible = true;
			profile.dash_width = kPromptDashWidthShort;
			break;
		case 5:
		case 6:
		case 9:
		case 10:
			profile.visible = true;
			profile.dash_width = kPromptDashWidthLong;
			break;
		default:
			profile.visible = false;
			profile.dash_width = 0;
			break;
	}

	return profile;
}

void WordPracticeApp::HandleAnswer(AppButton button) {
	if (total_answered_ >= pass_target_questions_ || current_index_ >= question_pool_.size()) {
		return;
	}

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
		answer_text = Trim(question_pool_[current_index_].answer);
	}

	const bool correct = NormalizeAnswerToken(picked) == NormalizeAnswerToken(current_choice_.expected);
	if (correct) {
		++correct_count_;
		score_ += 10;
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
		++wrong_count_;
		score_ = std::max(0, score_ - 2);
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
	++total_answered_;

	SaveAnswerStats(question_pool_[current_index_], correct);
	if (total_answered_ < pass_target_questions_) {
		awaiting_next_question_ = true;
	}
}

void WordPracticeApp::RefreshType4Widgets() {
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
		const std::string option_key(1, static_cast<char>('A' + i));
		set_text_and_focus(right_labels[static_cast<size_t>(i)], FormatOptionWithKey(option_key, right_text), right_matched[static_cast<size_t>(i)]);
	}
}

void WordPracticeApp::HandleType4Action(AppButton button) {
	if (total_answered_ >= pass_target_questions_ || current_index_ >= question_pool_.size()) {
		return;
	}

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
			++correct_count_;
			score_ += 10;
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

			++total_answered_;
			SaveAnswerStats(question_pool_[current_index_], true);
			if (total_answered_ < pass_target_questions_) {
				awaiting_next_question_ = true;
			}
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

	++wrong_count_;
	score_ = std::max(0, score_ - 2);
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

	++total_answered_;
	SaveAnswerStats(question_pool_[current_index_], false);
	if (total_answered_ < pass_target_questions_) {
		awaiting_next_question_ = true;
	}
}

void WordPracticeApp::HandleSpeakAction(const ButtonEvent &event) {
	if (total_answered_ >= pass_target_questions_ || current_index_ >= question_pool_.size()) {
		return;
	}

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

	if (button != AppButton::A && button != AppButton::D) {
		return;
	}

	std::string answer_text = Trim(current_choice_.expected);
	if (answer_text.empty()) {
		answer_text = Trim(question_pool_[current_index_].answer);
	}

	const bool correct = (button == AppButton::A);
	if (correct) {
		++correct_count_;
		score_ += 10;
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
		++wrong_count_;
		score_ = std::max(0, score_ - 2);
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

	++total_answered_;
	SaveAnswerStats(question_pool_[current_index_], correct);
	if (total_answered_ < pass_target_questions_) {
		awaiting_next_question_ = true;
	}
}

void WordPracticeApp::OnChatMessage(const char* role, const char* content) {
	if (!ctx_ || !ui_ready_ || !label_asr_result_ || !role || !content) {
		return;
	}
	if (current_question_type_ < 7 || current_question_type_ > 10) {
		return;
	}
	if (::strcmp(role, "user") != 0) {
		return;
	}
	if (content[0] == '\0') {
		return;
	}
	ESP_LOGI(kTag, "type7-10 asr text: %s", content);
	label_asr_result_->SetText(content);
	Render(*ctx_);
}

void WordPracticeApp::RefreshType56Widgets() {
	const bool show = (current_question_type_ == 5 || current_question_type_ == 6);
	if (textarea_input_answer_) {
		textarea_input_answer_->SetVisible(show);
		textarea_input_answer_->SetText(show ? type56_input_answer_ : "");
	}
	if (dialog_select_board_) {
		dialog_select_board_->SetVisible(show);
		if (show) {
			if (type56_selected_index_ < 0) {
				type56_selected_index_ = 0;
			}
			if (!type56_words_.empty() && type56_selected_index_ >= static_cast<int>(type56_words_.size())) {
				type56_selected_index_ = static_cast<int>(type56_words_.size()) - 1;
			}
			dialog_select_board_->SetItems(type56_words_);
			dialog_select_board_->SetSelectedIndex(type56_selected_index_);
		} else {
			dialog_select_board_->SetItems({});
			dialog_select_board_->SetSelectedIndex(0);
			dialog_select_board_->SetText("");
		}
	}
}

void WordPracticeApp::HandleType56Action(AppButton button) {
	if (total_answered_ >= pass_target_questions_ || current_index_ >= question_pool_.size()) {
		return;
	}

	if (type56_words_.empty()) {
		type56_words_ = current_choice_.hints;
		if (type56_words_.empty()) {
			type56_words_ = current_choice_.options;
		}
		if (type56_selected_index_ >= static_cast<int>(type56_words_.size())) {
			type56_selected_index_ = 0;
		}
	}

	const int count = static_cast<int>(type56_words_.size());
	const int cols = std::max(1, (count + 1) / 2);
	if (button == AppButton::Up && count > 0) {
		if (type56_selected_index_ >= cols) {
			type56_selected_index_ -= cols;
		}
		RefreshType56Widgets();
		return;
	}
	if (button == AppButton::Down && count > 0) {
		if (type56_selected_index_ + cols < count) {
			type56_selected_index_ += cols;
		}
		RefreshType56Widgets();
		return;
	}
	if (button == AppButton::Left && count > 0) {
		if (type56_selected_index_ > 0) {
			--type56_selected_index_;
		}
		RefreshType56Widgets();
		return;
	}
	if (button == AppButton::Right && count > 0) {
		if (type56_selected_index_ + 1 < count) {
			++type56_selected_index_;
		}
		RefreshType56Widgets();
		return;
	}

	if (button == AppButton::C && count > 0 && type56_selected_index_ >= 0 && type56_selected_index_ < count) {
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

	if (button != AppButton::Start) {
		return;
	}

	const std::string expected_display = Trim(current_choice_.expected.empty() ? question_pool_[current_index_].answer : current_choice_.expected);
	const std::string input_text = NormalizeType56ForCompare(type56_input_answer_);
	const std::string answer_text = NormalizeType56ForCompare(expected_display);
	const bool correct = (!input_text.empty() && input_text == answer_text);

	if (correct) {
		++correct_count_;
		score_ += 10;
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
		++wrong_count_;
		score_ = std::max(0, score_ - 2);
		if (image_bad_) {
			image_bad_->SetText("word_practice_bad.bin");
			image_bad_->SetVisible(true);
		}
		if (image_good_) {
			image_good_->SetVisible(false);
		}
		if (label_alert_) {
			label_alert_->SetText("答错了，正确答案是" + expected_display);
		}
		if (bottom_bar_) {
			bottom_bar_->SetText("按方向键或ABCD进入下一题");
		}
	}

	++total_answered_;
	SaveAnswerStats(question_pool_[current_index_], correct);
	if (total_answered_ < pass_target_questions_) {
		awaiting_next_question_ = true;
	}
}

bool WordPracticeApp::IsSessionPassed() const {
	if (total_answered_ <= 0) {
		return false;
	}
	const float acc = static_cast<float>(correct_count_) / static_cast<float>(total_answered_);
	return total_answered_ >= pass_target_questions_ && acc >= 0.8f && score_ >= 60;
}

std::string WordPracticeApp::SelectSceneIdByType(int question_type) const {
	if (question_type == 1) {
		return "page_3b66";
	}
	if (question_type == 2 || question_type == 3) {
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

std::string WordPracticeApp::TypeTitle(int question_type) const {
	switch (question_type) {
		case 1:
			return "题型1 图片选择";
		case 2:
		case 3:
			return "题型2/3 单词翻译";
		case 4:
			return "题型4 单词配对";
		case 5:
		case 6:
			return "题型5/6 句子翻译";
		case 7:
		case 8:
		case 9:
		case 10:
			return "题型7-10 口语词句";
		default:
			return "题型未知";
	}
}

std::string WordPracticeApp::TypeInstruction(int question_type) const {
	switch (question_type) {
		case 1:
			return "请选择正确图片(A/B/C/D)";
		case 2:
		case 3:
			return "请选择正确单词翻译(A/B/C/D)";
		case 4:
			return "请选择正确配对(A/B/C/D)";
		case 5:
		case 6:
			return "请选择正确句子翻译(A/B/C/D)";
		case 7:
		case 8:
		case 9:
		case 10:
			return "按A开始口语词句，D跳过";
		default:
			return "按键作答";
	}
}
WordPracticeApp::ChoiceState WordPracticeApp::BuildChoiceState(const QuestionData &q) const {
	ChoiceState state;
	state.prompt = "请作答";
	state.option_keys = {"A", "B", "C", "D"};
	state.options = {"A", "B", "C", "D"};
	state.option_images = {"", "", "", ""};
	state.expected = q.answer;
	state.textbook_name = q.stage.empty() ? "default" : q.stage;

	if (q.content_json.empty()) {
		return state;
	}

	cJSON *root = cJSON_Parse(q.content_json.c_str());
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
		for (size_t i = 0; i < keys.size(); ++i) {
			cJSON *entry = cJSON_GetObjectItemCaseSensitive(options_obj, keys[i]);
			if (cJSON_IsString(entry) && entry->valuestring) {
				state.options[i] = entry->valuestring;
				continue;
			}
			if (cJSON_IsObject(entry)) {
				std::string key = Trim(JsonString(entry, "key"));
				if (!key.empty()) {
					state.option_keys[i] = key;
				}

				std::string image = Trim(JsonString(entry, "image"));
				if (!image.empty()) {
					state.option_images[i] = image;
				}

				std::string text = JsonString(entry, "tex");
				if (text.empty()) {
					text = JsonString(entry, "text");
				}
				if (!text.empty()) {
					state.options[i] = text;
				}
				continue;
			}

			std::string value = JsonString(options_obj, keys[i]);
			if (!value.empty()) {
				state.options[i] = value;
			}
		}
	} else if (cJSON_IsArray(options_obj)) {
		for (int i = 0; i < cJSON_GetArraySize(options_obj) && i < 4; ++i) {
			cJSON *item = cJSON_GetArrayItem(options_obj, i);
			if (cJSON_IsString(item) && item->valuestring) {
				state.options[static_cast<size_t>(i)] = item->valuestring;
			} else if (cJSON_IsObject(item)) {
				std::string key = Trim(JsonString(item, "key"));
				size_t dst_index = static_cast<size_t>(i);
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

				std::string text = JsonString(item, "tex");
				if (text.empty()) {
					text = JsonString(item, "text");
				}
				if (!text.empty()) {
					state.options[dst_index] = text;
				}
			}
		}
	}

	cJSON *left_obj = cJSON_GetObjectItemCaseSensitive(root, "left");
	if (cJSON_IsArray(left_obj)) {
		for (int i = 0; i < cJSON_GetArraySize(left_obj) && i < 4; ++i) {
			cJSON *item = cJSON_GetArrayItem(left_obj, i);
			if (cJSON_IsString(item) && item->valuestring) {
				state.pair_left.push_back(item->valuestring);
			}
		}
	}

	cJSON *right_obj = cJSON_GetObjectItemCaseSensitive(root, "right");
	if (cJSON_IsArray(right_obj)) {
		for (int i = 0; i < cJSON_GetArraySize(right_obj) && i < 4; ++i) {
			cJSON *item = cJSON_GetArrayItem(right_obj, i);
			if (cJSON_IsString(item) && item->valuestring) {
				state.pair_right.push_back(item->valuestring);
			}
		}
	}

	cJSON *hints_obj = cJSON_GetObjectItemCaseSensitive(root, "hints");
	if (cJSON_IsArray(hints_obj)) {
		for (int i = 0; i < cJSON_GetArraySize(hints_obj); ++i) {
			cJSON *item = cJSON_GetArrayItem(hints_obj, i);
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
	if (state.hints.empty()) {
		state.hints = state.options;
	}

	cJSON_Delete(root);
	return state;
}

std::string WordPracticeApp::NormalizeAnswerToken(std::string value) const {
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

std::string WordPracticeApp::NormalizePairWord(const std::string &value) const {
	std::string text = Trim(value);
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return text;
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
	static const std::array<const char *, 3> kCandidates = {
		"/sdcard/resource/database/question.db",
		"/sdcard/resources/database/question.db",
		"/sdcard/resource/db/question.db",
	};

	for (const char *path : kCandidates) {
		sqlite3 *db = nullptr;
		const int rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, nullptr);
		if (rc != SQLITE_OK || !db) {
			if (db) {
				sqlite3_close(db);
			}
			continue;
		}
		sqlite3_stmt *stmt = nullptr;
		const char *sql = "SELECT 1 FROM question_bank LIMIT 1;";
		const int pr = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
		if (pr == SQLITE_OK && stmt) {
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			return path;
		}
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
	}
	return {};
}

std::string WordPracticeApp::DiscoverUserDbPath() const {
	return eteacher::database_manager::DiscoverUserDataDbPath(kTag, nullptr);
}

bool WordPracticeApp::EnsureStatsTables(sqlite3 *db) const {
	if (!db) {
		return false;
	}
	const char *sql_learned =
		"CREATE TABLE IF NOT EXISTS learned ("
		"user_id INTEGER NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"question_index INTEGER NOT NULL,"
		"correct_count INTEGER DEFAULT 0,"
		"wrong_count INTEGER DEFAULT 0,"
		"last_seen_at INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, textbook_name, question_index)"
		");";

	const char *sql_stats =
		"CREATE TABLE IF NOT EXISTS word_practice_stats_daily ("
		"user_id INTEGER NOT NULL,"
		"date TEXT NOT NULL,"
		"textbook_name TEXT NOT NULL,"
		"total_count INTEGER DEFAULT 0,"
		"correct_count INTEGER DEFAULT 0,"
		"wrong_count INTEGER DEFAULT 0,"
		"pass_count INTEGER DEFAULT 0,"
		"fail_count INTEGER DEFAULT 0,"
		"PRIMARY KEY (user_id, date, textbook_name)"
		");";

	char *err = nullptr;
	if (sqlite3_exec(db, sql_learned, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(kTag, "create learned failed: %s", err ? err : "unknown");
		if (err) sqlite3_free(err);
		return false;
	}
	if (sqlite3_exec(db, sql_stats, nullptr, nullptr, &err) != SQLITE_OK) {
		ESP_LOGE(kTag, "create word_practice_stats_daily failed: %s", err ? err : "unknown");
		if (err) sqlite3_free(err);
		return false;
	}
	return true;
}

int WordPracticeApp::QueryCurrentLevel(sqlite3 *db) const {
	if (!db) {
		return 1;
	}
	const char *sql = "SELECT level FROM users WHERE id=? LIMIT 1;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return 1;
	}
	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	int level = 1;
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		level = sqlite3_column_int(stmt, 0);
	}
	sqlite3_finalize(stmt);
	return (level <= 0) ? 1 : level;
}

WordPracticeApp::LearnedSnapshot WordPracticeApp::QueryLearned(sqlite3 *db,
															   int question_id,
															   const std::string &textbook) const {
	LearnedSnapshot snapshot;
	if (!db) {
		return snapshot;
	}
	const char *sql =
		"SELECT correct_count, wrong_count, last_seen_at FROM learned "
		"WHERE user_id=? AND textbook_name=? AND question_index=? LIMIT 1;";
	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		return snapshot;
	}
	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	sqlite3_bind_text(stmt, 2, textbook.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, question_id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		snapshot.correct = sqlite3_column_int(stmt, 0);
		snapshot.wrong = sqlite3_column_int(stmt, 1);
		snapshot.last_seen_at = sqlite3_column_int64(stmt, 2);
	}
	sqlite3_finalize(stmt);
	return snapshot;
}

void WordPracticeApp::SaveAnswerStats(const QuestionData &q, bool correct) {
	const std::string user_db = DiscoverUserDbPath();
	if (user_db.empty()) {
		return;
	}

	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(user_db.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK || !db) {
		if (db) {
			sqlite3_close(db);
		}
		return;
	}

	if (!EnsureStatsTables(db) || !eteacher::database_manager::ConfigureWriteConnection(db, kTag)) {
		sqlite3_close(db);
		return;
	}

	if (!eteacher::database_manager::BeginTransaction(db, kTag)) {
		sqlite3_close(db);
		return;
	}

	const char *sql_learned =
		"INSERT INTO learned(user_id, textbook_name, question_index, correct_count, wrong_count, last_seen_at) "
		"VALUES(?, ?, ?, ?, ?, ?) "
		"ON CONFLICT(user_id, textbook_name, question_index) DO UPDATE SET "
		"correct_count = correct_count + excluded.correct_count, "
		"wrong_count = wrong_count + excluded.wrong_count, "
		"last_seen_at = excluded.last_seen_at;";

	sqlite3_stmt *stmt = nullptr;
	if (sqlite3_prepare_v2(db, sql_learned, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		eteacher::database_manager::RollbackTransaction(db, kTag);
		sqlite3_close(db);
		return;
	}
	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	sqlite3_bind_text(stmt, 2, textbook_name_.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, q.id);
	sqlite3_bind_int(stmt, 4, correct ? 1 : 0);
	sqlite3_bind_int(stmt, 5, correct ? 0 : 1);
	sqlite3_bind_int64(stmt, 6, NowSec());
	if (!StepDone(stmt)) {
		sqlite3_finalize(stmt);
		eteacher::database_manager::RollbackTransaction(db, kTag);
		sqlite3_close(db);
		return;
	}
	sqlite3_finalize(stmt);

	const char *sql_daily =
		"INSERT INTO word_practice_stats_daily(user_id, date, textbook_name, total_count, correct_count, wrong_count, pass_count, fail_count) "
		"VALUES(?, ?, ?, 1, ?, ?, ?, ?) "
		"ON CONFLICT(user_id, date, textbook_name) DO UPDATE SET "
		"total_count = total_count + 1, "
		"correct_count = correct_count + excluded.correct_count, "
		"wrong_count = wrong_count + excluded.wrong_count, "
		"pass_count = pass_count + excluded.pass_count, "
		"fail_count = fail_count + excluded.fail_count;";

	if (sqlite3_prepare_v2(db, sql_daily, -1, &stmt, nullptr) != SQLITE_OK || !stmt) {
		eteacher::database_manager::RollbackTransaction(db, kTag);
		sqlite3_close(db);
		return;
	}
	const std::string date = TodayDate();
	const int pass_flag = IsSessionPassed() ? 1 : 0;
	const int fail_flag = IsSessionPassed() ? 0 : 1;
	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, textbook_name_.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 4, correct ? 1 : 0);
	sqlite3_bind_int(stmt, 5, correct ? 0 : 1);
	sqlite3_bind_int(stmt, 6, pass_flag);
	sqlite3_bind_int(stmt, 7, fail_flag);
	if (!StepDone(stmt)) {
		sqlite3_finalize(stmt);
		eteacher::database_manager::RollbackTransaction(db, kTag);
		sqlite3_close(db);
		return;
	}
	sqlite3_finalize(stmt);

	if (!eteacher::database_manager::CommitTransaction(db, kTag)) {
		eteacher::database_manager::RollbackTransaction(db, kTag);
	}
	sqlite3_close(db);
}

std::unique_ptr<AppBase> MakeWordPracticeApp() {
	return std::make_unique<WordPracticeApp>();
}

bool WordPracticeApp::PlayAudioFromSd(const std::string &audio_path) {
	if (audio_path.empty()) {
		return false;
	}

	File file = SD.open(audio_path.c_str(), FILE_READ);
	if (!file) {
		ESP_LOGW(kTag, "Open audio failed: %s", audio_path.c_str());
		return false;
	}

	const size_t file_size = static_cast<size_t>(file.size());
	if (file_size == 0) {
		file.close();
		ESP_LOGW(kTag, "Empty audio file: %s", audio_path.c_str());
		return false;
	}

	std::string ogg_data(file_size, '\0');
	const size_t read_size = file.readBytes(ogg_data.data(), static_cast<int>(file_size));
	file.close();
	if (read_size != file_size) {
		ESP_LOGW(kTag, "Read audio failed: %s (%u/%u)", audio_path.c_str(), static_cast<unsigned>(read_size),
				 static_cast<unsigned>(file_size));
		return false;
	}

	AppService::GetInstance().PlaySound(ogg_data);
	return true;
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
