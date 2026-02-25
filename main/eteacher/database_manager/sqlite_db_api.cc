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
constexpr const char *kDbPathPrimary = "/sdcard/resource/database/words.db";
constexpr const char *kDbPathSecondary = "/sd/resource/database/words.db";
constexpr const char *kDbPathFallback = "resource/database/words.db";
constexpr const char *kDbPathRoot = "/sdcard/words.db";
constexpr const char *kDbPathDatabase = "/sdcard/database/words.db";
constexpr const char *kDbPathWinStyle = "/sdcard/resource\\database\\words.db";

constexpr const char *kWordBookDbPathPrimary = "/sdcard/user/user_data.db";
constexpr const char *kWordBookDbPathSecondary = "/sd/user/user_data.db";
constexpr const char *kWordBookDbPathFallback = "user/user_data.db";
constexpr const char *kWordBookDbPathUpper = "/sdcard/USER/USER_DATA.DB";
constexpr const char *kWordBookDbPathRoot = "/sdcard/user_data.db";
constexpr const char *kWordBookDbPathRootAlt = "/sd/user_data.db";
constexpr const char *kWordBookDbPathLegacyPrimary = "/sdcard/Data.db";
constexpr const char *kWordBookDbPathLegacySecondary = "/sd/Data.db";
constexpr const char *kWordBookDbPathShortPrimary = "/sdcard/USER/USER_D~1.DB";
constexpr const char *kWordBookDbPathShortSecondary = "/sdcard/user/user_d~1.db";
constexpr const char *kWordBookDbPathShortRoot = "/sd/USER_D~1.DB";

const char *SafeTag(const char *log_tag) {
    return (log_tag && log_tag[0]) ? log_tag : "DbApi";
}

bool HasSqliteMagicHeader(const char *path);
bool ValidateSqliteFileLayout(const char *path);

