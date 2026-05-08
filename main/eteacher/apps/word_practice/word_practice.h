#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <sqlite3.h>
#include <esp_timer.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "eteacher/apps/word_practice/word_practice_flow_controller.h"
#include "eteacher/apps/word_practice/word_practice_learning_module.h"
#include "eteacher/apps/word_practice/word_practice_question_seed_module.h"
#include "eteacher/apps/word_practice/word_practice_quiz_module.h"
#include "eteacher/apps/word_practice/word_practice_result_module.h"
#include "eteacher/apps/word_practice/word_practice_selection_module.h"
#include "eteacher/apps/word_practice/word_practice_session_evaluator.h"
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
	bool ShouldInterceptSelectExit() const override;

private:
	using QuestionData = word_practice::QuestionData;
	using ChoiceState = word_practice::ChoiceState;

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

	enum class UiMode {
		HomePreview,
		Practicing,
	};

	enum class OverlayMode {
		None,
		Settlement,
	};

	enum class DialogFocus {
		Confirm,
		Cancel,
	};

	struct TodayMissionData {
		int daily_new_word_target = 10;
		int daily_review_word_target = 5;
		int daily_total_target = 15;
		int completed_words = 0;
	};

	struct DeviceJsonData {
		std::string device_id;
		std::string firmware;
	};

	struct PracticeStatsData {
		int continuous_days = 1;
		std::string last_practice_date;
	};

	struct UserJsonData {
		int user_id = 0;
		std::string name = "student";
		std::string current_stage = "stage1";
		std::string time_source = "wifi_system";
		int level = 0;
		TodayMissionData today_mission{};
		bool enable_read_questions = true;
		int today_progress_percent = 0;
		int mastered_words = 0;
		std::vector<int> stage_levelup_count = std::vector<int>(12, 20);
		DeviceJsonData device{};
		PracticeStatsData practice_stats{};
	};

	struct SessionSummaryData {
		int duration_seconds = 0;
		int accuracy_percent = 0;
		int total_questions = 0;
		int wrong_questions = 0;
		int skipped_questions = 0;
		int new_word_total = 0;
		int new_word_mastered = 0;
		int review_word_total = 0;
		int review_word_correct = 0;
		int mastered_before = 0;
		int mastered_after = 0;
		int progress_before = 0;
		int progress_after = 0;
		bool level_up = false;
		int continuous_days = 1;
		std::vector<std::string> wrong_words{};
	};

	struct TodayTargetProgressState {
		int completed_words = 0;
		int progress_percent = 0;
		bool all_completed = false;
	};

	struct AttemptUiStateSnapshot {
		word_practice::SessionModule::Snapshot session{};
		int cycle_correct_count = 0;
		int cycle_wrong_count = 0;
		int cycle_skip_count = 0;
		int current_speak_asr_failure_count = 0;
		std::vector<std::string> learned_words{};
		std::vector<int> type4_selected_right_by_left{};
		int type4_selected_left_index = 0;
		bool type56_show_correct_answer = false;
		std::string type56_correct_answer_display{};
		std::vector<std::string> type56_dialog_items{};
		int type56_selected_index = 0;
		std::string type56_input_answer{};
	};

	bool LoadUi(AppContext &ctx);
	bool LoadScene(AppContext &ctx, const std::string &scene_id, uint16_t scene_index);
	bool ActivateScene(AppContext &ctx, const std::string &scene_id);
	void InitUiEngine();
	void Render(AppContext &ctx);
	void BindWidgets(app_ui::Widget *root);
	void ShowHomePreview(AppContext &ctx);
	void LoadHomePreviewSelection();
	void RefreshHomePreview();
	void RefreshSelectionDialog();
	void HideSelectionDialog();
	void SetWidgetVisibleById(uint32_t widget_id, bool visible);
	void HandleHomePreviewAction(AppContext &ctx, const ButtonEvent &event);
	void HandleSettlementAction(AppContext &ctx, const ButtonEvent &event);
	void StartPracticeRound(AppContext &ctx);

	void ResetLoadedQuestionDataCache();
	void ResetMasteryProfileCache();
	void RebuildMasteryProfileCache();
	void UpdateMasteryProfileCache(const word_practice::WordMasteryProfile &profile);
	bool TryAppendSeedFromCache(const word_practice::SelectedWord &selected_word);
	const word_practice::WordMasteryProfile *FindCachedMasteryProfile(int word_id) const;
	void BuildQuestionPoolFromSelectedWords(const std::vector<word_practice::SelectedWord> &selected_words,
						 int stage_index,
						 int select_words_ms);
	void LoadQuestionPool();
	bool WarmQuestionCandidates(size_t target_seed_count, const char *reason);
	bool PickNextQuestion();
	bool CommitScheduledQuestion(const word_practice::ScheduledQuestion &scheduled);
	void PresentCurrentQuestion();
	void ShowSessionSummary(bool finalize_round = true);
	void ResetRoundState();
	void UpdateSessionState(bool scheduler_exhausted = false);
	bool RecordCurrentAttempt(sqlite3 *db, const QuestionData &q, bool correct, bool skipped = false);
	void MaybeRecordRoundCompletion();
	std::string BuildRoundGoalText() const;
	std::string BuildQuestionReasonText(const word_practice::ScheduledQuestion &scheduled) const;
	std::string BuildWordFeedbackText(const word_practice::WordMasteryProfile &before,
					 const word_practice::WordMasteryProfile &after,
					 word_practice::BatchWordKind kind,
					 word_practice::TrainingSkill skill,
					 bool correct) const;

	void HandleAnswer(AppButton button);
	void HandleType4Action(AppButton button);
	void HandleSpeakAction(const ButtonEvent &event);
	void HandleType56Action(AppButton button);
	void RefreshType4Widgets();
	void PlayType4FocusedWordAudioIfNeeded();
	void RefreshType56Widgets();
	void UpdateQuestionPromptPresentation(int question_type, const std::string &prompt);
	void UpdateAsrResultPresentation(int question_type, const std::string &result_text);
	void ResetCycleScoreState();
	void RecordCycleAnswer(bool correct);
	void RecordCycleSkip();
	void SyncScoreLabels();
	void AddLearnedWordsFromText(const std::string &text);
	void HideSettlementLearnedWordLabels();
	void RenderSettlementLearnedWordLabels();
	void HideHomeLevelIconLabels();
	void RenderHomeLevelIconLabels();
	QuestionPromptProfile BuildQuestionPromptProfile(int question_type) const;
	word_practice::SessionEvaluation EvaluateSession() const;
	SessionSummaryData BuildSessionSummaryData() const;
	std::string BuildSettlementDialogText(const SessionSummaryData &summary) const;
	std::string BuildTodayMissionText() const;
	std::string BuildWordPreviewText() const;
	std::string BuildPracticeWordGridText() const;
	void UpdatePracticeWordTextarea();
	void ApplyCurrentQuestionWidgets(const QuestionData &q);
	int DisplayedPracticeProgressPercent() const;
	TodayTargetProgressState ComputeTodayTargetProgress(const std::vector<word_practice::SelectedWord> &selected_words,
						     const std::vector<word_practice::WordMasteryProfile> *profiles = nullptr) const;
	bool LoadPersistedTodayTargets(int stage_index,
				      std::vector<word_practice::SelectedWord> *selected_words,
				      int *next_new_word_cursor) const;
	void SavePersistedTodayTargets(int stage_index,
				      const std::vector<word_practice::SelectedWord> &selected_words,
				      int next_new_word_cursor) const;
	void ClearPersistedTodayTargets(int stage_index) const;
	void UpdatePracticeProgressWidget(bool visible);
	int CurrentStageIndex() const;
	int ComputeDisplayLevel() const;
	int QueryMasteredWordCount() const;
	int LoadStageCursorState(int stage_index) const;
	void SaveStageCursorState(int stage_index, int cursor_value);
	void SyncUserProgressState();
	bool LoadUserJson();
	bool SaveUserJson() const;

	std::string ButtonToken(AppButton button) const;
	bool SaveAnswerStats(const QuestionData &q, bool correct, bool skipped = false);
	AttemptUiStateSnapshot CaptureAttemptUiStateSnapshot() const;
	void RestoreAttemptUiStateSnapshot(const AttemptUiStateSnapshot &snapshot);
	bool PlayAudioFromSd(const std::string &audio_path);
	bool EnsureAudioBundleIndexLoaded(const std::string &audio_path);
	bool ReadAudioBundleEntry(const std::string &audio_path, const std::string &audio_name, std::string *ogg_data);
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
	app_ui::LabelWidget *label_my_stage_ = nullptr;
	app_ui::LabelWidget *label_my_level_ = nullptr;
	app_ui::LabelWidget *label_progress_ = nullptr;
	app_ui::LabelWidget *label_daily_target_ = nullptr;
	app_ui::LabelWidget *label_read_setting_ = nullptr;
	app_ui::LabelWidget *label_today_mission_ = nullptr;
	app_ui::LabelWidget *label_word_preview_ = nullptr;
	app_ui::LabelWidget *label_mission_progress_ = nullptr;
	app_ui::LabelWidget *label_progress_percent_ = nullptr;
	app_ui::TextWidget *bottom_bar_ = nullptr;
	app_ui::TextAreaWidget *textarea_input_answer_ = nullptr;
	app_ui::TextAreaWidget *textarea_practice_word_ = nullptr;
	app_ui::DialogWidget *dialog_select_board_ = nullptr;
	app_ui::DialogWidget *dialog_setting_result_ = nullptr;
	app_ui::ListViewWidget *listview_select_ = nullptr;
	app_ui::ButtonWidget *button_confirm_ = nullptr;
	app_ui::ButtonWidget *button_cancle_ = nullptr;
	app_ui::ButtonWidget *button_stage_setting_ = nullptr;
	app_ui::ButtonWidget *button_mission_setting_ = nullptr;
	app_ui::CheckboxWidget *checkbox_has_read_ = nullptr;
	app_ui::CheckboxWidget *checkbox_no_read_ = nullptr;
	app_ui::ProgressWidget *progress_today_mission_ = nullptr;
	app_ui::ProgressWidget *progress_practice_ = nullptr;

	app_ui::ImageWidget *image_good_ = nullptr;
	app_ui::ImageWidget *image_bad_ = nullptr;
	app_ui::ImageWidget *image_public_speaker_ = nullptr;
	app_ui::ImageWidget *image_sun_moon_star_ = nullptr;
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
	app_ui::Rect image_sun_moon_star_static_rect_{};

	word_practice::SelectionModule selection_module_{};
	word_practice::QuestionSeedModule question_seed_module_{};
	word_practice::QuizModule quiz_module_{};
	word_practice::SessionModule session_module_{};
	word_practice::UserProgressDao progress_dao_{};
	word_practice::WordMasteryDao mastery_dao_{};
	word_practice::LearningBatchPlanner batch_planner_{};
	word_practice::BatchProgressTracker batch_progress_tracker_{};
	word_practice::QuestionScheduler question_scheduler_{};
	word_practice::WordSelectionConfig word_selection_config_{};
	std::vector<word_practice::SelectedWord> selected_words_{};
	std::vector<word_practice::VocabularySeed> question_seed_pool_{};
	std::unordered_map<int, word_practice::VocabularySeed> seed_cache_{};
	std::unordered_map<int, std::vector<int>> available_question_types_by_word_{};
	size_t next_seed_pool_load_index_ = 0;
	std::vector<word_practice::WordMasteryProfile> mastery_profiles_{};
	std::unordered_map<int, word_practice::WordMasteryProfile> mastery_profile_cache_{};
	word_practice::LearningBatch learning_batch_{};
	word_practice::ScheduledQuestion current_scheduled_question_{};
	word_practice::CurrentQuestionSlot current_question_slot_{};
	std::string current_textbook_name_ = "default";
	std::string current_round_goal_text_{};
	std::string last_attempt_feedback_text_{};

	int current_question_type_ = 1;
	ChoiceState current_choice_{};

	int pass_target_questions_ = 18;
	int completed_rounds_for_textbook_ = 0;
	std::vector<std::string> type4_left_words_{};
	std::vector<std::string> type4_left_audio_filenames_{};
	std::vector<std::string> type4_right_words_{};
	std::vector<int> type4_expected_right_index_{};
	std::vector<int> type4_selected_right_by_left_{};
	int type4_selected_left_index_ = 0;
	int type4_last_spoken_left_index_ = -1;
	std::vector<std::string> type56_words_{};
	std::vector<std::string> type56_dialog_items_{};
	int type56_selected_index_ = 0;
	int type56_grid_cols_ = 1;
	std::string type56_input_answer_;
	bool type56_show_correct_answer_ = false;
	std::string type56_correct_answer_display_{};
	std::vector<std::string> learned_words_this_round_{};
	std::vector<std::string> wrong_words_this_round_{};
	std::vector<int> wrong_word_ids_this_round_{};
	std::vector<int> last_session_wrong_word_ids_{};
	std::vector<app_ui::LabelWidget *> settlement_learned_word_labels_{};
	std::vector<app_ui::LabelWidget *> home_level_icon_labels_{};
	QuestionPromptProfile question_prompt_profile_{};
	std::string current_audio_path_;
	int64_t current_question_presented_at_ms_ = 0;
	esp_timer_handle_t question_audio_timer_ = nullptr;
	bool audio_bundle_index_loaded_ = false;
	bool audio_bundle_index_available_ = false;
	std::string audio_bundle_resolved_path_{};
	std::unordered_map<std::string, AudioBundleEntry> audio_bundle_entries_{};
	bool speak_recording_ = false;
	int current_speak_asr_failure_count_ = 0;
	bool enable_speak_questions_ = true;
	word_practice::LearningMode current_learning_mode_ = word_practice::LearningMode::Normal;
	bool round_completion_recorded_ = false;
	bool session_scheduler_exhausted_ = false;
	bool home_preview_selection_ready_ = false;
	int home_preview_stage_index_ = 0;
	int home_preview_next_new_word_cursor_ = -1;
	int seed_cache_stage_index_ = 0;
	int mastery_profile_cache_user_id_ = -1;
	std::string mastery_profile_cache_textbook_name_{};
	word_practice::PracticeFlowController practice_flow_controller_{};
	UiMode ui_mode_ = UiMode::HomePreview;
	OverlayMode overlay_mode_ = OverlayMode::None;
	DialogFocus dialog_focus_ = DialogFocus::Confirm;
	UserJsonData user_json_{};
	int current_user_id_ = 0;
	SessionSummaryData last_session_summary_{};
	int session_started_at_sec_ = 0;
	int session_mastered_words_before_ = 0;
	int session_progress_before_ = 0;
	int cycle_correct_count_ = 0;
	int cycle_wrong_count_ = 0;
	int cycle_skip_count_ = 0;
};

std::unique_ptr<AppBase> MakeWordPracticeApp();
