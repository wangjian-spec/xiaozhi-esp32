#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_engine.h"
#include "eteacher/app_ui/ui_router.h"
#include "eteacher/app_ui/widget.h"

class CustomEpdDisplay;

class WordPracticeApp : public AppBase {
public:
	WordPracticeApp() = default;

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;

private:
	struct QuestionData {
		int id = 0;
		int type = 1;
		std::string stage;
		int difficulty = 1;
		std::string content_json;
		std::string answer;
	};

	struct ChoiceState {
		std::string prompt;
		std::string textbook_name;
		std::vector<std::string> option_keys;
		std::vector<std::string> options;
		std::vector<std::string> hints;
		std::vector<std::string> pair_left;
		std::vector<std::string> pair_right;
		std::string expected;
	};

	struct LearnedSnapshot {
		int correct = 0;
		int wrong = 0;
		int64_t last_seen_at = 0;
	};

	bool LoadUi(AppContext &ctx);
	bool LoadScene(AppContext &ctx, const std::string &scene_id, uint16_t scene_index);
	void InitUiEngine();
	void Render(AppContext &ctx);
	void BindWidgets(app_ui::Widget *root);

	void LoadQuestionPool();
	bool PickNextQuestion();
	void PresentCurrentQuestion();
	void ShowSessionSummary();

	void HandleAnswer(AppButton button);
	void HandleType4Action(AppButton button);
	void HandleSpeakAction(AppButton button);
	void HandleType56Action(AppButton button);
	void RefreshType4Widgets();
	void RefreshType56Widgets();
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
	LearnedSnapshot QueryLearned(sqlite3 *db, int question_id, const std::string &textbook) const;
	void SaveAnswerStats(const QuestionData &q, bool correct);

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

	app_ui::LabelWidget *label_a_ = nullptr;
	app_ui::LabelWidget *label_b_ = nullptr;
	app_ui::LabelWidget *label_c_ = nullptr;
	app_ui::LabelWidget *label_d_ = nullptr;

	app_ui::LabelWidget *label_up_ = nullptr;
	app_ui::LabelWidget *label_left_ = nullptr;
	app_ui::LabelWidget *label_down_ = nullptr;
	app_ui::LabelWidget *label_right_ = nullptr;

	std::vector<QuestionData> question_pool_{};
	std::vector<int> recent_types_{};
	size_t current_index_ = 0;
	int current_question_type_ = 1;
	ChoiceState current_choice_{};

	int correct_count_ = 0;
	int wrong_count_ = 0;
	int score_ = 0;
	int total_answered_ = 0;
	int pass_target_questions_ = 10;
	bool awaiting_next_question_ = false;
	std::vector<std::string> type4_left_words_{};
	std::vector<std::string> type4_right_words_{};
	std::vector<int> type4_expected_right_index_{};
	std::vector<int> type4_selected_right_by_left_{};
	int type4_selected_left_index_ = 0;
	std::vector<std::string> type56_words_{};
	int type56_selected_index_ = 0;
	std::string type56_input_answer_;
	std::string textbook_name_ = "default";
};

std::unique_ptr<AppBase> MakeWordPracticeApp();
