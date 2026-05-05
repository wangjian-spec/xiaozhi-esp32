#include "eteacher/database_manager/sqlite_db_api.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>
#include <sys/stat.h>

#include <SD.h>
#include <SPI.h>
#include <esp_log.h>

#include "boards/EnglishTeacher/config.h"
#include "eteacher/database_manager/database_debug.h"

namespace {
constexpr const char *kDefaultDictionaryDbPath = "/sdcard/resource/database/stage_1.db";
constexpr const char *kStageDbDir = "/sdcard/resource/database/";
constexpr const char *kQuestionDbPathPrimary = "/sdcard/resource/database/question.db";
constexpr const char *kUserDbPathPrimary = "/sdcard/user/user.db";

std::string BuildStageDictionaryDbPath(int stage_index) {
    const int normalized_stage = std::max(1, stage_index);
    return std::string(kStageDbDir) + "stage_" + std::to_string(normalized_stage) + ".db";
}

const char *SafeTag(const char *log_tag) {
    return (log_tag && log_tag[0]) ? log_tag : "DbApi";
}

bool HasSqliteMagicHeader(const char *path);
bool ValidateSqliteFileLayout(const char *path);

bool FileExists(const char *path) {
    if (!path || !path[0]) {
        return false;
    }
    struct stat st;
    return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool ValidateUserDataSchema(sqlite3 *db, const char *required_table, const char *scope, const char *log_tag) {
    if (!db) {
        return false;
    }

    if (!required_table || !required_table[0]) {
        return true;
    }

    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        DB_LOGW(SafeTag(log_tag),
                 "%s validate user schema failed rc=%d xrc=%d msg=%s",
                 scope ? scope : "userdb",
                 rc,
                 sqlite3_extended_errcode(db),
                 sqlite3_errmsg(db));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    rc = sqlite3_bind_text(stmt, 1, required_table, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        DB_LOGW(SafeTag(log_tag),
                 "%s validate user schema bind failed rc=%d msg=%s",
                 scope ? scope : "userdb",
                 rc,
                 sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }

    const int step_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step_rc == SQLITE_ROW) {
        return true;
    }

    DB_LOGW(SafeTag(log_tag), "%s missing table %s (rc=%d)", scope ? scope : "userdb", required_table, step_rc);
    return false;
}

bool IsSqliteCorruptionCode(int rc) {
    return rc == SQLITE_CORRUPT || rc == SQLITE_NOTADB;
}

bool MessageIndicatesCorruption(const char *message) {
    return message != nullptr &&
           (std::strstr(message, "malformed") != nullptr || std::strstr(message, "corrupt") != nullptr ||
            std::strstr(message, "not a database") != nullptr);
}

bool ValidateSqliteIntegrity(sqlite3 *db, const char *scope, const char *log_tag, bool *isCorrupt) {
    if (isCorrupt != nullptr) {
        *isCorrupt = false;
    }
    if (db == nullptr) {
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK || stmt == nullptr) {
        DB_LOGW(SafeTag(log_tag),
                "%s integrity check prepare failed rc=%d xrc=%d msg=%s",
                scope ? scope : "userdb",
                rc,
                sqlite3_extended_errcode(db),
                sqlite3_errmsg(db));
        if (isCorrupt != nullptr && (IsSqliteCorruptionCode(rc) || MessageIndicatesCorruption(sqlite3_errmsg(db)))) {
            *isCorrupt = true;
        }
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    const int stepRc = sqlite3_step(stmt);
    const unsigned char *resultText = stepRc == SQLITE_ROW ? sqlite3_column_text(stmt, 0) : nullptr;
    const std::string result = resultText != nullptr ? reinterpret_cast<const char *>(resultText) : std::string();
    sqlite3_finalize(stmt);

    if (stepRc == SQLITE_ROW && result == "ok") {
        return true;
    }

    if (stepRc == SQLITE_DONE && result.empty()) {
        static bool logged_no_rows_as_readable = false;
        if (!logged_no_rows_as_readable) {
            DB_LOGI(SafeTag(log_tag),
                    "%s integrity check returned no rows step_rc=%d msg=%s; treating db as readable",
                    scope ? scope : "userdb",
                    stepRc,
                    sqlite3_errmsg(db));
            logged_no_rows_as_readable = true;
        }
        return true;
    }

    DB_LOGW(SafeTag(log_tag),
            "%s integrity check failed step_rc=%d result=%s msg=%s",
            scope ? scope : "userdb",
            stepRc,
            result.empty() ? "(empty)" : result.c_str(),
            sqlite3_errmsg(db));
    if (isCorrupt != nullptr) {
        *isCorrupt = true;
    }
    return false;
}

bool QuarantineCorruptUserDataDb(const char *path, const char *log_tag) {
    if (!path || !path[0] || !FileExists(path)) {
        return false;
    }

    const std::string backupPath = std::string(path) + ".corrupt";
    (void)std::remove(backupPath.c_str());
    if (std::rename(path, backupPath.c_str()) != 0) {
        const int rename_errno = errno;
        DB_LOGW(SafeTag(log_tag),
                "failed to quarantine corrupt user db path=%s backup=%s errno=%d",
                path,
                backupPath.c_str(),
                rename_errno);
        if (std::remove(path) == 0) {
            DB_LOGW(SafeTag(log_tag),
                    "deleted corrupt user db after quarantine failure path=%s errno=%d",
                    path,
                    rename_errno);
            return true;
        }
        DB_LOGW(SafeTag(log_tag), "failed to delete corrupt user db path=%s errno=%d", path, errno);
        return false;
    }

    DB_LOGW(SafeTag(log_tag), "quarantined corrupt user db path=%s backup=%s", path, backupPath.c_str());
    return true;
}

bool ValidateUserDataDbFile(const char *path, const char *required_table, const char *log_tag, bool *isCorrupt) {
    if (isCorrupt != nullptr) {
        *isCorrupt = false;
    }
    if (!path || !path[0]) {
        return false;
    }
    if (!FileExists(path)) {
        return false;
    }
    if (!HasSqliteMagicHeader(path) || !ValidateSqliteFileLayout(path)) {
        DB_LOGW(SafeTag(log_tag), "skip invalid sqlite user db file: %s", path);
        if (isCorrupt != nullptr) {
            *isCorrupt = true;
        }
        return false;
    }

    sqlite3 *db = nullptr;
    const int rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || !db) {
        DB_LOGW(SafeTag(log_tag), "open user db failed path=%s rc=%d msg=%s", path, rc, db ? sqlite3_errmsg(db) : "null");
        if (isCorrupt != nullptr && (IsSqliteCorruptionCode(rc) || MessageIndicatesCorruption(db ? sqlite3_errmsg(db) : nullptr))) {
            *isCorrupt = true;
        }
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }

    if (!ValidateSqliteIntegrity(db, path, log_tag, isCorrupt)) {
        sqlite3_close(db);
        return false;
    }

    const bool ok = ValidateUserDataSchema(db, required_table, path, log_tag);
    sqlite3_close(db);
    return ok;
}

bool HasSqliteMagicHeader(const char *path) {
    if (!path || !path[0]) {
        return false;
    }
    FILE *fp = ::fopen(path, "rb");
    if (!fp) {
        return false;
    }
    unsigned char buf[16] = {0};
    const size_t n = ::fread(buf, 1, sizeof(buf), fp);
    ::fclose(fp);
    if (n < sizeof(buf)) {
        return false;
    }
    static const unsigned char kMagic[16] = {
        'S', 'Q', 'L', 'i', 't', 'e', ' ', 'f', 'o', 'r', 'm', 'a', 't', ' ', '3', 0,
    };
    return std::memcmp(buf, kMagic, sizeof(kMagic)) == 0;
}

bool ValidateSqliteFileLayout(const char *path) {
    if (!path || !path[0]) {
        return false;
    }

    FILE *fp = ::fopen(path, "rb");
    if (!fp) {
        return false;
    }

    unsigned char header[100] = {0};
    const size_t n = ::fread(header, 1, sizeof(header), fp);
    ::fclose(fp);
    if (n < sizeof(header)) {
        return false;
    }

    const int page_size_raw = (static_cast<int>(header[16]) << 8) | static_cast<int>(header[17]);
    const int page_size = (page_size_raw == 1) ? 65536 : page_size_raw;
    if (page_size < 512 || page_size > 65536 || (page_size & (page_size - 1)) != 0) {
        return false;
    }

    struct stat st {};
    if (::stat(path, &st) != 0) {
        return false;
    }

    const long file_size = static_cast<long>(st.st_size);
    if (file_size < page_size) {
        return false;
    }
    return (file_size % page_size) == 0;
}

bool ValidateWordDictionarySchema(sqlite3 *db, const char *scope, const char *log_tag) {
    if (!db) {
        return false;
    }
    sqlite3_stmt *stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, "SELECT 1 FROM word_dictionary LIMIT 1;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        DB_LOGW(SafeTag(log_tag),
                 "%s validate failed rc=%d xrc=%d msg=%s",
                 scope ? scope : "db",
                 rc,
                 sqlite3_extended_errcode(db),
                 sqlite3_errmsg(db));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    const int step_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step_rc == SQLITE_ROW || step_rc == SQLITE_DONE) {
        return true;
    }
    DB_LOGW(SafeTag(log_tag), "%s validate step failed rc=%d msg=%s", scope ? scope : "db", step_rc, sqlite3_errmsg(db));
    return false;
}

