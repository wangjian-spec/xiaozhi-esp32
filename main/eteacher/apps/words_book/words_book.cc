#include "eteacher/apps/words_book/words_book.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <SD.h>
#include <SPI.h>
#include <esp_log.h>
#include <sqlite3.h>

#include "boards/EnglishTeacher/config.h"
#include "boards/EnglishTeacher/custom_epd_display.h"
#include "display.h"
#include "epd_manager/epd_manager.h"
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
constexpr const char *kTag = "WordsBookApp";
constexpr int kDefaultUserId = 0;
constexpr const char *kWordSeparator = "     ";
constexpr size_t kMaxCharsPerLine = 36;
constexpr size_t kLinesPerPage = 8;
constexpr const char *kTitleFont = "wenquanyi_11pt";
constexpr const char *kTextFont = "wenquanyi_11pt";
constexpr const char *kStatusFont = "wenquanyi_9pt";
constexpr int16_t kPadding = 8;
constexpr int16_t kTitleBaseline = 18;
constexpr int16_t kLineHeight = 16;
constexpr int16_t kListTopBaseline = 44;

bool IsClickLike(const ButtonEvent &event) {
    return event.action == ButtonAction::Click || event.action == ButtonAction::PressDown ||
           event.action == ButtonAction::LongPress;
}

bool EnsureSqliteRuntimeReady() {
    return eteacher::database_manager::EnsureSqliteRuntimeReady(kTag);
}

bool EnsureSqliteSdMounted() {
    return eteacher::database_manager::EnsureSqliteSdMounted(kTag);
}

std::string DiscoverWordBookDbPath() {
    return eteacher::database_manager::DiscoverUserDataDbPath(kTag);
}

std::string DiscoverDictDbPath() {
    return eteacher::database_manager::DiscoverDictionaryDbPath(kTag);
}

bool OpenValidatedWordDbReadonly(const std::string &discovered_path, sqlite3 **out_db, std::string *out_path) {
    return eteacher::database_manager::OpenValidatedDictionaryDbReadonly(discovered_path, out_db, out_path, kTag);
}

bool AttachValidatedWordDb(sqlite3 *db, const std::string &discovered_path, std::string *attached_path) {
    return eteacher::database_manager::AttachValidatedDictionaryDb(db, discovered_path, attached_path, "dictdb", kTag);
}

}

MenuMeta WordsBookApp::GetMenuMeta() const
{
    return MenuMeta{"words_book", "单词本", "方向键选择 Start/C查看 B删除 Select退出"};
}

void WordsBookApp::OnEnter(AppContext &ctx)
{
    selected_word_index_ = 0;
    top_row_ = 0;
    detail_mode_ = false;
    detail_text_.clear();
    detail_word_.clear();
    LoadWordsFromBook();
    BuildRows();
    RenderList(ctx);
}

void WordsBookApp::OnExit(AppContext &ctx)
{
    (void)ctx;
}

void WordsBookApp::OnButton(AppContext &ctx, const ButtonEvent &event)
{
    if (!IsClickLike(event)) {
        return;
    }

    if (detail_mode_) {
        if (event.id == AppButton::Select || event.id == AppButton::B) {
            detail_mode_ = false;
            RenderList(ctx);
            return;
        }
        RenderDetail(ctx);
        return;
    }

    if (words_.empty()) {
        RenderList(ctx);
        return;
    }

    if (event.id == AppButton::Up) {
        MoveSelectionUp();
        RenderList(ctx);
        return;
    }
    if (event.id == AppButton::Down) {
        MoveSelectionDown();
        RenderList(ctx);
        return;
    }
    if (event.id == AppButton::Left) {
        MoveSelectionLeft();
        RenderList(ctx);
        return;
    }
    if (event.id == AppButton::Right) {
        MoveSelectionRight();
        RenderList(ctx);
        return;
    }
    if (event.id == AppButton::Start || event.id == AppButton::C) {
        ShowSelectedWordDetail();
        RenderDetail(ctx);
        return;
    }
    if (event.id == AppButton::B) {
        if (selected_word_index_ < words_.size()) {
            const std::string remove_word = words_[selected_word_index_];
            if (RemoveWordFromBook(remove_word)) {
                const size_t keep_index = selected_word_index_;
                LoadWordsFromBook();
                BuildRows();
                if (words_.empty()) {
                    selected_word_index_ = 0;
                    top_row_ = 0;
                } else {
                    selected_word_index_ = std::min(keep_index, words_.size() - 1);
                    EnsureSelectionVisible();
                }
            }
        }
        RenderList(ctx);
        return;
    }
}

bool WordsBookApp::ShouldInterceptSelectExit() const {
    return detail_mode_;
}