bool FileExists(const char *path) {
    if (!path || !path[0]) {
        return false;
    }
    struct stat st {};
    return ::stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool FindDbRecursive(const std::string &dir_path,
                     int depth,
                     const std::vector<std::string> &exact_names,
                     const char *name_hint,
                     bool allow_any_db_fallback,
                     std::string *out_path) {
    if (!out_path || depth < 0 || dir_path.empty()) {
        return false;
    }

    DIR *dir = ::opendir(dir_path.c_str());
    if (!dir) {
        return false;
    }

    bool found = false;
    std::string best_fallback;
    while (!found) {
        dirent *entry = ::readdir(dir);
        if (!entry) {
            break;
        }
        const char *name = entry->d_name;
        if (!name || name[0] == '\0' || (name[0] == '.' && name[1] == '\0') ||
            (name[0] == '.' && name[1] == '.' && name[2] == '\0')) {
            continue;
        }

        std::string full = dir_path;
        if (full.back() != '/') {
            full.push_back('/');
        }
        full += name;

        struct stat st {};
        if (::stat(full.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISREG(st.st_mode)) {
            std::string lower_name(name);
            std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            const bool exact = std::find(exact_names.begin(), exact_names.end(), lower_name) != exact_names.end();
            const bool hinted = name_hint && name_hint[0] &&
                                lower_name.find(name_hint) != std::string::npos &&
                                lower_name.find(".db") != std::string::npos;
            const bool is_db_file =
                (lower_name.size() >= 3 && lower_name.compare(lower_name.size() - 3, 3, ".db") == 0);

            if (exact || hinted) {
                *out_path = full;
                found = true;
            } else if (is_db_file && best_fallback.empty()) {
                best_fallback = full;
            }
            continue;
        }

        if (S_ISDIR(st.st_mode) && depth > 0) {
            found = FindDbRecursive(full, depth - 1, exact_names, name_hint, allow_any_db_fallback, out_path);
        }
    }

    ::closedir(dir);
    if (!found && allow_any_db_fallback && !best_fallback.empty()) {
        *out_path = best_fallback;
        return true;
    }
    return found;
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

bool ValidateUserDataDbFile(const char *path, const char *required_table, const char *log_tag) {
    if (!path || !path[0]) {
        return false;
    }
    if (!FileExists(path)) {
        return false;
    }
    if (!HasSqliteMagicHeader(path) || !ValidateSqliteFileLayout(path)) {
        DB_LOGW(SafeTag(log_tag), "skip invalid sqlite user db file: %s", path);
        return false;
    }

    sqlite3 *db = nullptr;
    const int rc = sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK || !db) {
        DB_LOGW(SafeTag(log_tag), "open user db failed path=%s rc=%d msg=%s", path, rc, db ? sqlite3_errmsg(db) : "null");
        if (db) {
            sqlite3_close(db);
        }
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

std::vector<std::string> BuildDictionaryCandidates(const std::string &discovered_path) {
    std::vector<std::string> paths;
    if (!discovered_path.empty()) {
        paths.push_back(discovered_path);
    }

    const char *fallback_paths[] = {
        kDbPathPrimary,
        kDbPathSecondary,
        kDbPathFallback,
        kDbPathRoot,
        kDbPathDatabase,
        kDbPathWinStyle,
    };
    for (const char *path : fallback_paths) {
        if (!path || !path[0]) {
            continue;
        }
        const std::string candidate(path);
        if (std::find(paths.begin(), paths.end(), candidate) == paths.end()) {
            paths.push_back(candidate);
        }
    }
    return paths;
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

    if (SD.begin((int)SD_PIN_NUM_CS, SPI, 20000000, "/sd")) {
        mounted = true;
        DB_LOGI(SafeTag(log_tag), "sqlite sd vfs mount OK: /sd");
        return true;
    }
    DB_LOGW(SafeTag(log_tag), "sqlite sd vfs mount failed: /sd");
    return false;
}

std::string DiscoverDictionaryDbPath(const char *log_tag) {
    const char *candidates[] = {
        kDbPathPrimary,
        kDbPathSecondary,
        kDbPathFallback,
        kDbPathRoot,
        kDbPathDatabase,
        kDbPathWinStyle,
    };
    for (const char *path : candidates) {
        if (FileExists(path)) {
            DB_LOGI(SafeTag(log_tag), "dict db discovered by candidate: %s", path);
            return std::string(path);
        }
    }

    std::string discovered;
    const std::vector<std::string> exact = {"words.db"};
    if (FindDbRecursive("/sdcard", 6, exact, "word", true, &discovered)) {
        DB_LOGI(SafeTag(log_tag), "dict db discovered by recursive scan: %s", discovered.c_str());
        return discovered;
    }
    if (FindDbRecursive("/sd", 6, exact, "word", true, &discovered)) {
        DB_LOGI(SafeTag(log_tag), "dict db discovered by recursive scan: %s", discovered.c_str());
        return discovered;
    }
    return {};
}

std::string DiscoverUserDataDbPath(const char *log_tag, const char *required_table) {
    const char *candidates[] = {
        kWordBookDbPathPrimary,
        kWordBookDbPathSecondary,
        kWordBookDbPathFallback,
        kWordBookDbPathUpper,
        kWordBookDbPathRoot,
        kWordBookDbPathRootAlt,
        kWordBookDbPathLegacyPrimary,
        kWordBookDbPathLegacySecondary,
        kWordBookDbPathShortPrimary,
        kWordBookDbPathShortSecondary,
        kWordBookDbPathShortRoot,
    };
    for (const char *path : candidates) {
        if (ValidateUserDataDbFile(path, required_table, log_tag)) {
            DB_LOGI(SafeTag(log_tag), "user db discovered by candidate: %s", path);
            return std::string(path);
        }
    }

    std::string discovered;
    const std::vector<std::string> exact = {"user_data.db", "data.db", "user_d~1.db"};
    if (FindDbRecursive("/sdcard", 6, exact, nullptr, false, &discovered) &&
        ValidateUserDataDbFile(discovered.c_str(), required_table, log_tag)) {
        DB_LOGI(SafeTag(log_tag), "user db discovered by recursive scan: %s", discovered.c_str());
        return discovered;
    }
    if (FindDbRecursive("/sd", 6, exact, nullptr, false, &discovered) &&
        ValidateUserDataDbFile(discovered.c_str(), required_table, log_tag)) {
        DB_LOGI(SafeTag(log_tag), "user db discovered by recursive scan: %s", discovered.c_str());
        return discovered;
    }
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

bool OpenValidatedDictionaryDbReadonly(const std::string &discovered_path,
                                       sqlite3 **out_db,
                                       std::string *out_path,
                                       const char *log_tag) {
    if (!out_db || !out_path) {
        return false;
    }
    *out_db = nullptr;
    out_path->clear();

    const std::vector<std::string> paths = BuildDictionaryCandidates(discovered_path);
    for (const std::string &path : paths) {
        if (!FileExists(path.c_str())) {
            continue;
        }
        if (!HasSqliteMagicHeader(path.c_str()) || !ValidateSqliteFileLayout(path.c_str())) {
            DB_LOGW(SafeTag(log_tag), "skip invalid sqlite dict db file: %s", path.c_str());
            continue;
        }

        sqlite3 *db = nullptr;
        const int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK || !db) {
            DB_LOGW(SafeTag(log_tag), "open dict db failed path=%s rc=%d msg=%s", path.c_str(), rc, db ? sqlite3_errmsg(db) : "null");
            if (db) {
                sqlite3_close(db);
            }
            continue;
        }

        if (!ValidateWordDictionarySchema(db, path.c_str(), log_tag)) {
            sqlite3_close(db);
            continue;
        }

        *out_db = db;
        *out_path = path;
        return true;
    }

    return false;
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

    const std::vector<std::string> paths = BuildDictionaryCandidates(discovered_path);
    const std::string alias_text(alias);
    const std::string validate_sql = "SELECT 1 FROM " + alias_text + ".word_dictionary LIMIT 1;";

    for (const std::string &path : paths) {
        if (!FileExists(path.c_str())) {
            continue;
        }
        if (!HasSqliteMagicHeader(path.c_str()) || !ValidateSqliteFileLayout(path.c_str())) {
            DB_LOGW(SafeTag(log_tag), "skip invalid sqlite attach db file: %s", path.c_str());
            continue;
        }

        const std::string detach_sql = "DETACH DATABASE " + alias_text + ";";
        (void)sqlite3_exec(db, detach_sql.c_str(), nullptr, nullptr, nullptr);

        const std::string attach_sql = "ATTACH DATABASE '" + EscapeSqlLiteral(path) + "' AS " + alias_text + ";";
        const int attach_rc = sqlite3_exec(db, attach_sql.c_str(), nullptr, nullptr, nullptr);
        if (attach_rc != SQLITE_OK) {
            DB_LOGW(SafeTag(log_tag), "attach dict db failed path=%s rc=%d msg=%s", path.c_str(), attach_rc, sqlite3_errmsg(db));
            continue;
        }

        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(db, validate_sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK || !stmt) {
            DB_LOGW(SafeTag(log_tag), "attach dict db schema check failed path=%s rc=%d msg=%s", path.c_str(), rc, sqlite3_errmsg(db));
            if (stmt) {
                sqlite3_finalize(stmt);
            }
            (void)sqlite3_exec(db, detach_sql.c_str(), nullptr, nullptr, nullptr);
            continue;
        }

        const int step_rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (step_rc != SQLITE_ROW && step_rc != SQLITE_DONE) {
            DB_LOGW(SafeTag(log_tag), "attach dict db data check failed path=%s rc=%d msg=%s", path.c_str(), step_rc, sqlite3_errmsg(db));
            (void)sqlite3_exec(db, detach_sql.c_str(), nullptr, nullptr, nullptr);
            continue;
        }

        *attached_path = path;
        return true;
    }

    return false;
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