std::string EscapeSqlLiteral(const std::string &text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        out.push_back(c);
        if (c == '\'') {
            out.push_back('\'');
        }
    }
    return out;
}

}  // namespace

namespace eteacher::database_manager {

const char *GetDictionaryDbFixedPath() {
    return kDefaultDictionaryDbPath;
}

const char *GetQuestionDbFixedPath() {
    return kQuestionDbPathPrimary;
}

const char *GetUserDataDbFixedPath() {
    return kUserDbPathPrimary;
}

bool EnsureSqliteRuntimeReady(const char *log_tag) {
    static bool initialized = false;
    if (initialized) {
        return true;
    }

    const int rc = sqlite3_initialize();
    if (rc != SQLITE_OK) {
        DB_LOGE(SafeTag(log_tag), "sqlite3_initialize failed rc=%d", rc);
        return false;
    }

    initialized = true;
    DB_LOGI(SafeTag(log_tag), "sqlite runtime ready, version=%s", sqlite3_libversion());
    return true;
}

bool EnsureSqliteSdMounted(const char *log_tag) {
    static bool mounted = false;
    static bool attempted = false;
    if (mounted) {
        return true;
    }
    if (attempted) {
        return false;
    }
    attempted = true;

    if (SD.begin((int)SD_PIN_NUM_CS, SPI, 20000000, "/sdcard")) {
        mounted = true;
        DB_LOGI(SafeTag(log_tag), "sqlite sd vfs mount OK: /sdcard");
        return true;
    }
    DB_LOGW(SafeTag(log_tag), "sqlite sd vfs mount failed: /sdcard");
    return false;
}

std::string DiscoverDictionaryDbPath(const char *log_tag) {
    const std::string stage1_path = BuildStageDictionaryDbPath(1);
    if (FileExists(stage1_path.c_str())) {
        DB_LOGI(SafeTag(log_tag), "dict db discovered by stage path: %s", stage1_path.c_str());
        DB_LOGI(SafeTag(log_tag), "RESOURCE_OK kind=db scope=dictionary action=discover path=%s method=FileExists(stage)", stage1_path.c_str());
        return stage1_path;
    }
    DB_LOGW(SafeTag(log_tag), "dict db not found at default stage path: %s", stage1_path.c_str());
    return {};
}

std::string DiscoverDictionaryDbPath(const char *log_tag, int stage_index) {
    const std::string stage_path = BuildStageDictionaryDbPath(stage_index);
    if (FileExists(stage_path.c_str())) {
        DB_LOGI(SafeTag(log_tag), "dict db discovered by stage path: %s", stage_path.c_str());
        DB_LOGI(SafeTag(log_tag), "RESOURCE_OK kind=db scope=dictionary action=discover path=%s method=FileExists(stage)", stage_path.c_str());
        return stage_path;
    }
    DB_LOGW(SafeTag(log_tag), "dict db not found for stage=%d path=%s", std::max(1, stage_index), stage_path.c_str());
    return {};
}

std::string DiscoverQuestionDbPath(const char *log_tag) {
    if (FileExists(kQuestionDbPathPrimary)) {
        DB_LOGI(SafeTag(log_tag), "question db discovered by fixed path: %s", kQuestionDbPathPrimary);
        DB_LOGI(SafeTag(log_tag),
                "RESOURCE_OK kind=db scope=question action=discover path=%s method=FileExists(fixed)",
                kQuestionDbPathPrimary);
        return std::string(kQuestionDbPathPrimary);
    }
    return {};
}

std::string DiscoverUserDataDbPath(const char *log_tag, const char *required_table) {
    bool isCorrupt = false;
    if (ValidateUserDataDbFile(kUserDbPathPrimary, required_table, log_tag, &isCorrupt)) {
        DB_LOGI(SafeTag(log_tag), "user db discovered by fixed path: %s", kUserDbPathPrimary);
        DB_LOGI(SafeTag(log_tag), "RESOURCE_OK kind=db scope=user action=discover path=%s method=ValidateUserDataDbFile(fixed)", kUserDbPathPrimary);
        return std::string(kUserDbPathPrimary);
    }
    if (isCorrupt && QuarantineCorruptUserDataDb(kUserDbPathPrimary, log_tag)) {
        DB_LOGW(SafeTag(log_tag), "user db will be recreated after corruption quarantine path=%s", kUserDbPathPrimary);
        return std::string(kUserDbPathPrimary);
    }
    if (!FileExists(kUserDbPathPrimary)) {
        DB_LOGI(SafeTag(log_tag), "user db target path reserved for create: %s", kUserDbPathPrimary);
        DB_LOGI(SafeTag(log_tag), "RESOURCE_OK kind=db scope=user action=discover path=%s method=FileExists(missing-create-target)", kUserDbPathPrimary);
        return std::string(kUserDbPathPrimary);
    }
    DB_LOGW(SafeTag(log_tag),
            "discover user db failed path=%s exists=%d corrupt=%d required_table=%s",
            kUserDbPathPrimary,
            FileExists(kUserDbPathPrimary) ? 1 : 0,
            isCorrupt ? 1 : 0,
            required_table != nullptr ? required_table : "(null)");
    return {};
}

bool ConfigureWriteConnection(sqlite3 *db, const char *log_tag) {
    if (!db) {
        return false;
    }

    const char *sqls[] = {
        "PRAGMA journal_mode=WAL;",
        "PRAGMA synchronous=NORMAL;",
        "PRAGMA temp_store=MEMORY;",
        "PRAGMA busy_timeout=3000;",
    };

    for (const char *sql : sqls) {
        char *errmsg = nullptr;
        const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
        if (errmsg) {
            sqlite3_free(errmsg);
            errmsg = nullptr;
        }
        if (rc != SQLITE_OK) {
            DB_LOGW(SafeTag(log_tag), "apply write pragma failed rc=%d sql=%s", rc, sql);
            return false;
        }
    }
    return true;
}

bool BeginTransaction(sqlite3 *db, const char *log_tag) {
    if (!db) {
        return false;
    }
    const int rc = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        DB_LOGE(SafeTag(log_tag), "begin transaction failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
        return false;
    }
    return true;
}

bool CommitTransaction(sqlite3 *db, const char *log_tag) {
    if (!db) {
        return false;
    }
    const int rc = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        DB_LOGE(SafeTag(log_tag), "commit transaction failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
        return false;
    }
    return true;
}

void RollbackTransaction(sqlite3 *db, const char *log_tag) {
    if (!db) {
        return;
    }
    if (sqlite3_get_autocommit(db) != 0) {
        return;
    }
    const int rc = sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        DB_LOGW(SafeTag(log_tag), "rollback transaction failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
    }
}

bool EnsureTasksTable(sqlite3 *db, const char *log_tag) {
    if (!db) {
        return false;
    }

    const char *sqls[] = {
        "CREATE TABLE IF NOT EXISTS tasks ("
        "id INTEGER PRIMARY KEY,"
        "user_id INTEGER,"
        "task_type TEXT,"
        "target_id INTEGER,"
        "title TEXT,"
        "description TEXT,"
        "start_at INTEGER,"
        "due_at INTEGER,"
        "is_completed INTEGER DEFAULT 0,"
        "is_deleted INTEGER DEFAULT 0,"
        "created_at INTEGER"
        ");",
        "CREATE INDEX IF NOT EXISTS idx_tasks_user_deleted_due ON tasks(user_id, is_deleted, due_at, start_at);",
        "CREATE INDEX IF NOT EXISTS idx_tasks_deleted_done ON tasks(is_deleted, is_completed);",
    };

    for (const char *sql : sqls) {
        const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            DB_LOGE(SafeTag(log_tag), "ensure tasks table failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
            return false;
        }
    }
    return true;
}

bool ConfigureReadonlyConnection(sqlite3 *db, const char *log_tag) {
    if (!db) {
        return false;
    }

    const char *sqls[] = {
        "PRAGMA temp_store=MEMORY;",
        "PRAGMA cache_size=-4096;",
        "PRAGMA busy_timeout=3000;",
    };

    for (const char *sql : sqls) {
        const int rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_OK) {
            DB_LOGW(SafeTag(log_tag), "apply readonly pragma failed rc=%d sql=%s msg=%s", rc, sql, sqlite3_errmsg(db));
            return false;
        }
    }
    return true;
}

