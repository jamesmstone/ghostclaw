#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/config/schema.hpp"

#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace ghostclaw::calendar {

struct CalendarInfo {
  std::string id;
  std::string title;
};

struct CalendarEvent {
  std::string id;
  std::string calendar_id;
  std::string title;
  std::string start;
  std::string end;
  std::string location;
  std::string notes;
};

struct EventWriteRequest {
  std::string calendar;
  std::string title;
  std::string start;
  std::string end;
  std::string location;
  std::string notes;
};

struct EventUpdateRequest {
  std::string id;
  std::optional<std::string> title;
  std::optional<std::string> start;
  std::optional<std::string> end;
  std::optional<std::string> location;
  std::optional<std::string> notes;
};

class ICalendarBackend {
public:
  virtual ~ICalendarBackend() = default;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual common::Result<std::vector<CalendarInfo>> list_calendars() = 0;
  [[nodiscard]] virtual common::Result<std::vector<CalendarEvent>>
  list_events(const std::string &calendar, const std::string &start,
              const std::string &end) = 0;
  [[nodiscard]] virtual common::Result<CalendarEvent>
  create_event(const EventWriteRequest &request) = 0;
  [[nodiscard]] virtual common::Result<CalendarEvent>
  update_event(const EventUpdateRequest &request) = 0;
  [[nodiscard]] virtual common::Result<bool> delete_event(const std::string &event_id) = 0;
};

[[nodiscard]] std::unique_ptr<ICalendarBackend> make_calendar_backend(const config::Config &config);

} // namespace ghostclaw::calendar
