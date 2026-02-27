#include "eteacher/apps/dictionary/dictionary.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include <SD.h>
#include <SPI.h>
#include <esp_log.h>
#include <sqlite3.h>

#include "boards/EnglishTeacher/custom_epd_display.h"
#include "boards/EnglishTeacher/config.h"
#include "eteacher/app_ui/input.h"
#include "eteacher/app_ui/scene.h"
#include "eteacher/apps/dictionary/dictionary_ui.h"
#include "eteacher/database_manager/database_debug.h"
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
constexpr const char *kTag = "DictionaryApp";
constexpr const app_ui::desc::UiDesc *kUiDesc = &app_ui::generated::dictionary::kUi;

constexpr uint32_t kWidgetTextAreaWord = 0x93B8C7C4u;
constexpr uint32_t kWidgetButtonAdd = 0x7CEB7B41u;
constexpr uint32_t kWidgetLabelStatus = 0x79252C88u;
constexpr uint32_t kWidgetFrameResult = 0x8B8E0425u;
constexpr uint32_t kWidgetBottomBar = 0x3D8F28B1u;
constexpr uint32_t kWidgetSoftKeyboard = 0xD7467530u;
constexpr int kDefaultUserId = 0;

constexpr const char *kStatusNotFound = "查询不到该单词";
constexpr const char *kStatusInBook = "单词在生词表中，B删除";
constexpr const char *kStatusNotInBook = "该单词不在生词表，D添加";

bool IsClickLike(const ButtonEvent &event) {
	return event.action == ButtonAction::Click || event.action == ButtonAction::PressDown ||
		   event.action == ButtonAction::LongPress;
}

bool MapButtonToKey(AppButton button, app_ui::KeyCode &out) {
	switch (button) {
		case AppButton::Up:
			out = app_ui::KeyCode::Up;
			return true;
		case AppButton::Down:
			out = app_ui::KeyCode::Down;
			return true;
		case AppButton::Left:
			out = app_ui::KeyCode::Left;
			return true;
		case AppButton::Right:
			out = app_ui::KeyCode::Right;
			return true;
		case AppButton::A:
			out = app_ui::KeyCode::A;
			return true;
		case AppButton::B:
			out = app_ui::KeyCode::B;
			return true;
		case AppButton::C:
			out = app_ui::KeyCode::C;
			return true;
		case AppButton::D:
			out = app_ui::KeyCode::D;
			return true;
		case AppButton::Select:
			out = app_ui::KeyCode::Select;
			return true;
		case AppButton::Start:
			out = app_ui::KeyCode::Start;
			return true;
		case AppButton::VolumeUp:
			out = app_ui::KeyCode::VolumeUp;
			return true;
		case AppButton::VolumeDown:
			out = app_ui::KeyCode::VolumeDown;
			return true;
		default:
			return false;
	}
}

std::string NormalizeWord(const std::string &text) {
	size_t start = 0;
	while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
		++start;
	}
	size_t end = text.size();
	while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
		--end;
	}
	std::string out = text.substr(start, end - start);
	std::replace(out.begin(), out.end(), '\n', ' ');
	std::replace(out.begin(), out.end(), '\r', ' ');
	std::replace(out.begin(), out.end(), '\t', ' ');
	return out;
}

std::string ToLowerAscii(std::string text) {
	std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return text;
}

std::string DiscoverDbPath() {
	return eteacher::database_manager::DiscoverDictionaryDbPath(kTag);
}

bool EnsureSqliteSdMounted() {
	return eteacher::database_manager::EnsureSqliteSdMounted(kTag);
}

bool EnsureSqliteRuntimeReady() {
	return eteacher::database_manager::EnsureSqliteRuntimeReady(kTag);
}

std::string DiscoverWordBookDbPath() {
	return eteacher::database_manager::DiscoverUserDataDbPath(kTag);
}

bool OpenValidatedWordDbReadonly(const std::string &discovered_path, sqlite3 **out_db, std::string *out_path) {
	return eteacher::database_manager::OpenValidatedDictionaryDbReadonly(discovered_path, out_db, out_path, kTag);
}

bool AttachValidatedWordDb(sqlite3 *db, const std::string &discovered_path, std::string *attached_path) {
	return eteacher::database_manager::AttachValidatedDictionaryDb(db, discovered_path, attached_path, "dictdb", kTag);
}

}