bool OpenReadonlyDbFile(const std::string &discovered_path,
                        sqlite3 **out_db,
                        std::string *out_path,
                        const char *log_tag,
                        const char *scope) {
    if (!out_db || !out_path) {
        return false;
    }
    *out_db = nullptr;
    out_path->clear();

    if (discovered_path.empty()) {
        DB_LOGW(SafeTag(log_tag), "open readonly db skipped: discovered path is empty scope=%s", scope && scope[0] ? scope : "db");
        return false;
    }
    const std::string path = discovered_path;
    if (!FileExists(path.c_str())) {
        return false;
    }
    if (!HasSqliteMagicHeader(path.c_str()) || !ValidateSqliteFileLayout(path.c_str())) {
        DB_LOGW(SafeTag(log_tag), "skip invalid sqlite db file: %s", path.c_str());
        return false;
    }

    sqlite3 *db = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || !db) {
        DB_LOGW(SafeTag(log_tag), "open readonly db failed path=%s rc=%d msg=%s", path.c_str(), rc, db ? sqlite3_errmsg(db) : "null");
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }
    if (!ConfigureReadonlyConnection(db, log_tag)) {
        sqlite3_close(db);
        return false;
    }

    *out_db = db;
    *out_path = path;
    DB_LOGI(SafeTag(log_tag),
            "RESOURCE_OK kind=db scope=%s action=open path=%s method=sqlite3_open_v2(READONLY)",
            scope && scope[0] ? scope : "db",
            path.c_str());
    return true;
}

