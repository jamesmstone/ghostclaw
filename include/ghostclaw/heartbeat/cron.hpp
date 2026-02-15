#pragma once

#include "ghostclaw/common/result.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghostclaw::heartbeat {

struct CronJob {
  std::string id;
  std::string expression;
  std::string command;
  std::chrono::system_clock::time_point next_run{};
  std::optional<std::chrono::system_clock::time_point> last_run;
  std::optional<std::string> last_status;
};

class CronExpression {
public:
  [[nodiscard]] static common::Result<CronExpression> parse(std::string_view expression);

  [[nodiscard]] std::chrono::system_clock::time_point
  next_occurrence(std::chrono::system_clock::time_point after =
                      std::chrono::system_clock::now()) const;
  [[nodiscard]] bool matches(const std::tm &time) const;

private:
  [[nodiscard]] static common::Result<std::vector<int>> parse_field(std::string_view field, int min,
                                                                     int max);

  std::vector<int> minutes_;
  std::vector<int> hours_;
  std::vector<int> days_;
  std::vector<int> months_;
  std::vector<int> weekdays_;
};

[[nodiscard]] std::string time_point_to_unix_string(
    std::chrono::system_clock::time_point time_point);
[[nodiscard]] common::Result<std::chrono::system_clock::time_point>
unix_string_to_time_point(const std::string &value);

} // namespace ghostclaw::heartbeat