MenuMeta DictionaryApp::GetMenuMeta() const {
	return MenuMeta{"dictionary", "中英词典", "Start查询 Select退出"};
}

bool DictionaryApp::ShouldInterceptSelectExit() const {
	return keyboard_visible_;
}

void DictionaryApp::OnEnter(AppContext &ctx) {
	ui_ready_ = false;
	keyboard_visible_ = false;
	suppress_auto_keyboard_ = false;
	current_word_.clear();
	current_word_found_ = false;
	root_ = nullptr;
	textarea_word_ = nullptr;
	button_add_ = nullptr;
	label_status_ = nullptr;
	frame_result_ = nullptr;
	bottom_bar_ = nullptr;
	keyboard_ = nullptr;

	router_.Reset();
	scene_load_id_ = 0;
	epd_ = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());

	if (!LoadUi(ctx)) {
		return;
	}

	InitUiEngine();
	router_.SetActivateFn([this](AppContext &context, size_t /*index*/, const std::string &scene_id) {
		return LoadScene(context, scene_id, ++scene_load_id_);
	});

	if (router_.Activate(ctx, 0)) {
		Render(ctx);
	}
}

void DictionaryApp::OnExit(AppContext &ctx) {
	(void)ctx;
	ui_ready_ = false;
	keyboard_visible_ = false;
	suppress_auto_keyboard_ = false;
	current_word_.clear();
	current_word_found_ = false;

	root_ = nullptr;
	textarea_word_ = nullptr;
	button_add_ = nullptr;
	label_status_ = nullptr;
	frame_result_ = nullptr;
	bottom_bar_ = nullptr;
	keyboard_ = nullptr;

	epd_ = nullptr;
	router_.Reset();
}