bool OpenValidatedDictionaryDbReadonly(const std::string &discovered_path,
                                       sqlite3 **out_db,
                                       std::string *out_path,
                                       const char *log_tag) {
    if (!out_db || !out_path) {
        return false;
    }
    *out_db = nullptr;
    out_path->clear();

    if (discovered_path.empty()) {
        DB_LOGW(SafeTag(log_tag), "open validated dictionary db skipped: discovered path is empty");
        return false;
    }
    const std::string path = discovered_path;
    if (!FileExists(path.c_str())) {
        return false;
    }
    if (!HasSqliteMagicHeader(path.c_str()) || !ValidateSqliteFileLayout(path.c_str())) {
        DB_LOGW(SafeTag(log_tag), "skip invalid sqlite dict db file: %s", path.c_str());
        return false;
    }

    sqlite3 *db = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || !db) {
        DB_LOGW(SafeTag(log_tag), "open dict db failed path=%s rc=%d msg=%s", path.c_str(), rc, db ? sqlite3_errmsg(db) : "null");
        if (db) {
            sqlite3_close(db);
        }
        return false;
    }
    if (!ConfigureReadonlyConnection(db, log_tag)) {
        sqlite3_close(db);
        return false;
    }

    if (!ValidateWordDictionarySchema(db, path.c_str(), log_tag)) {
        sqlite3_close(db);
        return false;
    }

    *out_db = db;
    *out_path = path;
    DB_LOGI(SafeTag(log_tag), "RESOURCE_OK kind=db scope=dictionary action=open path=%s method=sqlite3_open_v2(READONLY)", path.c_str());
    return true;
}

