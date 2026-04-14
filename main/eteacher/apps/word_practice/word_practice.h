#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <sqlite3.h>
#include <esp_timer.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/apps/word_practice/word_practice_quiz_module.h"
#include "eteacher/apps/word_practice/word_practice_result_module.h"
#include "eteacher/apps/word_practice/word_practice_selection_module.h"
#include "eteacher/apps/word_practice/word_practice_session_module.h"
#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_engine.h"
#include "eteacher/app_ui/ui_router.h"
#include "eteacher/app_ui/widget.h"

class WordPracticeApp : public AppBase, public CustomEpdDisplay::ChatMessageListener {
public:
	WordPracticeApp() = default;

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;
	void OnChatMessage(const char* role, const char* content) override;

private:
	using QuestionData = word_practice::QuestionData;
	using ChoiceState = word_practice::ChoiceState;
	using QuestionSelectionStrategy = word_practice::QuestionSelectionStrategy;

	struct AudioBundleEntry {
		uint64_t offset = 0;
		uint64_t size = 0;
	};

	struct QuestionPromptProfile {
		bool visible = false;
		int dash_width = 0;
		int dash_gap_px = 2;
		int max_lines = 2;
		int dash_black_len = 4;
		int dash_white_len = 1;
	};

	bool LoadUi(AppContext &ctx);
	bool LoadScene(AppContext &ctx, const std::string &scene_id, uint16_t scene_index);
	void InitUiEngine();
	void Render(AppContext &ctx);
	void BindWidgets(app_ui::Widget *root);

	void LoadQuestionPool();
	bool PickNextQuestion();
	bool PickNextQuestionByLegacyAdaptive();
	bool PickNextQuestionByTypeCycleRandom();
	void CommitSelectedQuestion(size_t index);
	void PresentCurrentQuestion();
	void ShowSessionSummary();

	void HandleAnswer(AppButton button);
	void HandleType4Action(AppButton button);
	void HandleSpeakAction(const ButtonEvent &event);
	void HandleType56Action(AppButton button);
	void RefreshType4Widgets();
	void RefreshType56Widgets();
	void UpdateQuestionPromptPresentation(int question_type, const std::string &prompt);
	void UpdateAsrResultPresentation(int question_type, const std::string &result_text);
	void SyncScoreLabels();
	void AddLearnedWordsFromText(const std::string &text);
	void HideSettlementLearnedWordLabels();
	void RenderSettlementLearnedWordLabels();
	QuestionPromptProfile BuildQuestionPromptProfile(int question_type) const;
	bool IsSessionPassed() const;

	std::string SelectSceneIdByType(int question_type) const;
	std::string TypeTitle(int question_type) const;
	std::string TypeInstruction(int question_type) const;

	ChoiceState BuildChoiceState(const QuestionData &q) const;
	std::string NormalizeAnswerToken(std::string value) const;
	std::string NormalizePairWord(const std::string &value) const;
	std::string ButtonToken(AppButton button) const;

	std::string DiscoverQuestionDbPath() const;
	std::string DiscoverUserDbPath() const;
	bool EnsureStatsTables(sqlite3 *db) const;
	int QueryCurrentLevel(sqlite3 *db) const;
	word_practice::LearnedSnapshot QueryLearned(sqlite3 *db, int question_id, const std::string &textbook) const;
	void SaveAnswerStats(const QuestionData &q, bool correct);
	bool PlayAudioFromSd(const std::string &audio_path);
	bool EnsureAudioBundleIndexLoaded();
	bool ReadAudioBundleEntry(const std::string &audio_name, std::string *ogg_data);
	std::string ResolveBundledImagePath(const std::string &image_name);
	void ScheduleQuestionAudioAutoPlay();
	void CancelQuestionAudioAutoPlay();
	static void QuestionAudioTimerCallback(void *arg);

	app_ui::UIEngine ui_engine_{};
	app_ui::runtime::SceneRuntime scene_runtime_{};
	UiRouter router_{};
	AppContext *ctx_ = nullptr;
	std::vector<std::string> scene_ids_{};
	uint16_t scene_load_id_ = 0;
	CustomEpdDisplay *epd_ = nullptr;
	bool ui_ready_ = false;

