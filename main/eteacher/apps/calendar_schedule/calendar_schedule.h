#pragma once

#include <memory>
#include <string>
#include <vector>

#include "eteacher/app_manager/app_base.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/app_ui/ui_engine.h"
#include "eteacher/app_ui/ui_router.h"
#include "eteacher/app_ui/widget.h"

class CustomEpdDisplay;

class CalendarScheduleApp : public AppBase {
public:
	CalendarScheduleApp() = default;

	struct DateInfo {
		int year = 0;
		int month = 0;
		int day = 0;
	};

	MenuMeta GetMenuMeta() const override;
	void OnEnter(AppContext &ctx) override;
	void OnExit(AppContext &ctx) override;
	void OnButton(AppContext &ctx, const ButtonEvent &event) override;
	bool ShouldInterceptSelectExit() const override;

private:
	enum class TodoFilter {
		All,
		Today,
		ThisWeek,
	};

	struct TaskEntry {
		int rowid = 0;
		std::string content;
		std::string date;
		std::string priority;
		bool done = false;
		bool deleted = false;
	};

	bool LoadUi(AppContext &ctx);
	bool LoadScene(AppContext &ctx, const std::string &scene_id, uint16_t scene_index);
	void InitUiEngine();
	void Render(AppContext &ctx);
	void PrevScene(AppContext &ctx);
	void NextScene(AppContext &ctx);

	void BindWidgets(app_ui::Widget *root);
	void BuildFocusCycle(const std::string &scene_id);
	void SyncFocusCycleIndex();
	bool HandleFocusCycle(const ButtonEvent &event);
	bool HandleTabViewNav(AppContext &ctx, const ButtonEvent &event);
	bool HandleAlertDialogButtons(const ButtonEvent &event);
	bool HandleTodoFilterAction(const ButtonEvent &event);
	void SendInputToUi(const ButtonEvent &event);

	void UpdateTabSelection();
	void UpdateBottomBarHintByFocus();
	void SetBottomBarHint(const std::string &text);

	void EnterCalendarScene(AppContext &ctx);
	void EnterScheduleSettingsScene(AppContext &ctx);
	void RefreshCalendarData();
	bool IsNetworkConnected() const;
	void ShowCalendarAlert(const std::string &text);
	void HideCalendarAlert();
	void ShowDeleteConfirmAlertForSelectedTask();
	bool ToggleSelectedTaskDone();
	bool ToggleTaskDeletedByRowId(int rowid);

	void SetTodoFilter(TodoFilter filter);
	void UpdateTodoFilterChecks();
	void RefreshTodoLists();
	std::vector<TaskEntry> QueryTasks() const;

	static bool IsClickLike(const ButtonEvent &event);
	static bool MapButtonToKey(AppButton button, app_ui::KeyCode &out);

	app_ui::UIEngine ui_engine_{};
	app_ui::runtime::SceneRuntime scene_runtime_{};
	UiRouter router_{};
	uint16_t scene_load_id_ = 0;
	CustomEpdDisplay *epd_ = nullptr;
	bool ui_ready_ = false;
	bool alert_dialog_visible_ = false;
	bool is_calendar_scene_ = false;
	int calendar_view_year_ = 0;
	int calendar_view_month_ = 0;
	TodoFilter todo_filter_ = TodoFilter::Today;
	std::string bottom_bar_hint_{};
	bool delete_confirm_mode_ = false;
	int pending_delete_rowid_ = 0;
	std::vector<TaskEntry> visible_tasks_{};
	std::vector<uint32_t> focus_cycle_ids_{};
	int focus_cycle_index_ = 0;

	app_ui::Widget *root_ = nullptr;
	app_ui::TabViewWidget *tabview_ = nullptr;
	app_ui::BottomBarWidget *bottom_bar_ = nullptr;
	app_ui::DialogWidget *dialog_alert_ = nullptr;
	app_ui::LabelWidget *label_alert_ = nullptr;
	app_ui::ButtonWidget *button_submit_ = nullptr;
	app_ui::ButtonWidget *button_cancel_ = nullptr;

	app_ui::FrameWidget *frame_calendar_ = nullptr;
	app_ui::Widget *calendar_grid_ = nullptr;
	app_ui::ListViewWidget *listview_task_ = nullptr;
	app_ui::RadioWidget *radio_today_ = nullptr;
	app_ui::RadioWidget *radio_this_week_ = nullptr;
	app_ui::RadioWidget *radio_all_ = nullptr;
	app_ui::CheckboxWidget *checkbox_todo_ = nullptr;
	app_ui::CheckboxWidget *checkbox_done_ = nullptr;
	app_ui::CheckboxWidget *checkbox_delete_ = nullptr;
	app_ui::ButtonWidget *button_phone_connect_ = nullptr;
	app_ui::ImageWidget *image_schedule_qr_ = nullptr;
	app_ui::LabelWidget *label_schedule_url_ = nullptr;
};

std::unique_ptr<AppBase> MakeCalendarScheduleApp();