void WordsBookApp::LoadWordsFromBook() {
    words_.clear();

    if (!EnsureSqliteRuntimeReady()) {
        ESP_LOGE(kTag, "load words failed: sqlite runtime init error");
        return;
    }
    if (!EnsureSqliteSdMounted()) {
        ESP_LOGE(kTag, "load words failed: sd mount error");
        return;
    }

    const std::string db_path = DiscoverWordBookDbPath();
    if (db_path.empty()) {
        ESP_LOGE(kTag, "load words failed: db not found");
        return;
    }

    sqlite3 *db = nullptr;
    int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || !db) {
        ESP_LOGE(kTag, "open db failed rc=%d msg=%s", rc, db ? sqlite3_errmsg(db) : "null");
        if (db) {
            sqlite3_close(db);
        }
        return;
    }

    const std::string discovered_dict_path = DiscoverDictDbPath();
    std::string attached_path;
    if (!AttachValidatedWordDb(db, discovered_dict_path, &attached_path)) {
        ESP_LOGE(kTag, "attach dict db failed: no valid words.db");
        sqlite3_close(db);
        return;
    }
    ESP_LOGI(kTag, "attach dict db ok: %s", attached_path.c_str());

    const char *sql =
        "SELECT wd.word "
        "FROM vocab_items vi "
        "JOIN dictdb.word_dictionary wd ON wd.id = vi.entry_id "
        "WHERE vi.user_id = ? AND IFNULL(vi.is_deleted, 0) = 0 "
        "AND wd.word IS NOT NULL AND TRIM(wd.word) != '' "
        "ORDER BY vi.id ASC;";
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        ESP_LOGE(kTag, "prepare words query failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return;
    }

    sqlite3_bind_int(stmt, 1, kDefaultUserId);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (value && value[0]) {
            words_.emplace_back(value);
        }
    }

    if (rc != SQLITE_DONE) {
        ESP_LOGE(kTag, "step words query failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    ESP_LOGI(kTag, "loaded words count=%d", (int)words_.size());
}

void WordsBookApp::BuildRows() {
    rows_.clear();
    word_pos_.assign(words_.size(), {-1, -1});

    if (words_.empty()) {
        return;
    }

    std::vector<size_t> current_row;
    size_t current_len = 0;
    const size_t separator_len = std::char_traits<char>::length(kWordSeparator);

    for (size_t index = 0; index < words_.size(); ++index) {
        const std::string &token = words_[index];
        if (token.empty()) {
            continue;
        }

        const size_t needed_len = current_row.empty() ? token.size() : (current_len + separator_len + token.size());
        if (!current_row.empty() && needed_len > kMaxCharsPerLine) {
            rows_.push_back(current_row);
            current_row.clear();
            current_len = 0;
        }

        if (!current_row.empty()) {
            current_len += separator_len;
        }
        current_row.push_back(index);
        current_len += token.size();
    }

    if (!current_row.empty()) {
        rows_.push_back(current_row);
    }

    for (size_t row_index = 0; row_index < rows_.size(); ++row_index) {
        for (size_t col_index = 0; col_index < rows_[row_index].size(); ++col_index) {
            const size_t word_index = rows_[row_index][col_index];
            word_pos_[word_index] = {static_cast<int>(row_index), static_cast<int>(col_index)};
        }
    }
}


void WordsBookApp::MoveSelectionUp() {
    if (selected_word_index_ >= word_pos_.size()) {
        return;
    }
    const auto pos = word_pos_[selected_word_index_];
    if (pos.first <= 0) {
        return;
    }
    const int target_row = pos.first - 1;
    const int target_col = std::min(pos.second, static_cast<int>(rows_[target_row].size()) - 1);
    selected_word_index_ = rows_[target_row][target_col];
    EnsureSelectionVisible();
}

void WordsBookApp::MoveSelectionDown() {
    if (selected_word_index_ >= word_pos_.size()) {
        return;
    }
    const auto pos = word_pos_[selected_word_index_];
    if (pos.first < 0 || static_cast<size_t>(pos.first + 1) >= rows_.size()) {
        return;
    }
    const int target_row = pos.first + 1;
    const int target_col = std::min(pos.second, static_cast<int>(rows_[target_row].size()) - 1);
    selected_word_index_ = rows_[target_row][target_col];
    EnsureSelectionVisible();
}

void WordsBookApp::MoveSelectionLeft() {
    if (selected_word_index_ >= word_pos_.size()) {
        return;
    }
    const auto pos = word_pos_[selected_word_index_];
    if (pos.first < 0 || pos.second < 0) {
        return;
    }
    if (pos.second > 0) {
        selected_word_index_ = rows_[pos.first][pos.second - 1];
    } else if (pos.first > 0) {
        selected_word_index_ = rows_[pos.first - 1].back();
    }
    EnsureSelectionVisible();
}

void WordsBookApp::MoveSelectionRight() {
    if (selected_word_index_ >= word_pos_.size()) {
        return;
    }
    const auto pos = word_pos_[selected_word_index_];
    if (pos.first < 0 || pos.second < 0) {
        return;
    }
    if (static_cast<size_t>(pos.second + 1) < rows_[pos.first].size()) {
        selected_word_index_ = rows_[pos.first][pos.second + 1];
    } else if (static_cast<size_t>(pos.first + 1) < rows_.size()) {
        selected_word_index_ = rows_[pos.first + 1].front();
    }
    EnsureSelectionVisible();
}

void WordsBookApp::EnsureSelectionVisible() {
    if (selected_word_index_ >= word_pos_.size()) {
        return;
    }
    const auto pos = word_pos_[selected_word_index_];
    if (pos.first < 0) {
        return;
    }
    const size_t row = static_cast<size_t>(pos.first);
    if (row < top_row_) {
        top_row_ = row;
    } else if (row >= top_row_ + kLinesPerPage) {
        top_row_ = row - kLinesPerPage + 1;
    }
}

void WordsBookApp::ShowSelectedWordDetail() {
    if (selected_word_index_ >= words_.size()) {
        return;
    }
    detail_word_ = words_[selected_word_index_];
    const EntryData entry = QueryByWord(detail_word_);
    detail_text_ = FormatEntryText(entry);
    detail_mode_ = true;
}

WordsBookApp::EntryData WordsBookApp::QueryByWord(const std::string &word) const {
    EntryData out;
    if (word.empty()) {
        return out;
    }
    if (!EnsureSqliteRuntimeReady()) {
        ESP_LOGE(kTag, "sqlite runtime init failed, abort query");
        return out;
    }
    (void)EnsureSqliteSdMounted();

    const std::string discovered_path = DiscoverDictDbPath();
    if (!discovered_path.empty()) {
        ESP_LOGI(kTag, "dict db final path selected: %s", discovered_path.c_str());
    }

    sqlite3 *db = nullptr;
    std::string opened_path;
    if (!OpenValidatedWordDbReadonly(discovered_path, &db, &opened_path) || !db) {
        ESP_LOGE(kTag, "all dict db path open/validate failed, abort query word='%s'", word.c_str());
        return out;
    }
    ESP_LOGI(kTag, "dict db opened readonly path=%s", opened_path.c_str());

    const char *sql =
        "SELECT id, word, phonetic, meaning_zh, meaning_en, tags, forms, example1, example2, example3 "
        "FROM word_dictionary WHERE word = ? LIMIT 1;";

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return out;
    }

    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
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
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return out;
}

