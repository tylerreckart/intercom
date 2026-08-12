#include "alfred/session_store.hpp"
#include "alfred/util.hpp"

#include <chrono>
#include <filesystem>
#include <sqlite3.h>

namespace alfred {
namespace {

std::int64_t now_unix() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

SessionStore::SessionStore(std::string db_path) : db_path_(std::move(db_path)) {}

SessionStore::~SessionStore() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool SessionStore::open(std::string* err) {
  try {
    std::filesystem::path p(db_path_);
    if (p.has_parent_path()) {
      std::filesystem::create_directories(p.parent_path());
    }
  } catch (const std::exception& e) {
    if (err) *err = std::string("session db mkdir: ") + e.what();
    return false;
  }

  if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
    if (err) *err = db_ ? sqlite3_errmsg(db_) : "sqlite3_open failed";
    if (db_) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return false;
  }

  const char* sql =
      "CREATE TABLE IF NOT EXISTS device_sessions ("
      "  device_id TEXT PRIMARY KEY NOT NULL,"
      "  conversation_id INTEGER NOT NULL,"
      "  last_turn_id TEXT NOT NULL DEFAULT '',"
      "  updated_at INTEGER NOT NULL"
      ");";
  char* errmsg = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
    if (err) *err = errmsg ? errmsg : "create table failed";
    sqlite3_free(errmsg);
    return false;
  }
  return true;
}

std::optional<DeviceSession> SessionStore::get(const std::string& device_id) const {
  if (!db_) return std::nullopt;
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "SELECT device_id, conversation_id, last_turn_id, updated_at "
      "FROM device_sessions WHERE device_id = ?";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, device_id.c_str(), -1, SQLITE_TRANSIENT);
  std::optional<DeviceSession> out;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    DeviceSession s;
    s.device_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    s.conversation_id = sqlite3_column_int64(stmt, 1);
    const unsigned char* turn = sqlite3_column_text(stmt, 2);
    s.last_turn_id = turn ? reinterpret_cast<const char*>(turn) : "";
    s.updated_at = sqlite3_column_int64(stmt, 3);
    out = std::move(s);
  }
  sqlite3_finalize(stmt);
  return out;
}

bool SessionStore::upsert(const DeviceSession& session, std::string* err) {
  if (!db_) {
    if (err) *err = "session store not open";
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT INTO device_sessions(device_id, conversation_id, last_turn_id, updated_at) "
      "VALUES(?,?,?,?) "
      "ON CONFLICT(device_id) DO UPDATE SET "
      "conversation_id=excluded.conversation_id, "
      "last_turn_id=excluded.last_turn_id, "
      "updated_at=excluded.updated_at";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    if (err) *err = sqlite3_errmsg(db_);
    return false;
  }
  const std::int64_t ts = session.updated_at > 0 ? session.updated_at : now_unix();
  sqlite3_bind_text(stmt, 1, session.device_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, session.conversation_id);
  sqlite3_bind_text(stmt, 3, session.last_turn_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, ts);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    if (err) *err = sqlite3_errmsg(db_);
    return false;
  }
  return true;
}

}  // namespace alfred