bool AttachValidatedDictionaryDb(sqlite3 *db,
                                 const std::string &discovered_path,
                                 std::string *attached_path,
                                 const char *alias,
                                 const char *log_tag) {
    if (!db || !attached_path || !alias || !alias[0]) {
        return false;
    }
    attached_path->clear();

    const std::string alias_text(alias);
    const std::string validate_sql = "SELECT 1 FROM " + alias_text + ".word_dictionary LIMIT 1;";

    if (discovered_path.empty()) {
        DB_LOGW(SafeTag(log_tag), "attach validated dictionary db skipped: discovered path is empty alias=%s", alias);
        return false;
    }
    const std::string path = discovered_path;
    if (!FileExists(path.c_str())) {
        return false;
    }
    if (!HasSqliteMagicHeader(path.c_str()) || !ValidateSqliteFileLayout(path.c_str())) {
        DB_LOGW(SafeTag(log_tag), "skip invalid sqlite attach db file: %s", path.c_str());
        return false;
    }

    const std::string detach_sql = "DETACH DATABASE " + alias_text + ";";
    (void)sqlite3_exec(db, detach_sql.c_str(), nullptr, nullptr, nullptr);

    const std::string attach_sql = "ATTACH DATABASE '" + EscapeSqlLiteral(path) + "' AS " + alias_text + ";";
    const int attach_rc = sqlite3_exec(db, attach_sql.c_str(), nullptr, nullptr, nullptr);
    if (attach_rc != SQLITE_OK) {
        DB_LOGW(SafeTag(log_tag), "attach dict db failed path=%s rc=%d msg=%s", path.c_str(), attach_rc, sqlite3_errmsg(db));
        return false;
    }

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, validate_sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        DB_LOGW(SafeTag(log_tag), "attach dict db schema check failed path=%s rc=%d msg=%s", path.c_str(), rc, sqlite3_errmsg(db));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        (void)sqlite3_exec(db, detach_sql.c_str(), nullptr, nullptr, nullptr);
        return false;
    }

    const int step_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step_rc != SQLITE_ROW && step_rc != SQLITE_DONE) {
        DB_LOGW(SafeTag(log_tag), "attach dict db data check failed path=%s rc=%d msg=%s", path.c_str(), step_rc, sqlite3_errmsg(db));
        (void)sqlite3_exec(db, detach_sql.c_str(), nullptr, nullptr, nullptr);
        return false;
    }

    *attached_path = path;
    DB_LOGI(SafeTag(log_tag), "RESOURCE_OK kind=db scope=dictionary action=attach path=%s method=ATTACH DATABASE alias=%s", path.c_str(), alias);
    return true;
}

