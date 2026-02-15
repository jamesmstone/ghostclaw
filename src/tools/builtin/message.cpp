#include "ghostclaw/tools/builtin/message.hpp"

#include "ghostclaw/channels/send_service.hpp"
#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"

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

common::Result<ToolResult> confirmation_required(const std::string &summary) {
  ToolResult out;
  out.success = true;
  out.metadata["requires_confirmation"] = "true";
  out.metadata["action"] = "send";
  out.output = "{\"ok\":false,\"requires_confirmation\":true,\"action\":\"send\",\"preview\":\"" +
               common::json_escape(summary) + "\",\"next\":\"re-run with confirm=true\"}";
  return common::Result<ToolResult>::success(std::move(out));
}

} // namespace

MessageTool::MessageTool(config::Config config) : config_(std::move(config)) {}

std::string_view MessageTool::name() const { return "message"; }

std::string_view MessageTool::description() const { return "Send outbound channel messages"; }

std::string MessageTool::parameters_schema() const {
  return R"({"type":"object","properties":{"action":{"type":"string","enum":["send"]},"channel":{"type":"string"},"to":{"type":"string"},"recipient":{"type":"string"},"text":{"type":"string"},"confirm":{"type":"string"}}})";
}

common::Result<ToolResult> MessageTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  std::string action = common::to_lower(optional_arg(args, "action", "send"));
  if (action.empty()) {
    action = "send";
  }
  if (action != "send") {
    return common::Result<ToolResult>::failure("Unsupported action: " + action);
  }

  std::string channel = optional_arg(args, "channel");
  if (channel.empty()) {
    channel = common::trim(ctx.channel_id);
  }
  if (channel.empty()) {
    channel = common::trim(config_.reminders.default_channel);
  }
  std::string recipient = optional_arg(args, "to");
  if (recipient.empty()) {
    recipient = optional_arg(args, "recipient");
  }
  const std::string text = optional_arg(args, "text", optional_arg(args, "message"));

  if (channel.empty()) {
    return common::Result<ToolResult>::failure("channel is required");
  }
  if (recipient.empty()) {
    return common::Result<ToolResult>::failure("to is required");
  }
  if (text.empty()) {
    return common::Result<ToolResult>::failure("text is required");
  }

  if (!parse_bool(args, "confirm", false)) {
    return confirmation_required("Send message to " + recipient + " on " + channel);
  }

  channels::SendService sender(config_);
  auto sent = sender.send({.channel = channel, .recipient = recipient, .text = text});
  if (!sent.ok()) {
    return common::Result<ToolResult>::failure(sent.error());
  }

  ToolResult result;
  result.output = "Message sent to " + recipient + " on " + channel;
  result.metadata["channel"] = channel;
  result.metadata["to"] = recipient;
  return common::Result<ToolResult>::success(std::move(result));
}

bool MessageTool::is_safe() const { return false; }

std::string_view MessageTool::group() const { return "messaging"; }

} // namespace ghostclaw::tools