	app_ui::Widget *root_ = nullptr;
	app_ui::LabelWidget *label_question_type_ = nullptr;
	app_ui::LabelWidget *label_correct_count_ = nullptr;
	app_ui::LabelWidget *label_wrong_count_ = nullptr;
	app_ui::LabelWidget *label_alert_ = nullptr;
	app_ui::LabelWidget *label_question_ = nullptr;
	app_ui::LabelWidget *label_asr_result_ = nullptr;
	app_ui::LabelWidget *label_press_aread_ = nullptr;
	app_ui::LabelWidget *label_press_d_skip_ = nullptr;
	app_ui::TextWidget *bottom_bar_ = nullptr;
	app_ui::TextAreaWidget *textarea_input_answer_ = nullptr;
	app_ui::DialogWidget *dialog_select_board_ = nullptr;

	app_ui::ImageWidget *image_good_ = nullptr;
	app_ui::ImageWidget *image_bad_ = nullptr;
	app_ui::ImageWidget *image_public_speaker_ = nullptr;
	app_ui::ImageWidget *image_a_ = nullptr;
	app_ui::ImageWidget *image_b_ = nullptr;
	app_ui::ImageWidget *image_c_ = nullptr;
	app_ui::ImageWidget *image_d_ = nullptr;
	app_ui::ImageWidget *image_write_ = nullptr;
	app_ui::ImageWidget *image_input_ = nullptr;

	app_ui::LabelWidget *label_a_ = nullptr;
	app_ui::LabelWidget *label_b_ = nullptr;
	app_ui::LabelWidget *label_c_ = nullptr;
	app_ui::LabelWidget *label_d_ = nullptr;
	app_ui::LabelWidget *label_question_line2_ = nullptr;
	app_ui::LabelWidget *label_question_line3_ = nullptr;
	app_ui::Widget *question_dash_line1_ = nullptr;
	app_ui::Widget *question_dash_line2_ = nullptr;
	app_ui::Widget *question_dash_line3_ = nullptr;
	app_ui::LabelWidget *label_asr_line2_ = nullptr;
	app_ui::LabelWidget *label_asr_line3_ = nullptr;
	app_ui::Widget *asr_dash_line1_ = nullptr;
	app_ui::Widget *asr_dash_line2_ = nullptr;
	app_ui::Widget *asr_dash_line3_ = nullptr;

	app_ui::LabelWidget *label_up_ = nullptr;
	app_ui::LabelWidget *label_left_ = nullptr;
	app_ui::LabelWidget *label_down_ = nullptr;
	app_ui::LabelWidget *label_right_ = nullptr;
	app_ui::Rect label_question_static_rect_{};
	app_ui::Rect label_asr_static_rect_{};
	app_ui::Rect image_public_speaker_static_rect_{};

	word_practice::SelectionModule selection_module_{};
	word_practice::QuizModule quiz_module_{};
	word_practice::SessionModule session_module_{};
	word_practice::ResultModule result_module_{};
	word_practice::WordSelectionConfig word_selection_config_{};

	int current_question_type_ = 1;
	ChoiceState current_choice_{};

	int pass_target_questions_ = 12;
	std::vector<std::string> type4_left_words_{};
	std::vector<std::string> type4_right_words_{};
	std::vector<int> type4_expected_right_index_{};
	std::vector<int> type4_selected_right_by_left_{};
	int type4_selected_left_index_ = 0;
	std::vector<std::string> type56_words_{};
	int type56_selected_index_ = 0;
	int type56_grid_cols_ = 1;
	std::string type56_input_answer_;
	std::vector<std::string> learned_words_this_round_{};
	std::vector<app_ui::LabelWidget *> settlement_learned_word_labels_{};
	QuestionPromptProfile question_prompt_profile_{};
	std::string current_audio_path_;
	esp_timer_handle_t question_audio_timer_ = nullptr;
	bool audio_bundle_index_loaded_ = false;
	bool audio_bundle_index_available_ = false;
	std::string audio_bundle_resolved_path_{};
	std::unordered_map<std::string, AudioBundleEntry> audio_bundle_entries_{};
	bool speak_recording_ = false;
	QuestionSelectionStrategy question_selection_strategy_ = QuestionSelectionStrategy::TypeCycleRandom;
};

std::unique_ptr<AppBase> MakeWordPracticeApp();