void DictionaryApp::OnButton(AppContext &ctx, const ButtonEvent &event) {
	if (!ui_ready_ || !IsClickLike(event)) {
		return;
	}

	if (keyboard_visible_) {
		if (event.id == AppButton::Select) {
			HideKeyboard(true);
			UpdateBottomBarHintByFocus();
			Render(ctx);
			return;
		}
		if (event.id == AppButton::B) {
			DeleteInputChar();
			UpdateBottomBarHintByFocus();
			Render(ctx);
			return;
		}
		if (event.id == AppButton::Start) {
			QueryCurrentWordAndDisplay();
			HideKeyboard(false);
			UpdateBottomBarHintByFocus();
			Render(ctx);
			return;
		}

		app_ui::KeyCode key{};
		if (MapButtonToKey(event.id, key) && keyboard_) {
			app_ui::InputEvent input_event;
			input_event.type = (event.action == ButtonAction::LongPress) ? app_ui::InputType::KeyRepeat
																		  : app_ui::InputType::KeyDown;
			input_event.key = static_cast<int>(key);
			input_event.timestamp = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
			keyboard_->OnInput(input_event, app_ui::InputPhase::Target);
		}
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if ((event.id == AppButton::Start || event.id == AppButton::C) && button_add_ && button_add_->Focused()) {
		if (current_word_found_ && !current_word_.empty()) {
			if (IsWordInBook(current_word_)) {
				SetStatusText(kStatusInBook);
			} else {
				SetStatusText(kStatusNotInBook);
			}
		}
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (event.id == AppButton::D) {
		if (current_word_found_ && !current_word_.empty() && !IsWordInBook(current_word_)) {
			if (AddWordToBook(current_word_)) {
				SetStatusText(kStatusInBook);
			}
		}
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (event.id == AppButton::B) {
		if (current_word_found_ && !current_word_.empty() && IsWordInBook(current_word_)) {
			if (RemoveWordFromBook(current_word_)) {
				SetStatusText(kStatusNotInBook);
			}
		}
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	if (event.id == AppButton::Start && textarea_word_ && textarea_word_->Focused()) {
		ShowKeyboard();
		UpdateBottomBarHintByFocus();
		Render(ctx);
		return;
	}

	app_ui::KeyCode key{};
	if (MapButtonToKey(event.id, key)) {
		app_ui::InputEvent input_event;
		input_event.type = (event.action == ButtonAction::LongPress) ? app_ui::InputType::KeyRepeat
																	  : app_ui::InputType::KeyDown;
		input_event.key = static_cast<int>(key);
		ui_engine_.OnInput(input_event);
	}

	TryAutoShowKeyboardByFocus();
	UpdateBottomBarHintByFocus();
	Render(ctx);
}

bool DictionaryApp::LoadUi(AppContext &ctx) {
	auto scene_ids = app_ui::CollectSceneIds(*kUiDesc);
	if (scene_ids.empty()) {
		ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: no scenes");
		return false;
	}
	router_.SetScenes(std::move(scene_ids));
	ui_ready_ = true;
	return true;
}

bool DictionaryApp::LoadScene(AppContext &ctx, const std::string &scene_id, uint16_t scene_index) {
	if (!scene_runtime_.LoadFromDesc(*kUiDesc, scene_id.c_str(), scene_index)) {
		ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: failed to load scene");
		return false;
	}

	auto new_root = scene_runtime_.TakeRoot();
	if (!new_root) {
		ctx.board.GetDisplay()->SetChatMessage("system", "Dictionary UI: failed to take scene root");
		return false;
	}

	root_ = new_root.get();
	BindWidgets(root_);
	ui_engine_.SetRoot(std::move(new_root));

	if (textarea_word_) {
		textarea_word_->SetText("");
		ui_engine_.RequestFocus(textarea_word_->Id());
		ShowKeyboard();
	}

	UpdateBottomBarHintByFocus();
	return true;
}

void DictionaryApp::InitUiEngine() {
	ui_engine_.Reset();
	ui_engine_.SetEpd(epd_);
}

void DictionaryApp::Render(AppContext &ctx) {
	if (!ui_ready_) {
		return;
	}

	ui_engine_.RequestRender();
	if (!epd_) {
		std::string msg = "Dictionary UI\n";
		msg += "Scene: #";
		msg += std::to_string(scene_runtime_.SceneId());
		ctx.board.GetDisplay()->SetChatMessage("system", msg.c_str());
	}
}

void DictionaryApp::BindWidgets(app_ui::Widget *root) {
	if (!root) {
		return;
	}

	textarea_word_ = dynamic_cast<app_ui::TextAreaWidget *>(root->FindById(kWidgetTextAreaWord));
	button_add_ = dynamic_cast<app_ui::ButtonWidget *>(root->FindById(kWidgetButtonAdd));
	label_status_ = dynamic_cast<app_ui::LabelWidget *>(root->FindById(kWidgetLabelStatus));
	frame_result_ = dynamic_cast<app_ui::FrameWidget *>(root->FindById(kWidgetFrameResult));
	bottom_bar_ = dynamic_cast<app_ui::BottomBarWidget *>(root->FindById(kWidgetBottomBar));
	keyboard_ = dynamic_cast<app_ui::SoftKeyboardWidget *>(root->FindById(kWidgetSoftKeyboard));

	if (keyboard_) {
		keyboard_->SetOnKey(&DictionaryApp::OnKeyboardKey, this);
		keyboard_->SetVisible(false);
	}
	if (textarea_word_) {
		textarea_word_->SetText("");
	}
	if (label_status_) {
		label_status_->SetFontName("wenquanyi_11pt");
		label_status_->SetText("");
	}
	if (frame_result_) {
		frame_result_->SetFontName("wenquanyi_11pt");
		frame_result_->SetText("请先输入单词");
	}
}

void DictionaryApp::ShowKeyboard() {
	keyboard_visible_ = true;
	suppress_auto_keyboard_ = false;
	if (keyboard_) {
		auto profile = keyboard_->Profile();
		profile.page = 0;
		keyboard_->SetProfile(profile);
		keyboard_->SetSelectedIndex(0);
		keyboard_->SetVisible(true);
		ui_engine_.RequestFocus(keyboard_->Id());
	}
}

void DictionaryApp::HideKeyboard(bool from_select) {
	keyboard_visible_ = false;
	suppress_auto_keyboard_ = from_select;
	if (keyboard_) {
		keyboard_->SetVisible(false);
	}
	if (textarea_word_) {
		ui_engine_.RequestFocus(textarea_word_->Id());
	}
}

void DictionaryApp::AppendInput(const char *value) {
	if (!textarea_word_ || !value || !value[0]) {
		return;
	}
	std::string text = textarea_word_->Text();
	text += value;
	textarea_word_->SetText(text);
}

void DictionaryApp::DeleteInputChar() {
	if (!textarea_word_) {
		return;
	}
	std::string text = textarea_word_->Text();
	if (text.empty()) {
		return;
	}
	text.pop_back();
	textarea_word_->SetText(text);
}

void DictionaryApp::QueryCurrentWordAndDisplay() {
	if (!textarea_word_) {
		return;
	}

	current_word_ = NormalizeWord(textarea_word_->Text());
	current_word_ = ToLowerAscii(current_word_);
	textarea_word_->SetText(current_word_);
	ESP_LOGI(kTag, "query word normalized='%s'", current_word_.c_str());

	if (current_word_.empty()) {
		current_word_found_ = false;
		SetStatusText(kStatusNotFound);
		UpdateResultFrame(EntryData{});
		return;
	}

	EntryData entry = QueryByWord(current_word_);
	current_word_found_ = entry.found;
	if (!entry.found) {
		SetStatusText(kStatusNotFound);
	} else if (IsWordInBook(current_word_)) {
		SetStatusText(kStatusInBook);
	} else {
		SetStatusText(kStatusNotInBook);
	}
	UpdateResultFrame(entry);
}

DictionaryApp::EntryData DictionaryApp::QueryByWord(const std::string &word) const {
	EntryData out;
	if (!EnsureSqliteRuntimeReady()) {
		ESP_LOGE(kTag, "sqlite runtime init failed, abort query");
		return out;
	}
	(void)EnsureSqliteSdMounted();

	const std::string discovered_path = DiscoverDbPath();
	if (!discovered_path.empty()) {
		ESP_LOGI(kTag, "db final path selected: %s", discovered_path.c_str());
	}

	sqlite3 *db = nullptr;
	std::string opened_path;
	if (!OpenValidatedWordDbReadonly(discovered_path, &db, &opened_path) || !db) {
		ESP_LOGE(kTag, "all db path open/validate failed, abort query word='%s'", word.c_str());
		return out;
	}
	ESP_LOGI(kTag, "db opened readonly path=%s", opened_path.c_str());

	const char *sql =
		"SELECT id, word, phonetic, meaning_zh, meaning_en, tags, forms, example1, example2, example3 "
		"FROM word_dictionary WHERE word = ? LIMIT 1;";

	sqlite3_stmt *stmt = nullptr;
	int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK || !stmt) {
		ESP_LOGE(kTag, "prepare failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
		return out;
	}

	sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_TRANSIENT);
	rc = sqlite3_step(stmt);
	ESP_LOGI(kTag, "step rc=%d word='%s'", rc, word.c_str());
	if (rc == SQLITE_ROW) {
		out.found = true;
		out.fields.reserve(10);
		out.fields.emplace_back("id", std::to_string(sqlite3_column_int(stmt, 0)));
		out.fields.emplace_back("word", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1))));
		out.fields.emplace_back("phonetic", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))));
		out.fields.emplace_back("meaning_zh", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3))));
		out.fields.emplace_back("meaning_en", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4))));
		out.fields.emplace_back("tags", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5))));
		out.fields.emplace_back("forms", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6))));
		out.fields.emplace_back("example1", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7))));
		out.fields.emplace_back("example2", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 8))));
		out.fields.emplace_back("example3", eteacher::database_manager::SanitizeFieldValue(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 9))));
	} else if (rc == SQLITE_DONE) {
		ESP_LOGW(kTag, "word not found in word_dictionary word='%s'", word.c_str());
	} else {
		ESP_LOGE(kTag, "step failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
	}

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return out;
}

