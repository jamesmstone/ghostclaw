#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/heartbeat/cron.hpp"

#include <filesystem>
#include <sqlite3.h>
#include <vector>

namespace ghostclaw::heartbeat {

class CronStore {
public:
  explicit CronStore(std::filesystem::path db_path);
  ~CronStore();

  [[nodiscard]] common::Status add_job(const CronJob &job);
  [[nodiscard]] common::Result<bool> remove_job(const std::string &id);
  [[nodiscard]] common::Result<std::vector<CronJob>> list_jobs();
  [[nodiscard]] common::Result<std::vector<CronJob>> get_due_jobs();
  [[nodiscard]] common::Status update_after_run(
      const std::string &id, const std::string &status,
      std::chrono::system_clock::time_point next_run);

private:
  [[nodiscard]] common::Status init_schema();
  [[nodiscard]] common::Result<CronJob> row_to_job(sqlite3_stmt *stmt);

  std::filesystem::path db_path_;
  sqlite3 *db_ = nullptr;
};

} // namespace ghostclaw::heartbeat
