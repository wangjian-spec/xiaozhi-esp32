#pragma once

#include <string>

#include <sqlite3.h>

namespace eteacher::database_manager {

const char *GetDictionaryDbFixedPath();
const char *GetQuestionDbFixedPath();
const char *GetUserDataDbFixedPath();

bool EnsureSqliteRuntimeReady(const char *log_tag);
bool EnsureSqliteSdMounted(const char *log_tag);

std::string DiscoverDictionaryDbPath(const char *log_tag);
std::string DiscoverDictionaryDbPath(const char *log_tag, int stage_index);
std::string DiscoverQuestionDbPath(const char *log_tag);
std::string DiscoverUserDataDbPath(const char *log_tag, const char *required_table = "vocab_items");

bool ConfigureWriteConnection(sqlite3 *db, const char *log_tag);
bool BeginTransaction(sqlite3 *db, const char *log_tag);
bool CommitTransaction(sqlite3 *db, const char *log_tag);
void RollbackTransaction(sqlite3 *db, const char *log_tag);

bool EnsureTasksTable(sqlite3 *db, const char *log_tag);

bool OpenReadonlyDbFile(const std::string &discovered_path,
                        sqlite3 **out_db,
                        std::string *out_path,
                        const char *log_tag,
                        const char *scope = "db");

bool OpenValidatedDictionaryDbReadonly(const std::string &discovered_path,
                                       sqlite3 **out_db,
                                       std::string *out_path,
                                       const char *log_tag);

bool AttachValidatedDictionaryDb(sqlite3 *db,
                                 const std::string &discovered_path,
                                 std::string *attached_path,
                                 const char *alias,
                                 const char *log_tag);

std::string SanitizeFieldValue(const char *value);
bool QueryDictionaryEntryIdByWord(sqlite3 *db,
                                  const char *attached_alias,
                                  const std::string &word,
                                  int *entry_id,
                                  const char *log_tag);

}  // namespace eteacher::database_manager
