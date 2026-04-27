#pragma once

#include <memory>
#include <string>

#include <sqlite3.h>

namespace word_practice::db {

struct SqliteStmtFinalizer {
	void operator()(sqlite3_stmt *stmt) const {
		if (stmt != nullptr) {
			sqlite3_finalize(stmt);
		}
	}
};

using StatementPtr = std::unique_ptr<sqlite3_stmt, SqliteStmtFinalizer>;

inline bool StepDone(sqlite3_stmt *stmt) {
	const int rc = sqlite3_step(stmt);
	return rc == SQLITE_DONE || rc == SQLITE_ROW;
}

inline bool PrepareStatement(sqlite3 *database, const char *sql, StatementPtr *stmt) {
	if (database == nullptr || sql == nullptr || stmt == nullptr) {
		return false;
	}

	sqlite3_stmt *raw_stmt = nullptr;
	const int rc = sqlite3_prepare_v2(database, sql, -1, &raw_stmt, nullptr);
	if (rc != SQLITE_OK || raw_stmt == nullptr) {
		if (raw_stmt != nullptr) {
			sqlite3_finalize(raw_stmt);
		}
		return false;
	}

	stmt->reset(raw_stmt);
	return true;
}

inline bool PrepareStatement(sqlite3 *database, const std::string &sql, StatementPtr *stmt) {
	return PrepareStatement(database, sql.c_str(), stmt);
}

}  // namespace word_practice::db