void DictionaryApp::UpdateResultFrame(const EntryData &entry) {
	if (!frame_result_) {
		return;
	}
	if (!entry.found) {
		frame_result_->SetText("未查询到结果");
		return;
	}

	std::string text;
	for (const auto &item : entry.fields) {
		text += item.first;
		text += ": ";
		text += item.second;
		text += "\n";
	}
	if (!text.empty()) {
		text.pop_back();
	}
	frame_result_->SetText(text);
}

void DictionaryApp::SetStatusText(const std::string &text) {
	if (label_status_) {
		label_status_->SetText(text);
	}
}

void DictionaryApp::UpdateBottomBarHintByFocus() {
	if (!bottom_bar_) {
		return;
	}

	if (keyboard_visible_) {
		bottom_bar_->SetText("C输入 B退格 D翻页 Start查询 Select关闭");
		return;
	}
	if (textarea_word_ && textarea_word_->Focused()) {
		bottom_bar_->SetText("Start打开键盘 D添加 B删除 Select退出应用");
		return;
	}
	if (button_add_ && button_add_->Focused()) {
		bottom_bar_->SetText("Start/C查看生词状态 D添加 B删除 Select退出应用");
		return;
	}
	bottom_bar_->SetText("左右切换焦点 D添加 B删除 Select退出应用");
}

