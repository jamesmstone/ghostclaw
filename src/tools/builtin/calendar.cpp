#include "ghostclaw/tools/builtin/calendar.hpp"

#include "ghostclaw/calendar/backend.hpp"
#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

#include <sstream>

namespace ghostclaw::tools {

namespace {

bool parse_bool(const ToolArgs &args, const std::string &key, const bool fallback = false) {
  const auto it = args.find(key);
  if (it == args.end()) {
    return fallback;
  }
  const std::string value = common::to_lower(common::trim(it->second));
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return fallback;
}

std::string optional_arg(const ToolArgs &args, const std::string &key,
                         const std::string &fallback = "") {
  const auto it = args.find(key);
  if (it == args.end()) {
    return fallback;
  }
  return common::trim(it->second);
}

common::Result<ToolResult> confirmation_required(const std::string &action,
                                                 const std::string &summary) {
  ToolResult out;
  out.success = true;
  out.metadata["requires_confirmation"] = "true";
  out.metadata["action"] = action;
  out.output = "{\"ok\":false,\"requires_confirmation\":true,\"action\":\"" + action +
               "\",\"preview\":\"" + common::json_escape(summary) +
               "\",\"next\":\"re-run with confirm=true\"}";
  return common::Result<ToolResult>::success(std::move(out));
}

} // namespace

CalendarTool::CalendarTool(config::Config config) : config_(std::move(config)) {}

std::string_view CalendarTool::name() const { return "calendar"; }

std::string_view CalendarTool::description() const {
  return "Manage calendars and events (list/create/update/delete)";
}

std::string CalendarTool::parameters_schema() const {
  return R"({"type":"object","required":["action"],"properties":{"action":{"type":"string","enum":["list_calendars","list_events","create_event","update_event","delete_event"]},"calendar":{"type":"string"},"id":{"type":"string"},"title":{"type":"string"},"start":{"type":"string"},"end":{"type":"string"},"location":{"type":"string"},"notes":{"type":"string"},"confirm":{"type":"string"}}})";
}

common::Result<ToolResult> CalendarTool::execute(const ToolArgs &args, const ToolContext &) {
  const std::string action = common::to_lower(optional_arg(args, "action"));
  if (action.empty()) {
    return common::Result<ToolResult>::failure("action is required");
  }

  auto backend = calendar::make_calendar_backend(config_);

  if (action == "list_calendars") {
    auto listed = backend->list_calendars();
    if (!listed.ok()) {
      return common::Result<ToolResult>::failure(listed.error());
    }
    ToolResult out;
    std::ostringstream text;
    for (const auto &entry : listed.value()) {
      text << "- " << entry.title << " (" << entry.id << ")\n";
    }
    out.output = text.str();
    out.metadata["backend"] = std::string(backend->name());
    out.metadata["count"] = std::to_string(listed.value().size());
    return common::Result<ToolResult>::success(std::move(out));
  }

  if (action == "list_events") {
    std::string calendar_name = optional_arg(args, "calendar");
    if (calendar_name.empty()) {
      calendar_name = config_.calendar.default_calendar;
    }
    auto listed =
        backend->list_events(calendar_name, optional_arg(args, "start"), optional_arg(args, "end"));
    if (!listed.ok()) {
      return common::Result<ToolResult>::failure(listed.error());
    }
    ToolResult out;
    std::ostringstream text;
    for (const auto &event : listed.value()) {
      text << "- " << event.title << " [" << event.start << " -> " << event.end << "] id="
           << event.id << "\n";
    }
    out.output = text.str();
    out.metadata["backend"] = std::string(backend->name());
    out.metadata["count"] = std::to_string(listed.value().size());
    return common::Result<ToolResult>::success(std::move(out));
  }

  if (action == "create_event") {
    calendar::EventWriteRequest request;
    request.calendar = optional_arg(args, "calendar", config_.calendar.default_calendar);
    request.title = optional_arg(args, "title");
    request.start = optional_arg(args, "start");
    request.end = optional_arg(args, "end");
    request.location = optional_arg(args, "location");
    request.notes = optional_arg(args, "notes");
    if (!parse_bool(args, "confirm", false)) {
      return confirmation_required("create_event",
                                   "Create event '" + request.title + "' at " + request.start);
    }

    auto created = backend->create_event(request);
    if (!created.ok()) {
      return common::Result<ToolResult>::failure(created.error());
    }

    ToolResult out;
    out.output = "Created calendar event: " + created.value().title + " (id=" + created.value().id +
                 ")";
    out.metadata["id"] = created.value().id;
    out.metadata["backend"] = std::string(backend->name());
    return common::Result<ToolResult>::success(std::move(out));
  }

  if (action == "update_event") {
    calendar::EventUpdateRequest request;
    request.id = optional_arg(args, "id", optional_arg(args, "event_id"));
    if (request.id.empty()) {
      return common::Result<ToolResult>::failure("id is required");
    }
    if (args.contains("title")) {
      request.title = optional_arg(args, "title");
    }
    if (args.contains("start")) {
      request.start = optional_arg(args, "start");
    }
    if (args.contains("end")) {
      request.end = optional_arg(args, "end");
    }
    if (args.contains("location")) {
      request.location = optional_arg(args, "location");
    }
    if (args.contains("notes")) {
      request.notes = optional_arg(args, "notes");
    }
    if (!parse_bool(args, "confirm", false)) {
      return confirmation_required("update_event", "Update event id=" + request.id);
    }

    auto updated = backend->update_event(request);
    if (!updated.ok()) {
      return common::Result<ToolResult>::failure(updated.error());
    }

    ToolResult out;
    out.output = "Updated calendar event: " + updated.value().title + " (id=" + updated.value().id +
                 ")";
    out.metadata["id"] = updated.value().id;
    out.metadata["backend"] = std::string(backend->name());
    return common::Result<ToolResult>::success(std::move(out));
  }

  if (action == "delete_event") {
    const std::string id = optional_arg(args, "id", optional_arg(args, "event_id"));
    if (id.empty()) {
      return common::Result<ToolResult>::failure("id is required");
    }
    if (!parse_bool(args, "confirm", false)) {
      return confirmation_required("delete_event", "Delete event id=" + id);
    }

    auto removed = backend->delete_event(id);
    if (!removed.ok()) {
      return common::Result<ToolResult>::failure(removed.error());
    }

    ToolResult out;
    out.output = removed.value() ? "Deleted calendar event: " + id : "Calendar event not found: " + id;
    out.metadata["backend"] = std::string(backend->name());
    return common::Result<ToolResult>::success(std::move(out));
  }

  return common::Result<ToolResult>::failure("Unsupported action: " + action);
}

bool CalendarTool::is_safe() const { return false; }

std::string_view CalendarTool::group() const { return "calendar"; }

} // namespace ghostclaw::tools
