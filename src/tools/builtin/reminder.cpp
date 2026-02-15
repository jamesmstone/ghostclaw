#include "ghostclaw/tools/builtin/reminder.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"
#include "ghostclaw/heartbeat/cron.hpp"
#include "ghostclaw/heartbeat/cron_store.hpp"

#include <chrono>
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

std::filesystem::path resolve_cron_db_path(const ToolContext &ctx) {
  if (!ctx.workspace_path.empty()) {
    return ctx.workspace_path / "cron" / "jobs.db";
  }
  return std::filesystem::temp_directory_path() / "ghostclaw-reminders-jobs.db";
}

std::string make_reminder_payload(const std::string &channel, const std::string &to,
                                  const std::string &text, const std::string &id) {
  std::ostringstream out;
  out << "{\"kind\":\"channel_message\",\"channel\":\"" << common::json_escape(channel)
      << "\",\"to\":\"" << common::json_escape(to) << "\",\"text\":\""
      << common::json_escape(text) << "\",\"id\":\"" << common::json_escape(id) << "\"}";
  return out.str();
}

} // namespace

ReminderTool::ReminderTool(config::Config config) : config_(std::move(config)) {}

std::string_view ReminderTool::name() const { return "reminder"; }

std::string_view ReminderTool::description() const {
  return "Schedule, list, and cancel channel reminders";
}

std::string ReminderTool::parameters_schema() const {
  return R"({"type":"object","required":["action"],"properties":{"action":{"type":"string","enum":["schedule","list","cancel"]},"id":{"type":"string"},"expression":{"type":"string"},"channel":{"type":"string"},"to":{"type":"string"},"text":{"type":"string"},"confirm":{"type":"string"}}})";
}

common::Result<ToolResult> ReminderTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  const std::string action = common::to_lower(optional_arg(args, "action"));
  if (action.empty()) {
    return common::Result<ToolResult>::failure("action is required");
  }

  heartbeat::CronStore store(resolve_cron_db_path(ctx));

  if (action == "list") {
    auto listed = store.list_jobs();
    if (!listed.ok()) {
      return common::Result<ToolResult>::failure(listed.error());
    }

    std::ostringstream out;
    std::size_t count = 0;
    for (const auto &job : listed.value()) {
      const auto payload = common::json_parse_flat(job.command);
      const auto kind_it = payload.find("kind");
      if (kind_it == payload.end() || common::to_lower(kind_it->second) != "channel_message") {
        continue;
      }
      const auto channel_it = payload.find("channel");
      const auto to_it = payload.find("to");
      const auto text_it = payload.find("text");
      if (channel_it == payload.end() || to_it == payload.end() || text_it == payload.end()) {
        continue;
      }
      out << "- " << job.id << " | " << job.expression << " | "
          << channel_it->second << " -> " << to_it->second << " | " << text_it->second << "\n";
      ++count;
    }
    ToolResult result;
    result.output = out.str();
    result.metadata["count"] = std::to_string(count);
    return common::Result<ToolResult>::success(std::move(result));
  }

  if (action == "schedule") {
    const std::string expression = optional_arg(args, "expression");
    if (expression.empty()) {
      return common::Result<ToolResult>::failure("expression is required");
    }
    const auto parsed = heartbeat::CronExpression::parse(expression);
    if (!parsed.ok()) {
      return common::Result<ToolResult>::failure(parsed.error());
    }

    std::string channel = optional_arg(args, "channel");
    if (channel.empty()) {
      channel = common::trim(config_.reminders.default_channel);
    }
    if (channel.empty()) {
      channel = common::trim(ctx.channel_id);
    }
    const std::string to = optional_arg(args, "to");
    const std::string text = optional_arg(args, "text");
    if (channel.empty()) {
      return common::Result<ToolResult>::failure("channel is required");
    }
    if (to.empty()) {
      return common::Result<ToolResult>::failure("to is required");
    }
    if (text.empty()) {
      return common::Result<ToolResult>::failure("text is required");
    }

    std::string id = optional_arg(args, "id");
    if (id.empty()) {
      const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
      id = "reminder-" + std::to_string(now);
    }

    if (!parse_bool(args, "confirm", false)) {
      return confirmation_required("schedule",
                                   "Schedule reminder " + id + " with expression '" + expression + "'");
    }

    heartbeat::CronJob job;
    job.id = id;
    job.expression = expression;
    job.command = make_reminder_payload(channel, to, text, id);
    job.next_run = parsed.value().next_occurrence();

    auto added = store.add_job(job);
    if (!added.ok()) {
      return common::Result<ToolResult>::failure(added.error());
    }

    ToolResult result;
    result.output = "Scheduled reminder: " + id;
    result.metadata["id"] = id;
    result.metadata["expression"] = expression;
    return common::Result<ToolResult>::success(std::move(result));
  }

  if (action == "cancel") {
    const std::string id = optional_arg(args, "id");
    if (id.empty()) {
      return common::Result<ToolResult>::failure("id is required");
    }
    if (!parse_bool(args, "confirm", false)) {
      return confirmation_required("cancel", "Cancel reminder " + id);
    }

    auto removed = store.remove_job(id);
    if (!removed.ok()) {
      return common::Result<ToolResult>::failure(removed.error());
    }
    ToolResult result;
    result.output = removed.value() ? "Cancelled reminder: " + id : "Reminder not found: " + id;
    result.metadata["id"] = id;
    return common::Result<ToolResult>::success(std::move(result));
  }

  return common::Result<ToolResult>::failure("Unsupported action: " + action);
}

bool ReminderTool::is_safe() const { return false; }

std::string_view ReminderTool::group() const { return "messaging"; }

} // namespace ghostclaw::tools