void DictionaryApp::TryAutoShowKeyboardByFocus() {
	if (textarea_word_ && !textarea_word_->Focused()) {
		suppress_auto_keyboard_ = false;
	}
	if (keyboard_visible_ || suppress_auto_keyboard_) {
		return;
	}
	if (textarea_word_ && textarea_word_->Focused()) {
		ShowKeyboard();
	}
}

bool DictionaryApp::IsWordInBook(const std::string &word) const {
	if (word.empty()) {
		return false;
	}
	if (!EnsureSqliteRuntimeReady()) {
		return false;
	}
	if (!EnsureSqliteSdMounted()) {
		return false;
	}

	const std::string db_path = DiscoverWordBookDbPath();
	if (db_path.empty()) {
		ESP_LOGE(kTag, "word book db not found");
		return false;
	}

	sqlite3 *db = nullptr;
	int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
	if (rc != SQLITE_OK || !db) {
		ESP_LOGE(kTag, "open word book readonly failed rc=%d msg=%s", rc, db ? sqlite3_errmsg(db) : "null");
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}

	std::string attached_path;
	if (!AttachValidatedWordDb(db, DiscoverDbPath(), &attached_path)) {
		ESP_LOGE(kTag, "attach words db failed: no valid words.db");
		sqlite3_close(db);
		return false;
	}
	ESP_LOGI(kTag, "attach words db ok: %s", attached_path.c_str());

	int entry_id = 0;
	if (!eteacher::database_manager::QueryDictionaryEntryIdByWord(db, "dictdb", word, &entry_id, kTag)) {
		sqlite3_close(db);
		return false;
	}

	const char *sql =
		"SELECT 1 FROM vocab_items WHERE user_id = ? AND entry_id = ? "
		"AND (is_deleted = 0 OR is_deleted IS NULL) LIMIT 1;";
	sqlite3_stmt *stmt = nullptr;
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK || !stmt) {
		ESP_LOGE(kTag, "prepare in-book query failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		sqlite3_close(db);
		return false;
	}

	sqlite3_bind_int(stmt, 1, kDefaultUserId);
	sqlite3_bind_int(stmt, 2, entry_id);
	rc = sqlite3_step(stmt);
	const bool exists = (rc == SQLITE_ROW);

	sqlite3_finalize(stmt);
	sqlite3_close(db);
	return exists;
}