std::string SanitizeFieldValue(const char *value) {
    if (!value) {
        return {};
    }
    std::string out(value);
    std::replace(out.begin(), out.end(), '\n', ' ');
    std::replace(out.begin(), out.end(), '\r', ' ');
    std::replace(out.begin(), out.end(), '\t', ' ');
    return out;
}

bool QueryDictionaryEntryIdByWord(sqlite3 *db,
                                  const char *attached_alias,
                                  const std::string &word,
                                  int *entry_id,
                                  const char *log_tag) {
    if (!db || !entry_id || !attached_alias || !attached_alias[0] || word.empty()) {
        return false;
    }

    *entry_id = 0;
    const std::string sql = std::string("SELECT id FROM ") + attached_alias + ".word_dictionary WHERE word = ? LIMIT 1;";
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK || !stmt) {
        DB_LOGE(SafeTag(log_tag), "query entry_id prepare failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
        if (stmt) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    rc = sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        DB_LOGE(SafeTag(log_tag), "query entry_id bind failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *entry_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return true;
    }

    if (rc != SQLITE_DONE) {
        DB_LOGE(SafeTag(log_tag), "query entry_id step failed rc=%d msg=%s", rc, sqlite3_errmsg(db));
    }
    sqlite3_finalize(stmt);
    return false;
}

}  // namespace eteacher::database_manager
