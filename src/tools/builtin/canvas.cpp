#include "ghostclaw/tools/builtin/canvas.hpp"

#include "ghostclaw/common/fs.hpp"

#include <limits>
#include <sstream>

namespace ghostclaw::tools {

namespace {

constexpr std::size_t kDefaultSnapshotLimitBytes = 256U * 1024U;

std::string read_arg(const ToolArgs &args, const std::string &name) {
  const auto it = args.find(name);
  if (it == args.end()) {
    return "";
  }
  return it->second;
}

std::string read_first_arg(const ToolArgs &args, const std::initializer_list<const char *> names) {
  for (const char *name : names) {
    const auto it = args.find(name);
    if (it != args.end() && !common::trim(it->second).empty()) {
      return it->second;
    }
  }
  return "";
}

common::Result<std::size_t> parse_limit_arg(const ToolArgs &args, const std::string &name,
                                            const std::size_t fallback) {
  const auto it = args.find(name);
  if (it == args.end() || common::trim(it->second).empty()) {
    return common::Result<std::size_t>::success(fallback);
  }

  try {
    const auto parsed = std::stoull(it->second);
    if (parsed == 0U) {
      return common::Result<std::size_t>::failure(name + " must be > 0");
    }
    if (parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
      return common::Result<std::size_t>::failure(name + " is too large");
    }
    return common::Result<std::size_t>::success(static_cast<std::size_t>(parsed));
  } catch (...) {
    return common::Result<std::size_t>::failure("invalid " + name);
  }
}

std::string build_react_html(const std::string &component_expression, const std::string &props_json) {
  const std::string props = common::trim(props_json).empty() ? "{}" : props_json;

  std::ostringstream out;
  out << "<!doctype html><html><head><meta charset=\"utf-8\"/>"
      << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"/>"
      << "<title>GhostClaw Canvas</title>"
      << "<script crossorigin src=\"https://unpkg.com/react@18/umd/react.production.min.js\"></script>"
      << "<script crossorigin src=\"https://unpkg.com/react-dom@18/umd/react-dom.production.min.js\"></script>"
      << "</head><body><div id=\"root\"></div><script>"
      << "const __ghostclawProps=" << props << ";"
      << "const __ghostclawNode=(" << component_expression << ");"
      << "ReactDOM.createRoot(document.getElementById('root')).render(__ghostclawNode);"
      << "</script></body></html>";
  return out.str();
}

} // namespace

std::string_view CanvasTool::name() const { return "canvas"; }

std::string_view CanvasTool::description() const {
  return "Agent-driven visual canvas host (HTML/React push, JS eval, snapshot, reset)";
}

std::string CanvasTool::parameters_schema() const {
  return R"({"type":"object","required":["action"],"properties":{"action":{"type":"string","enum":["push","present","eval","snapshot","reset"]},"html":{"type":"string"},"content":{"type":"string"},"component":{"type":"string"},"react_component":{"type":"string"},"props":{"type":"string"},"js":{"type":"string"},"javaScript":{"type":"string"},"script":{"type":"string"},"max_snapshot_bytes":{"type":"string"}}})";
}

common::Result<ToolResult> CanvasTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  const auto action_it = args.find("action");
  if (action_it == args.end() || common::trim(action_it->second).empty()) {
    return common::Result<ToolResult>::failure("Missing action");
  }

  const std::string action = common::to_lower(common::trim(action_it->second));
  canvas::CanvasHost &host = host_for_session(ctx);

  if (action == "push" || action == "present") {
    std::string html = read_first_arg(args, {"html", "content"});
    if (common::trim(html).empty()) {
      const std::string component = read_first_arg(args, {"component", "react_component"});
      if (common::trim(component).empty()) {
        return common::Result<ToolResult>::failure("Missing html/content or component");
      }
      html = build_react_html(component, read_arg(args, "props"));
    }

    const auto pushed = host.push(html);
    if (!pushed.ok()) {
      return common::Result<ToolResult>::failure(pushed.error());
    }

    ToolResult result;
    result.output = "Canvas updated";
    result.metadata["action"] = "push";
    result.metadata["bytes"] = std::to_string(html.size());
    return common::Result<ToolResult>::success(std::move(result));
  }

  if (action == "eval") {
    const std::string js = read_first_arg(args, {"js", "javaScript", "script"});
    if (common::trim(js).empty()) {
      return common::Result<ToolResult>::failure("Missing js/javaScript/script");
    }

    const auto evaluated = host.eval(js);
    if (!evaluated.ok()) {
      return common::Result<ToolResult>::failure(evaluated.error());
    }

    ToolResult result;
    result.output = "Canvas script executed";
    result.metadata["action"] = "eval";
    result.metadata["bytes"] = std::to_string(js.size());
    return common::Result<ToolResult>::success(std::move(result));
  }

  if (action == "snapshot") {
    const auto limit = parse_limit_arg(args, "max_snapshot_bytes", kDefaultSnapshotLimitBytes);
    if (!limit.ok()) {
      return common::Result<ToolResult>::failure(limit.error());
    }

    const auto snap = host.snapshot();
    if (!snap.ok()) {
      return common::Result<ToolResult>::failure(snap.error());
    }

    ToolResult result;
    result.output = snap.value();
    result.metadata["action"] = "snapshot";
    result.metadata["bytes"] = std::to_string(result.output.size());
    if (result.output.size() > limit.value()) {
      result.output.resize(limit.value());
      result.output.append("\n...[truncated]");
      result.truncated = true;
      result.metadata["truncated"] = "true";
    }
    return common::Result<ToolResult>::success(std::move(result));
  }

  if (action == "reset") {
    const auto reset = host.reset();
    if (!reset.ok()) {
      return common::Result<ToolResult>::failure(reset.error());
    }

    ToolResult result;
    result.output = "Canvas reset";
    result.metadata["action"] = "reset";
    return common::Result<ToolResult>::success(std::move(result));
  }

  return common::Result<ToolResult>::failure("Unknown canvas action: " + action);
}

bool CanvasTool::is_safe() const { return false; }

std::string_view CanvasTool::group() const { return "ui"; }

canvas::CanvasHost &CanvasTool::host_for_session(const ToolContext &ctx) {
  const std::string session_id = common::trim(ctx.session_id);
  const std::string key = session_id.empty() ? "__default__" : session_id;

  std::lock_guard<std::mutex> lock(hosts_mutex_);
  auto &entry = hosts_[key];
  if (!entry) {
    entry = std::make_unique<canvas::CanvasHost>();
  }
  return *entry;
}

} // namespace ghostclaw::tools