bool WordsBookApp::RemoveWordFromBook(const std::string &word) const {
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
        ESP_LOGE(kTag, "remove failed: wordbook db not found");
        return false;
    }

    sqlite3 *db = nullptr;
    int rc = sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr);
    if (rc != SQLITE_OK || !db) {
        ESP_LOGE(kTag, "remove failed: open db rc=%d msg=%s", rc, db ? sqlite3_errmsg(db) : "null");
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }
    if (!eteacher::database_manager::ConfigureWriteConnection(db, kTag)) {
        sqlite3_close(db);
        return false;
    }

    const std::string discovered_dict_path = DiscoverDictDbPath();
    std::string attached_path;
    if (!AttachValidatedWordDb(db, discovered_dict_path, &attached_path)) {
        ESP_LOGE(kTag, "remove failed: attach dict db failed, no valid words.db");
        sqlite3_close(db);
        return false;
    }
    ESP_LOGI(kTag, "remove attach dict db ok: %s", attached_path.c_str());

    int entry_id = 0;
    if (!eteacher::database_manager::QueryDictionaryEntryIdByWord(db, "dictdb", word, &entry_id, kTag)) {
        ESP_LOGW(kTag, "remove failed: word not found in dict db word=%s", word.c_str());
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
        "AND IFNULL(is_deleted, 0) = 0;";
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        ESP_LOGE(kTag, "remove failed: prepare rc=%d msg=%s", rc, sqlite3_errmsg(db));
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

    sqlite3_bind_int(stmt, 1, kDefaultUserId);
    sqlite3_bind_int(stmt, 2, entry_id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        ESP_LOGE(kTag, "remove failed: step rc=%d msg=%s", rc, sqlite3_errmsg(db));
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

    const int changes = sqlite3_changes(db);
    ESP_LOGI(kTag, "remove word='%s' changes=%d", word.c_str(), changes);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return true;
}

std::string WordsBookApp::FormatEntryText(const EntryData &entry) const {
    if (!entry.found) {
        return "未查询到结果";
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
    return text;
}

void WordsBookApp::RenderList(AppContext &ctx) const {
    auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
    if (!epd) {
        ctx.board.GetDisplay()->SetChatMessage("system", "WordsBook: EPD unavailable");
        return;
    }

    struct DrawCtx {
        CustomEpdDisplay *epd;
        std::vector<std::string> words;
        std::vector<std::vector<size_t>> rows;
        size_t top_row;
        size_t selected;
    };

    auto *draw_ctx = new DrawCtx{epd, words_, rows_, top_row_, selected_word_index_};
    auto cb = [](Adafruit_GFX &gfx, void *ctx) {
        auto *d = static_cast<DrawCtx *>(ctx);
        if (!d || !d->epd) {
            return;
        }

        gfx.fillScreen(GxEPD_WHITE);
        d->epd->DrawUtf8(kPadding, kTitleBaseline, "生词表", kTitleFont, GxEPD_BLACK);
        d->epd->DrawUtf8(kPadding, d->epd->height() - 20, "Select退出", kStatusFont, GxEPD_BLACK);
        d->epd->DrawUtf8(kPadding, d->epd->height() - 6, "方向键选择 Start/C查看 B删除", kStatusFont, GxEPD_BLACK);

        if (d->words.empty() || d->rows.empty()) {
            d->epd->DrawUtf8(kPadding, kListTopBaseline, "生词表为空", kTextFont, GxEPD_BLACK);
            return;
        }

        const int16_t sep_width = d->epd->MeasureUtf8Width(kWordSeparator, kTextFont);
        const size_t row_end = std::min(d->rows.size(), d->top_row + kLinesPerPage);
        for (size_t row = d->top_row; row < row_end; ++row) {
            int16_t x = kPadding;
            const int16_t baseline = static_cast<int16_t>(kListTopBaseline + (row - d->top_row) * kLineHeight);
            for (size_t col = 0; col < d->rows[row].size(); ++col) {
                const size_t idx = d->rows[row][col];
                if (idx >= d->words.size()) {
                    continue;
                }
                const std::string &word = d->words[idx];
                const int16_t word_width = std::max<int16_t>(d->epd->MeasureUtf8Width(word, kTextFont), 8);
                if (idx == d->selected) {
                    gfx.drawRect(x - 2, baseline - 13, word_width + 4, 15, GxEPD_BLACK);
                }
                d->epd->DrawUtf8(x, baseline, word, kTextFont, GxEPD_BLACK);
                x = static_cast<int16_t>(x + word_width + sep_width);
            }
        }

    };

    EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial, cb, draw_ctx, [](void *ctx) {
        delete static_cast<DrawCtx *>(ctx);
    }, EpdManager::Rect(0, 0, epd->width(), epd->height()));
}

void WordsBookApp::RenderDetail(AppContext &ctx) const {
    auto *epd = dynamic_cast<CustomEpdDisplay *>(ctx.board.GetDisplay());
    if (!epd) {
        ctx.board.GetDisplay()->SetChatMessage("system", "WordsBook: EPD unavailable");
        return;
    }

    struct DrawCtx {
        CustomEpdDisplay *epd;
        std::string word;
        std::string detail;
    };

    auto *draw_ctx = new DrawCtx{epd, detail_word_, detail_text_};
    auto cb = [](Adafruit_GFX &gfx, void *ctx) {
        auto *d = static_cast<DrawCtx *>(ctx);
        if (!d || !d->epd) {
            return;
        }

        gfx.fillScreen(GxEPD_WHITE);
        std::string header = "单词详情: ";
        header += d->word;
        d->epd->DrawUtf8(kPadding, kTitleBaseline, header, kTitleFont, GxEPD_BLACK);

        int16_t baseline = kTitleBaseline + kLineHeight;
        size_t start = 0;
        while (start <= d->detail.size() && baseline < d->epd->height() - 16) {
            const size_t pos = d->detail.find('\n', start);
            const size_t len = (pos == std::string::npos) ? (d->detail.size() - start) : (pos - start);
            const std::string line = d->detail.substr(start, len);
            d->epd->DrawUtf8(kPadding, baseline, line, kStatusFont, GxEPD_BLACK);
            baseline = static_cast<int16_t>(baseline + 12);
            if (pos == std::string::npos) {
                break;
            }
            start = pos + 1;
        }

        d->epd->DrawUtf8(kPadding, d->epd->height() - 6, "Select/B返回列表", kStatusFont, GxEPD_BLACK);
    };

    EpdManager::GetInstance().Schedule(EpdManager::TaskType::kPartial, cb, draw_ctx, [](void *ctx) {
        delete static_cast<DrawCtx *>(ctx);
    }, EpdManager::Rect(0, 0, epd->width(), epd->height()));
}

std::unique_ptr<AppBase> MakeWordsBookApp()
{
    return std::make_unique<WordsBookApp>();
}
