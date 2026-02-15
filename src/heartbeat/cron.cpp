#include "ghostclaw/heartbeat/cron.hpp"

#include "ghostclaw/common/fs.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <set>
#include <sstream>

namespace ghostclaw::heartbeat {

namespace {

bool contains_value(const std::vector<int> &values, const int value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

std::string normalize_expression(std::string expression) {
  expression = common::to_lower(common::trim(expression));
  if (expression == "@hourly") {
    return "0 * * * *";
  }
  if (expression == "@daily") {
    return "0 0 * * *";
  }
  if (expression == "@weekly") {
    return "0 0 * * 0";
  }
  if (expression == "@monthly") {
    return "0 0 1 * *";
  }
  return expression;
}

common::Result<int> parse_int(const std::string &value) {
  int parsed = 0;
  auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec != std::errc() || ptr != value.data() + value.size()) {
    return common::Result<int>::failure("invalid integer: " + value);
  }
  return common::Result<int>::success(parsed);
}

} // namespace

common::Result<CronExpression> CronExpression::parse(std::string_view expression_view) {
  const std::string normalized = normalize_expression(std::string(expression_view));

  std::istringstream stream(normalized);
  std::string minute_field;
  std::string hour_field;
  std::string day_field;
  std::string month_field;
  std::string weekday_field;
  if (!(stream >> minute_field >> hour_field >> day_field >> month_field >> weekday_field)) {
    return common::Result<CronExpression>::failure(
        "cron expression must have 5 fields");
  }

  CronExpression expression;
  auto minutes = parse_field(minute_field, 0, 59);
  if (!minutes.ok()) {
    return common::Result<CronExpression>::failure(minutes.error());
  }
  auto hours = parse_field(hour_field, 0, 23);
  if (!hours.ok()) {
    return common::Result<CronExpression>::failure(hours.error());
  }
  auto days = parse_field(day_field, 1, 31);
  if (!days.ok()) {
    return common::Result<CronExpression>::failure(days.error());
  }
  auto months = parse_field(month_field, 1, 12);
  if (!months.ok()) {
    return common::Result<CronExpression>::failure(months.error());
  }
  auto weekdays = parse_field(weekday_field, 0, 6);
  if (!weekdays.ok()) {
    return common::Result<CronExpression>::failure(weekdays.error());
  }

  expression.minutes_ = std::move(minutes.value());
  expression.hours_ = std::move(hours.value());
  expression.days_ = std::move(days.value());
  expression.months_ = std::move(months.value());
  expression.weekdays_ = std::move(weekdays.value());
  return common::Result<CronExpression>::success(std::move(expression));
}

common::Result<std::vector<int>> CronExpression::parse_field(std::string_view field_view,
                                                             const int min, const int max) {
  const std::string field = common::trim(std::string(field_view));
  if (field.empty()) {
    return common::Result<std::vector<int>>::failure("empty cron field");
  }

  std::set<int> values;

  auto add_range = [&](int start, int end, int step) -> common::Status {
    if (step <= 0) {
      return common::Status::error("step must be positive");
    }
    if (start > end || start < min || end > max) {
      return common::Status::error("field range out of bounds");
    }
    for (int value = start; value <= end; value += step) {
      values.insert(value);
    }
    return common::Status::success();
  };

  std::stringstream parts(field);
  std::string segment;
  while (std::getline(parts, segment, ',')) {
    segment = common::trim(segment);
    if (segment.empty()) {
      continue;
    }

    if (segment == "*") {
      auto status = add_range(min, max, 1);
      if (!status.ok()) {
        return common::Result<std::vector<int>>::failure(status.error());
      }
      continue;
    }

    const auto slash = segment.find('/');
    if (slash != std::string::npos) {
      const std::string base = common::trim(segment.substr(0, slash));
      const std::string step_raw = common::trim(segment.substr(slash + 1));
      auto step = parse_int(step_raw);
      if (!step.ok()) {
        return common::Result<std::vector<int>>::failure(step.error());
      }
      int range_start = min;
      int range_end = max;
      if (base != "*" && !base.empty()) {
        const auto dash = base.find('-');
        if (dash != std::string::npos) {
          auto left = parse_int(common::trim(base.substr(0, dash)));
          auto right = parse_int(common::trim(base.substr(dash + 1)));
          if (!left.ok() || !right.ok()) {
            return common::Result<std::vector<int>>::failure("invalid step range");
          }
          range_start = left.value();
          range_end = right.value();
        } else {
          auto value = parse_int(base);
          if (!value.ok()) {
            return common::Result<std::vector<int>>::failure(value.error());
          }
          range_start = value.value();
          range_end = max;
        }
      }
      auto status = add_range(range_start, range_end, step.value());
      if (!status.ok()) {
        return common::Result<std::vector<int>>::failure(status.error());
      }
      continue;
    }

    const auto dash = segment.find('-');
    if (dash != std::string::npos) {
      auto left = parse_int(common::trim(segment.substr(0, dash)));
      auto right = parse_int(common::trim(segment.substr(dash + 1)));
      if (!left.ok() || !right.ok()) {
        return common::Result<std::vector<int>>::failure("invalid range");
      }
      auto status = add_range(left.value(), right.value(), 1);
      if (!status.ok()) {
        return common::Result<std::vector<int>>::failure(status.error());
      }
      continue;
    }

    auto value = parse_int(segment);
    if (!value.ok()) {
      return common::Result<std::vector<int>>::failure(value.error());
    }
    if (value.value() < min || value.value() > max) {
      return common::Result<std::vector<int>>::failure("field value out of bounds");
    }
    values.insert(value.value());
  }

  if (values.empty()) {
    return common::Result<std::vector<int>>::failure("no values in field");
  }

  return common::Result<std::vector<int>>::success(std::vector<int>(values.begin(), values.end()));
}

bool CronExpression::matches(const std::tm &time) const {
  return contains_value(minutes_, time.tm_min) && contains_value(hours_, time.tm_hour) &&
         contains_value(days_, time.tm_mday) && contains_value(months_, time.tm_mon + 1) &&
         contains_value(weekdays_, time.tm_wday);
}

std::chrono::system_clock::time_point
CronExpression::next_occurrence(std::chrono::system_clock::time_point after) const {
  auto candidate = std::chrono::time_point_cast<std::chrono::minutes>(after) +
                   std::chrono::minutes(1);

  const auto limit = candidate + std::chrono::hours(24 * 366 * 2);
  while (candidate < limit) {
    const auto time = std::chrono::system_clock::to_time_t(candidate);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    if (matches(tm)) {
      return candidate;
    }
    candidate += std::chrono::minutes(1);
  }

  return candidate;
}

std::string time_point_to_unix_string(std::chrono::system_clock::time_point time_point) {
  const auto value = std::chrono::duration_cast<std::chrono::seconds>(
                         time_point.time_since_epoch())
                         .count();
  return std::to_string(value);
}

common::Result<std::chrono::system_clock::time_point>
unix_string_to_time_point(const std::string &value) {
  long long seconds = 0;
  auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), seconds);
  if (ec != std::errc() || ptr != value.data() + value.size()) {
    return common::Result<std::chrono::system_clock::time_point>::failure("invalid unix time");
  }
  return common::Result<std::chrono::system_clock::time_point>::success(
      std::chrono::system_clock::time_point(std::chrono::seconds(seconds)));
}

} // namespace ghostclaw::heartbeat