bool DictionaryApp::AddWordToBook(const std::string &word) const {
	if (word.empty()) {
		return false;
	}
	if (!EnsureSqliteRuntimeReady()) {
		return false;
	}
	if (!EnsureSqliteSdMounted()) {
		return false;
	}

	const std::string db_path = DiscoverWordBookDbPath();
	if (db_path.empty()) {
		ESP_LOGE(kTag, "add word failed: db not found");
		return false;
	}

	sqlite3 *db = nullptr;
	int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
	if (rc != SQLITE_OK || !db) {
		ESP_LOGE(kTag, "add word open db failed rc=%d msg=%s", rc, db ? sqlite3_errmsg(db) : "null");
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}

	if (!eteacher::database_manager::ConfigureWriteConnection(db, kTag)) {
		sqlite3_close(db);
		return false;
	}

	std::string attached_path;
	if (!AttachValidatedWordDb(db, DiscoverDbPath(), &attached_path)) {
		ESP_LOGE(kTag, "add word failed: no valid words.db");
		sqlite3_close(db);
		return false;
	}
	ESP_LOGI(kTag, "add word attach words db ok: %s", attached_path.c_str());

	int entry_id = 0;
	if (!eteacher::database_manager::QueryDictionaryEntryIdByWord(db, "dictdb", word, &entry_id, kTag)) {
		ESP_LOGE(kTag, "word not found in word_dictionary: %s", word.c_str());
		sqlite3_close(db);
		return false;
	}
	(void)sqlite3_exec(db, "DETACH DATABASE dictdb;", nullptr, nullptr, nullptr);
	if (!eteacher::database_manager::BeginTransaction(db, kTag)) {
		sqlite3_close(db);
		return false;
	}
	bool tx_active = true;
	const sqlite3_int64 now_sec = static_cast<sqlite3_int64>(esp_timer_get_time() / 1000000ULL);

	const char *restore_sql =
		"UPDATE vocab_items "
		"SET is_deleted = ?, added_at = ?, source = ? "
		"WHERE user_id = ? "
		"AND entry_id = ?;";
	sqlite3_stmt *stmt = nullptr;
	rc = sqlite3_prepare_v2(db, restore_sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK || !stmt) {
		ESP_LOGE(kTag, "add word prepare failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		if (tx_active) {
			eteacher::database_manager::RollbackTransaction(db, kTag);
			tx_active = false;
		}
		sqlite3_close(db);
		return false;
	}

	rc = sqlite3_bind_int(stmt, 1, 0);
	if (rc == SQLITE_OK) {
		rc = sqlite3_bind_int64(stmt, 2, now_sec);
	}
	if (rc == SQLITE_OK) {
		rc = sqlite3_bind_text(stmt, 3, "dictionary", -1, SQLITE_STATIC);
	}
	if (rc == SQLITE_OK) {
		rc = sqlite3_bind_int(stmt, 4, kDefaultUserId);
	}
	if (rc == SQLITE_OK) {
		rc = sqlite3_bind_int(stmt, 5, entry_id);
	}
	if (rc != SQLITE_OK) {
		ESP_LOGE(kTag, "add word bind failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		if (tx_active) {
			eteacher::database_manager::RollbackTransaction(db, kTag);
			tx_active = false;
		}
		sqlite3_close(db);
		return false;
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		ESP_LOGE(kTag, "add word restore step failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		if (tx_active) {
			eteacher::database_manager::RollbackTransaction(db, kTag);
			tx_active = false;
		}
		sqlite3_close(db);
		return false;
	}
	const int restored = sqlite3_changes(db);
	sqlite3_finalize(stmt);

	if (restored <= 0) {
		const char *insert_sql =
			"INSERT INTO vocab_items(user_id, entry_id, added_at, source, is_favorite, is_difficult, is_mastered, is_deleted) "
			"VALUES (?, ?, ?, ?, 0, 0, 0, 0);";
		rc = sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr);
		if (rc != SQLITE_OK || !stmt) {
			ESP_LOGE(kTag, "add word insert prepare failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
			if (stmt) {
				sqlite3_finalize(stmt);
			}
			if (tx_active) {
				eteacher::database_manager::RollbackTransaction(db, kTag);
				tx_active = false;
			}
			sqlite3_close(db);
			return false;
		}

		rc = sqlite3_bind_int(stmt, 1, kDefaultUserId);
		if (rc == SQLITE_OK) {
			rc = sqlite3_bind_int(stmt, 2, entry_id);
		}
		if (rc == SQLITE_OK) {
			rc = sqlite3_bind_int64(stmt, 3, now_sec);
		}
		if (rc == SQLITE_OK) {
			rc = sqlite3_bind_text(stmt, 4, "dictionary", -1, SQLITE_STATIC);
		}
		if (rc != SQLITE_OK) {
			ESP_LOGE(kTag, "add word insert bind failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			if (tx_active) {
				eteacher::database_manager::RollbackTransaction(db, kTag);
				tx_active = false;
			}
			sqlite3_close(db);
			return false;
		}

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			ESP_LOGE(kTag, "add word insert step failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			if (tx_active) {
				eteacher::database_manager::RollbackTransaction(db, kTag);
				tx_active = false;
			}
			sqlite3_close(db);
			return false;
		}

		const int inserted = sqlite3_changes(db);
		sqlite3_finalize(stmt);
		if (inserted <= 0) {
			ESP_LOGE(kTag, "insert vocab_items no change word=%s entry_id=%d", word.c_str(), entry_id);
			if (tx_active) {
				eteacher::database_manager::RollbackTransaction(db, kTag);
				tx_active = false;
			}
			sqlite3_close(db);
			return false;
		}
	}
	if (tx_active) {
		if (!eteacher::database_manager::CommitTransaction(db, kTag)) {
			eteacher::database_manager::RollbackTransaction(db, kTag);
			sqlite3_close(db);
			return false;
		}
		tx_active = false;
	}
	sqlite3_close(db);
	ESP_LOGI(kTag, "word added to book: %s", word.c_str());
	return true;
}

