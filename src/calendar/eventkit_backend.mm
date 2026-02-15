#include "ghostclaw/calendar/backend.hpp"

#if defined(__APPLE__)

#import <EventKit/EventKit.h>
#import <Foundation/Foundation.h>

#include "ghostclaw/common/fs.hpp"

#include <algorithm>

namespace ghostclaw::calendar {

namespace {

std::string ns_to_std(NSString *value) {
  if (value == nil) {
    return "";
  }
  return std::string([value UTF8String]);
}

NSString *std_to_ns(const std::string &value) {
  return [NSString stringWithUTF8String:value.c_str()];
}

NSDate *parse_date(const std::string &value) {
  const std::string trimmed = common::trim(value);
  if (trimmed.empty()) {
    return nil;
  }

  NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
  iso.formatOptions = NSISO8601DateFormatWithInternetDateTime | NSISO8601DateFormatWithFractionalSeconds;
  NSDate *parsed = [iso dateFromString:std_to_ns(trimmed)];
  if (parsed != nil) {
    return parsed;
  }
  iso.formatOptions = NSISO8601DateFormatWithInternetDateTime;
  parsed = [iso dateFromString:std_to_ns(trimmed)];
  if (parsed != nil) {
    return parsed;
  }

  bool numeric = !trimmed.empty();
  for (const char ch : trimmed) {
    if (ch < '0' || ch > '9') {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    try {
      const long long epoch = std::stoll(trimmed);
      return [NSDate dateWithTimeIntervalSince1970:static_cast<NSTimeInterval>(epoch)];
    } catch (...) {
      return nil;
    }
  }

  return nil;
}

std::string format_date(NSDate *value) {
  if (value == nil) {
    return "";
  }
  NSISO8601DateFormatter *iso = [[NSISO8601DateFormatter alloc] init];
  iso.formatOptions = NSISO8601DateFormatWithInternetDateTime;
  return ns_to_std([iso stringFromDate:value]);
}

CalendarEvent to_calendar_event(EKEvent *event) {
  CalendarEvent out;
  if (event == nil) {
    return out;
  }
  out.id = ns_to_std(event.eventIdentifier);
  out.calendar_id = ns_to_std(event.calendar.calendarIdentifier);
  out.title = ns_to_std(event.title);
  out.start = format_date(event.startDate);
  out.end = format_date(event.endDate);
  out.location = ns_to_std(event.location);
  out.notes = ns_to_std(event.notes);
  return out;
}

EKCalendar *select_calendar(EKEventStore *store, const std::string &calendar) {
  NSString *target = std_to_ns(common::trim(calendar));
  if (target == nil || [target length] == 0) {
    return store.defaultCalendarForNewEvents;
  }

  for (EKCalendar *candidate in [store calendarsForEntityType:EKEntityTypeEvent]) {
    if ([candidate.calendarIdentifier isEqualToString:target] || [candidate.title isEqualToString:target]) {
      return candidate;
    }
  }
  return nil;
}

class EventKitCalendarBackend final : public ICalendarBackend {
public:
  EventKitCalendarBackend() : store_([[EKEventStore alloc] init]) {}
  ~EventKitCalendarBackend() override { store_ = nil; }

  [[nodiscard]] std::string_view name() const override { return "eventkit"; }

  [[nodiscard]] common::Result<std::vector<CalendarInfo>> list_calendars() override {
    if (!ensure_access()) {
      return common::Result<std::vector<CalendarInfo>>::failure(
          "calendar access denied; grant Calendar access to GhostClaw in macOS settings");
    }

    std::vector<CalendarInfo> out;
    for (EKCalendar *calendar in [store_ calendarsForEntityType:EKEntityTypeEvent]) {
      CalendarInfo item;
      item.id = ns_to_std(calendar.calendarIdentifier);
      item.title = ns_to_std(calendar.title);
      out.push_back(std::move(item));
    }
    return common::Result<std::vector<CalendarInfo>>::success(std::move(out));
  }

  [[nodiscard]] common::Result<std::vector<CalendarEvent>>
  list_events(const std::string &calendar, const std::string &start,
              const std::string &end) override {
    if (!ensure_access()) {
      return common::Result<std::vector<CalendarEvent>>::failure(
          "calendar access denied; grant Calendar access to GhostClaw in macOS settings");
    }

    NSDate *start_date = parse_date(start);
    NSDate *end_date = parse_date(end);
    if (start_date == nil) {
      start_date = [NSDate date];
    }
    if (end_date == nil) {
      end_date = [start_date dateByAddingTimeInterval:14 * 24 * 60 * 60];
    }
    if ([end_date compare:start_date] != NSOrderedDescending) {
      return common::Result<std::vector<CalendarEvent>>::failure("end must be after start");
    }

    NSArray<EKCalendar *> *scoped = nil;
    if (!common::trim(calendar).empty()) {
      EKCalendar *selected = select_calendar(store_, calendar);
      if (selected == nil) {
        return common::Result<std::vector<CalendarEvent>>::failure("calendar not found: " + calendar);
      }
      scoped = @[ selected ];
    }

    NSPredicate *predicate = [store_ predicateForEventsWithStartDate:start_date
                                                             endDate:end_date
                                                           calendars:scoped];
    NSArray<EKEvent *> *events = [store_ eventsMatchingPredicate:predicate];
    std::vector<CalendarEvent> out;
    out.reserve([events count]);
    for (EKEvent *event in events) {
      out.push_back(to_calendar_event(event));
    }
    std::sort(out.begin(), out.end(), [](const CalendarEvent &lhs, const CalendarEvent &rhs) {
      return lhs.start < rhs.start;
    });
    return common::Result<std::vector<CalendarEvent>>::success(std::move(out));
  }

  [[nodiscard]] common::Result<CalendarEvent>
  create_event(const EventWriteRequest &request) override {
    if (!ensure_access()) {
      return common::Result<CalendarEvent>::failure(
          "calendar access denied; grant Calendar access to GhostClaw in macOS settings");
    }
    if (common::trim(request.title).empty()) {
      return common::Result<CalendarEvent>::failure("title is required");
    }

    NSDate *start_date = parse_date(request.start);
    NSDate *end_date = parse_date(request.end);
    if (start_date == nil) {
      return common::Result<CalendarEvent>::failure("start must be an ISO date/time");
    }
    if (end_date == nil) {
      end_date = [start_date dateByAddingTimeInterval:30 * 60];
    }
    if ([end_date compare:start_date] != NSOrderedDescending) {
      return common::Result<CalendarEvent>::failure("end must be after start");
    }

    EKCalendar *calendar = select_calendar(store_, request.calendar);
    if (calendar == nil) {
      return common::Result<CalendarEvent>::failure(
          "calendar not found or unavailable: " + request.calendar);
    }

    EKEvent *event = [EKEvent eventWithEventStore:store_];
    event.calendar = calendar;
    event.title = std_to_ns(request.title);
    event.startDate = start_date;
    event.endDate = end_date;
    if (!common::trim(request.location).empty()) {
      event.location = std_to_ns(request.location);
    }
    if (!common::trim(request.notes).empty()) {
      event.notes = std_to_ns(request.notes);
    }

    NSError *error = nil;
    if (![store_ saveEvent:event span:EKSpanThisEvent commit:YES error:&error]) {
      return common::Result<CalendarEvent>::failure(
          "failed to create calendar event: " + ns_to_std([error localizedDescription]));
    }
    return common::Result<CalendarEvent>::success(to_calendar_event(event));
  }

  [[nodiscard]] common::Result<CalendarEvent>
  update_event(const EventUpdateRequest &request) override {
    if (!ensure_access()) {
      return common::Result<CalendarEvent>::failure(
          "calendar access denied; grant Calendar access to GhostClaw in macOS settings");
    }
    if (common::trim(request.id).empty()) {
      return common::Result<CalendarEvent>::failure("id is required");
    }

    EKEvent *event = [store_ eventWithIdentifier:std_to_ns(request.id)];
    if (event == nil) {
      return common::Result<CalendarEvent>::failure("event not found: " + request.id);
    }

    if (request.title.has_value()) {
      event.title = std_to_ns(*request.title);
    }
    if (request.start.has_value()) {
      NSDate *start = parse_date(*request.start);
      if (start == nil) {
        return common::Result<CalendarEvent>::failure("start must be an ISO date/time");
      }
      event.startDate = start;
    }
    if (request.end.has_value()) {
      NSDate *end = parse_date(*request.end);
      if (end == nil) {
        return common::Result<CalendarEvent>::failure("end must be an ISO date/time");
      }
      event.endDate = end;
    }
    if ([event.endDate compare:event.startDate] != NSOrderedDescending) {
      return common::Result<CalendarEvent>::failure("end must be after start");
    }
    if (request.location.has_value()) {
      event.location = std_to_ns(*request.location);
    }
    if (request.notes.has_value()) {
      event.notes = std_to_ns(*request.notes);
    }

    NSError *error = nil;
    if (![store_ saveEvent:event span:EKSpanThisEvent commit:YES error:&error]) {
      return common::Result<CalendarEvent>::failure(
          "failed to update calendar event: " + ns_to_std([error localizedDescription]));
    }
    return common::Result<CalendarEvent>::success(to_calendar_event(event));
  }

  [[nodiscard]] common::Result<bool> delete_event(const std::string &event_id) override {
    if (!ensure_access()) {
      return common::Result<bool>::failure(
          "calendar access denied; grant Calendar access to GhostClaw in macOS settings");
    }
    if (common::trim(event_id).empty()) {
      return common::Result<bool>::failure("event_id is required");
    }

    EKEvent *event = [store_ eventWithIdentifier:std_to_ns(event_id)];
    if (event == nil) {
      return common::Result<bool>::success(false);
    }
    NSError *error = nil;
    if (![store_ removeEvent:event span:EKSpanThisEvent commit:YES error:&error]) {
      return common::Result<bool>::failure(
          "failed to delete calendar event: " + ns_to_std([error localizedDescription]));
    }
    return common::Result<bool>::success(true);
  }

private:
  [[nodiscard]] bool ensure_access() {
    if (authorized_) {
      return true;
    }
    EKAuthorizationStatus status = [EKEventStore authorizationStatusForEntityType:EKEntityTypeEvent];
    if (status == EKAuthorizationStatusAuthorized) {
      authorized_ = true;
      return true;
    }
    if (status == EKAuthorizationStatusDenied || status == EKAuthorizationStatusRestricted) {
      return false;
    }

    __block bool granted = false;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    [store_ requestAccessToEntityType:EKEntityTypeEvent
                           completion:^(BOOL access_granted, NSError *) {
                             granted = access_granted;
                             dispatch_semaphore_signal(semaphore);
                           }];
    (void)dispatch_semaphore_wait(
        semaphore, dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(10 * NSEC_PER_SEC)));
    authorized_ = granted;
    return granted;
  }

  EKEventStore *store_ = nil;
  bool authorized_ = false;
};

} // namespace

std::unique_ptr<ICalendarBackend> make_eventkit_calendar_backend() {
  return std::make_unique<EventKitCalendarBackend>();
}

} // namespace ghostclaw::calendar

#endif
