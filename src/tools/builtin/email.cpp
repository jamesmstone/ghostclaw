#include "ghostclaw/tools/builtin/email.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"
#include "ghostclaw/email/backend.hpp"

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

EmailTool::EmailTool(config::Config config) : config_(std::move(config)) {}

std::string_view EmailTool::name() const { return "email"; }

std::string_view EmailTool::description() const {
  return "List accounts, draft, and send emails";
}

std::string EmailTool::parameters_schema() const {
  return R"({"type":"object","required":["action"],"properties":{"action":{"type":"string","enum":["list_accounts","draft","send"]},"to":{"type":"string"},"subject":{"type":"string"},"body":{"type":"string"},"account":{"type":"string"},"confirm":{"type":"string"}}})";
}

common::Result<ToolResult> EmailTool::execute(const ToolArgs &args, const ToolContext &) {
  const std::string action = common::to_lower(optional_arg(args, "action"));
  if (action.empty()) {
    return common::Result<ToolResult>::failure("action is required");
  }

  auto backend = email::make_email_backend(config_);

  if (action == "list_accounts") {
    auto listed = backend->list_accounts();
    if (!listed.ok()) {
      return common::Result<ToolResult>::failure(listed.error());
    }
    ToolResult out;
    std::ostringstream text;
    for (const auto &account : listed.value()) {
      text << "- " << account.label << " (" << account.id << ")\n";
    }
    out.output = text.str();
    out.metadata["backend"] = std::string(backend->name());
    out.metadata["count"] = std::to_string(listed.value().size());
    return common::Result<ToolResult>::success(std::move(out));
  }

  email::EmailMessage message;
  message.to = optional_arg(args, "to");
  message.subject = optional_arg(args, "subject");
  message.body = optional_arg(args, "body");
  message.from_account = optional_arg(args, "account", config_.email.default_account);

  if (action == "draft") {
    auto drafted = backend->draft(message);
    if (!drafted.ok()) {
      return common::Result<ToolResult>::failure(drafted.error());
    }
    ToolResult out;
    out.output = drafted.value();
    out.metadata["backend"] = std::string(backend->name());
    return common::Result<ToolResult>::success(std::move(out));
  }

  if (action == "send") {
    if (!parse_bool(args, "confirm", false)) {
      return confirmation_required("send", "Send email to " + message.to + " subject '" +
                                               message.subject + "'");
    }
    auto sent = backend->send(message);
    if (!sent.ok()) {
      return common::Result<ToolResult>::failure(sent.error());
    }
    ToolResult out;
    out.output = "Email sent to " + message.to;
    out.metadata["backend"] = std::string(backend->name());
    return common::Result<ToolResult>::success(std::move(out));
  }

  return common::Result<ToolResult>::failure("Unsupported action: " + action);
}

bool EmailTool::is_safe() const { return false; }

std::string_view EmailTool::group() const { return "messaging"; }

} // namespace ghostclaw::tools
