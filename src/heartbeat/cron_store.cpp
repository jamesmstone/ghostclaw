#include "ghostclaw/heartbeat/cron_store.hpp"

#include <fstream>

namespace ghostclaw::heartbeat {

namespace {

common::Status exec_sql(sqlite3 *db, const std::string &sql) {
  char *err = nullptr;
  const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    const std::string message = err == nullptr ? "sqlite error" : err;
    if (err != nullptr) {
      sqlite3_free(err);
    }
    return common::Status::error(message);
  }
  return common::Status::success();
}

} // namespace

CronStore::CronStore(std::filesystem::path db_path) : db_path_(std::move(db_path)) {
  std::error_code ec;
  std::filesystem::create_directories(db_path_.parent_path(), ec);
  if (sqlite3_open(db_path_.string().c_str(), &db_) != SQLITE_OK) {
    db_ = nullptr;
    return;
  }
  (void)init_schema();
}

CronStore::~CronStore() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

common::Status CronStore::init_schema() {
  if (db_ == nullptr) {
    return common::Status::error("cron db not initialized");
  }
  return exec_sql(db_, R"(
CREATE TABLE IF NOT EXISTS cron_jobs (
  id TEXT PRIMARY KEY,
  expression TEXT NOT NULL,
  command TEXT NOT NULL,
  next_run TEXT NOT NULL,
  last_run TEXT,
  last_status TEXT
);
)");
}

common::Status CronStore::add_job(const CronJob &job) {
  if (db_ == nullptr) {
    return common::Status::error("cron db not initialized");
  }
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "INSERT OR REPLACE INTO cron_jobs(id, expression, command, next_run, last_run, last_status) "
      "VALUES(?1, ?2, ?3, ?4, ?5, ?6)";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Status::error(sqlite3_errmsg(db_));
  }

  sqlite3_bind_text(stmt, 1, job.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, job.expression.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, job.command.c_str(), -1, SQLITE_TRANSIENT);
  const std::string next_run = time_point_to_unix_string(job.next_run);
  sqlite3_bind_text(stmt, 4, next_run.c_str(), -1, SQLITE_TRANSIENT);
  if (job.last_run.has_value()) {
    const std::string last = time_point_to_unix_string(*job.last_run);
    sqlite3_bind_text(stmt, 5, last.c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 5);
  }
  if (job.last_status.has_value()) {
    sqlite3_bind_text(stmt, 6, job.last_status->c_str(), -1, SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 6);
  }

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return common::Status::error(sqlite3_errmsg(db_));
  }
  return common::Status::success();
}

common::Result<bool> CronStore::remove_job(const std::string &id) {
  if (db_ == nullptr) {
    return common::Result<bool>::failure("cron db not initialized");
  }

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_, "DELETE FROM cron_jobs WHERE id = ?1", -1, &stmt, nullptr) !=
      SQLITE_OK) {
    return common::Result<bool>::failure(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return common::Result<bool>::failure(sqlite3_errmsg(db_));
  }
  return common::Result<bool>::success(sqlite3_changes(db_) > 0);
}

common::Result<CronJob> CronStore::row_to_job(sqlite3_stmt *stmt) {
  CronJob job;
  job.id = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  job.expression = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
  job.command = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
  auto next = unix_string_to_time_point(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)));
  if (!next.ok()) {
    return common::Result<CronJob>::failure(next.error());
  }
  job.next_run = next.value();

  const auto *last_run_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
  if (last_run_text != nullptr) {
    auto last = unix_string_to_time_point(last_run_text);
    if (last.ok()) {
      job.last_run = last.value();
    }
  }
  const auto *last_status = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
  if (last_status != nullptr) {
    job.last_status = last_status;
  }
  return common::Result<CronJob>::success(std::move(job));
}

common::Result<std::vector<CronJob>> CronStore::list_jobs() {
  if (db_ == nullptr) {
    return common::Result<std::vector<CronJob>>::failure("cron db not initialized");
  }
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT id, expression, command, next_run, last_run, last_status FROM "
                         "cron_jobs ORDER BY next_run ASC",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Result<std::vector<CronJob>>::failure(sqlite3_errmsg(db_));
  }

  std::vector<CronJob> out;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto job = row_to_job(stmt);
    if (!job.ok()) {
      sqlite3_finalize(stmt);
      return common::Result<std::vector<CronJob>>::failure(job.error());
    }
    out.push_back(std::move(job.value()));
  }
  sqlite3_finalize(stmt);
  return common::Result<std::vector<CronJob>>::success(std::move(out));
}

common::Result<std::vector<CronJob>> CronStore::get_due_jobs() {
  if (db_ == nullptr) {
    return common::Result<std::vector<CronJob>>::failure("cron db not initialized");
  }
  const auto now = time_point_to_unix_string(std::chrono::system_clock::now());

  sqlite3_stmt *stmt = nullptr;
  const char *sql = "SELECT id, expression, command, next_run, last_run, last_status FROM cron_jobs "
                    "WHERE CAST(next_run AS INTEGER) <= CAST(?1 AS INTEGER) ORDER BY next_run ASC";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Result<std::vector<CronJob>>::failure(sqlite3_errmsg(db_));
  }
  sqlite3_bind_text(stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<CronJob> out;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto job = row_to_job(stmt);
    if (!job.ok()) {
      sqlite3_finalize(stmt);
      return common::Result<std::vector<CronJob>>::failure(job.error());
    }
    out.push_back(std::move(job.value()));
  }
  sqlite3_finalize(stmt);
  return common::Result<std::vector<CronJob>>::success(std::move(out));
}

common::Status CronStore::update_after_run(const std::string &id, const std::string &status,
                                           std::chrono::system_clock::time_point next_run) {
  if (db_ == nullptr) {
    return common::Status::error("cron db not initialized");
  }
  sqlite3_stmt *stmt = nullptr;
  const char *sql =
      "UPDATE cron_jobs SET last_run = ?2, last_status = ?3, next_run = ?4 WHERE id = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return common::Status::error(sqlite3_errmsg(db_));
  }
  const std::string now = time_point_to_unix_string(std::chrono::system_clock::now());
  const std::string next = time_point_to_unix_string(next_run);
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, now.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, next.c_str(), -1, SQLITE_TRANSIENT);

  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  if (rc != SQLITE_DONE) {
    return common::Status::error(sqlite3_errmsg(db_));
  }
  return common::Status::success();
}

} // namespace ghostclaw::heartbeat