bool DictionaryApp::RemoveWordFromBook(const std::string &word) const {
	if (word.empty()) {
		return false;
	}
	if (!EnsureSqliteRuntimeReady()) {
		return false;
	}
	if (!EnsureSqliteSdMounted()) {
		return false;
	}

	const std::string db_path = DiscoverWordBookDbPath();
	if (db_path.empty()) {
		ESP_LOGE(kTag, "remove word failed: db not found");
		return false;
	}

	sqlite3 *db = nullptr;
	int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
	if (rc != SQLITE_OK || !db) {
		ESP_LOGE(kTag, "remove word open db failed rc=%d msg=%s", rc, db ? sqlite3_errmsg(db) : "null");
		if (db) {
			sqlite3_close(db);
		}
		return false;
	}

	if (!eteacher::database_manager::ConfigureWriteConnection(db, kTag)) {
		sqlite3_close(db);
		return false;
	}

	std::string attached_path;
	if (!AttachValidatedWordDb(db, DiscoverDbPath(), &attached_path)) {
		ESP_LOGE(kTag, "remove word failed: no valid words.db");
		sqlite3_close(db);
		return false;
	}
	ESP_LOGI(kTag, "remove word attach words db ok: %s", attached_path.c_str());

	int entry_id = 0;
	if (!eteacher::database_manager::QueryDictionaryEntryIdByWord(db, "dictdb", word, &entry_id, kTag)) {
		sqlite3_close(db);
		return false;
	}
	(void)sqlite3_exec(db, "DETACH DATABASE dictdb;", nullptr, nullptr, nullptr);
	if (!eteacher::database_manager::BeginTransaction(db, kTag)) {
		sqlite3_close(db);
		return false;
	}
	bool tx_active = true;

	const char *sql =
		"UPDATE vocab_items "
		"SET is_deleted = 1 "
		"WHERE user_id = ? "
		"AND entry_id = ? "
		"AND (is_deleted = 0 OR is_deleted IS NULL);";
	sqlite3_stmt *stmt = nullptr;
	rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
	if (rc != SQLITE_OK || !stmt) {
		ESP_LOGE(kTag, "remove word prepare failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		if (stmt) {
			sqlite3_finalize(stmt);
		}
		if (tx_active) {
			eteacher::database_manager::RollbackTransaction(db, kTag);
			tx_active = false;
		}
		sqlite3_close(db);
		return false;
	}

	rc = sqlite3_bind_int(stmt, 1, kDefaultUserId);
	if (rc == SQLITE_OK) {
		rc = sqlite3_bind_int(stmt, 2, entry_id);
	}
	if (rc != SQLITE_OK) {
		ESP_LOGE(kTag, "remove word bind failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		if (tx_active) {
			eteacher::database_manager::RollbackTransaction(db, kTag);
			tx_active = false;
		}
		sqlite3_close(db);
		return false;
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		ESP_LOGE(kTag, "remove word step failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		if (tx_active) {
			eteacher::database_manager::RollbackTransaction(db, kTag);
			tx_active = false;
		}
		sqlite3_close(db);
		return false;
	}

	if (tx_active) {
		if (!eteacher::database_manager::CommitTransaction(db, kTag)) {
			eteacher::database_manager::RollbackTransaction(db, kTag);
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			return false;
		}
		tx_active = false;
	}
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	ESP_LOGI(kTag, "word removed from book: %s", word.c_str());
	return true;
}

void DictionaryApp::OnKeyboardKey(app_ui::SoftKeyboardWidget *widget, const char *value, void *ctx) {
	(void)widget;
	auto *app = static_cast<DictionaryApp *>(ctx);
	if (!app) {
		return;
	}
	app->AppendInput(value);
}

std::unique_ptr<AppBase> MakeDictionaryApp() {
	return std::make_unique<DictionaryApp>();
}
