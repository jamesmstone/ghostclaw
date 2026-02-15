#include "ghostclaw/nodes/node.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/memory/memory.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ghostclaw::nodes {

namespace {

constexpr std::size_t kMaxOutputBytes = 1024 * 1024;

std::string random_hex(const std::size_t bytes = 8) {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  std::ostringstream out;
  for (std::size_t i = 0; i < bytes; ++i) {
    const auto value = static_cast<unsigned>(rng() & 0xFFULL);
    out << "0123456789abcdef"[value >> 4U] << "0123456789abcdef"[value & 0x0FU];
  }
  return out.str();
}

common::Result<std::string> required_arg(const tools::ToolArgs &args, const std::string &name) {
  const auto it = args.find(name);
  if (it == args.end()) {
    return common::Result<std::string>::failure("Missing argument: " + name);
  }
  const std::string value = common::trim(it->second);
  if (value.empty()) {
    return common::Result<std::string>::failure("Missing argument: " + name);
  }
  return common::Result<std::string>::success(value);
}

std::optional<std::string> optional_arg(const tools::ToolArgs &args, const std::string &name) {
  const auto it = args.find(name);
  if (it == args.end()) {
    return std::nullopt;
  }
  const std::string value = common::trim(it->second);
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

std::uint64_t parse_timeout_ms(const tools::ToolArgs &args, const std::uint64_t fallback,
                               const std::uint64_t max_value = 120'000ULL) {
  const auto value = optional_arg(args, "timeout_ms");
  if (!value.has_value()) {
    return fallback;
  }
  try {
    const auto parsed = static_cast<std::uint64_t>(std::stoull(*value));
    return std::min(parsed, max_value);
  } catch (...) {
    return fallback;
  }
}

void dedupe_and_sort(std::vector<std::string> *values) {
  if (values == nullptr) {
    return;
  }
  for (auto &value : *values) {
    value = common::trim(value);
  }
  values->erase(std::remove_if(values->begin(), values->end(),
                               [](const std::string &value) { return value.empty(); }),
                values->end());
  std::sort(values->begin(), values->end());
  values->erase(std::unique(values->begin(), values->end()), values->end());
}

std::string shell_quote(const std::string &value) {
  std::string out = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('\'');
  return out;
}

bool command_exists(const std::string &command) {
  const char *path_raw = std::getenv("PATH");
  if (path_raw == nullptr || *path_raw == '\0') {
    return false;
  }
  const std::string path_env(path_raw);
  std::stringstream stream(path_env);
  std::string dir;
  while (std::getline(stream, dir, ':')) {
    if (dir.empty()) {
      continue;
    }
    std::error_code ec;
    const auto candidate = std::filesystem::path(dir) / command;
    if (std::filesystem::exists(candidate, ec) && !ec) {
      return true;
    }
  }
  return false;
}

common::Result<NodeActionResult>
run_system_command(const std::shared_ptr<security::SecurityPolicy> &policy,
                   const tools::ToolArgs &args, const tools::ToolContext &ctx);

common::Result<NodeActionResult> run_unrestricted_command(const std::string &command,
                                                          const std::uint64_t timeout_ms,
                                                          const tools::ToolContext &ctx) {
  tools::ToolArgs args;
  args["command"] = command;
  args["timeout_ms"] = std::to_string(timeout_ms);
  return run_system_command(nullptr, args, ctx);
}

common::Result<NodeActionResult>
run_system_command(const std::shared_ptr<security::SecurityPolicy> &policy,
                   const tools::ToolArgs &args, const tools::ToolContext &ctx) {
  auto command = required_arg(args, "command");
  if (!command.ok()) {
    return common::Result<NodeActionResult>::failure(command.error());
  }

  if (policy != nullptr) {
    if (!policy->is_command_allowed(command.value())) {
      return common::Result<NodeActionResult>::failure("Command not allowed by policy");
    }
    if (!policy->check_rate_limit()) {
      return common::Result<NodeActionResult>::failure("Rate limit exceeded");
    }
  }

#ifdef _WIN32
  (void)ctx;
  NodeActionResult result;
  result.success = false;
  result.output = "system.run is not implemented on Windows";
  return common::Result<NodeActionResult>::success(std::move(result));
#else
  int pipefd[2] = {-1, -1};
  if (pipe(pipefd) != 0) {
    return common::Result<NodeActionResult>::failure("Failed to create pipe");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return common::Result<NodeActionResult>::failure("Failed to fork");
  }

  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    if (!ctx.workspace_path.empty()) {
      (void)chdir(ctx.workspace_path.c_str());
    }

    execl("/bin/sh", "sh", "-c", command.value().c_str(), static_cast<char *>(nullptr));
    _exit(127);
  }

  close(pipefd[1]);
  const int flags = fcntl(pipefd[0], F_GETFL, 0);
  fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

  std::string output;
  output.reserve(4096);
  bool truncated = false;
  bool timeout = false;
  int status = 0;
  bool running = true;

  const auto timeout_ms = parse_timeout_ms(args, 20'000ULL);
  const auto start = std::chrono::steady_clock::now();

  while (running) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
                                       .count());
    if (elapsed > timeout_ms) {
      kill(pid, SIGKILL);
      timeout = true;
      running = false;
      break;
    }

    struct pollfd pfd {
      .fd = pipefd[0], .events = POLLIN, .revents = 0,
    };
    (void)poll(&pfd, 1, 50);

    std::array<char, 4096> buffer{};
    const ssize_t bytes = read(pipefd[0], buffer.data(), buffer.size());
    if (bytes > 0) {
      const std::size_t remaining =
          kMaxOutputBytes > output.size() ? kMaxOutputBytes - output.size() : 0;
      const std::size_t to_copy = std::min<std::size_t>(remaining, static_cast<std::size_t>(bytes));
      output.append(buffer.data(), to_copy);
      if (to_copy < static_cast<std::size_t>(bytes) || output.size() >= kMaxOutputBytes) {
        truncated = true;
      }
    }

    const pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid) {
      running = false;
    }
  }

  std::array<char, 4096> tail{};
  while (true) {
    const ssize_t bytes = read(pipefd[0], tail.data(), tail.size());
    if (bytes <= 0) {
      break;
    }
    const std::size_t remaining = kMaxOutputBytes > output.size() ? kMaxOutputBytes - output.size() : 0;
    const std::size_t to_copy = std::min<std::size_t>(remaining, static_cast<std::size_t>(bytes));
    output.append(tail.data(), to_copy);
    if (to_copy < static_cast<std::size_t>(bytes) || output.size() >= kMaxOutputBytes) {
      truncated = true;
    }
  }

  close(pipefd[0]);
  waitpid(pid, &status, 0);

  if (policy != nullptr) {
    policy->record_action();
  }

  NodeActionResult result;
  result.truncated = truncated;
  result.output = output;
  if (truncated) {
    result.output += "\n[output truncated]";
  }
  if (timeout) {
    result.success = false;
    result.output += "\n[command timed out]";
    result.metadata["exit_code"] = "timeout";
  } else {
    result.success = WIFEXITED(status) && (WEXITSTATUS(status) == 0);
    result.metadata["exit_code"] =
        WIFEXITED(status) ? std::to_string(WEXITSTATUS(status)) : "signal";
  }
  return common::Result<NodeActionResult>::success(std::move(result));
#endif
}

NodeActionResult unsupported_action_result(const std::string &action) {
  NodeActionResult result;
  result.success = false;
  result.output = action + " is not supported on this node";
  result.metadata["error"] = "unsupported_action";
  return result;
}

} // namespace

common::Status NodeRegistry::advertise(NodeDescriptor descriptor) {
  descriptor.node_id = common::trim(descriptor.node_id);
  if (descriptor.node_id.empty()) {
    return common::Status::error("node_id is required");
  }
  descriptor.display_name = common::trim(descriptor.display_name);
  descriptor.endpoint = common::trim(descriptor.endpoint);
  descriptor.updated_at = memory::now_rfc3339();
  dedupe_and_sort(&descriptor.capabilities);

  std::lock_guard<std::mutex> lock(mutex_);
  const auto existing = nodes_.find(descriptor.node_id);
  if (existing != nodes_.end()) {
    if (descriptor.pair_token.empty()) {
      descriptor.pair_token = existing->second.pair_token;
      descriptor.paired = existing->second.paired;
    }
  }
  nodes_[descriptor.node_id] = std::move(descriptor);
  return common::Status::success();
}

common::Result<NodeDescriptor> NodeRegistry::get(const std::string_view node_id) const {
  const std::string key = common::trim(std::string(node_id));
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = nodes_.find(key);
  if (it == nodes_.end()) {
    return common::Result<NodeDescriptor>::failure("node not found");
  }
  return common::Result<NodeDescriptor>::success(it->second);
}

std::vector<NodeDescriptor> NodeRegistry::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<NodeDescriptor> out;
  out.reserve(nodes_.size());
  for (const auto &[node_id, descriptor] : nodes_) {
    (void)node_id;
    out.push_back(descriptor);
  }
  std::sort(out.begin(), out.end(), [](const NodeDescriptor &a, const NodeDescriptor &b) {
    return a.updated_at > b.updated_at;
  });
  return out;
}

common::Result<PairingRequest>
NodeRegistry::create_pairing_request(const std::string &node_id,
                                     std::vector<std::string> requested_capabilities) {
  const std::string normalized_node = common::trim(node_id);
  if (normalized_node.empty()) {
    return common::Result<PairingRequest>::failure("node_id is required");
  }
  dedupe_and_sort(&requested_capabilities);

  std::lock_guard<std::mutex> lock(mutex_);
  if (!nodes_.contains(normalized_node)) {
    nodes_[normalized_node] = NodeDescriptor{
        .node_id = normalized_node,
        .display_name = normalized_node,
        .updated_at = memory::now_rfc3339(),
    };
  }

  PairingRequest request;
  request.request_id = "pair-" + random_hex(8);
  request.node_id = normalized_node;
  request.requested_capabilities = std::move(requested_capabilities);
  request.created_at = memory::now_rfc3339();
  pending_[request.request_id] = request;
  return common::Result<PairingRequest>::success(std::move(request));
}

std::vector<PairingRequest> NodeRegistry::pending_pairings() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PairingRequest> out;
  out.reserve(pending_.size());
  for (const auto &[request_id, request] : pending_) {
    (void)request_id;
    out.push_back(request);
  }
  std::sort(out.begin(), out.end(),
            [](const PairingRequest &a, const PairingRequest &b) { return a.created_at > b.created_at; });
  return out;
}

common::Result<NodeDescriptor> NodeRegistry::approve_pairing(const std::string_view request_id,
                                                             const std::string &token) {
  const std::string request_key = common::trim(std::string(request_id));
  const std::string token_value = common::trim(token);
  if (request_key.empty()) {
    return common::Result<NodeDescriptor>::failure("request_id is required");
  }
  if (token_value.empty()) {
    return common::Result<NodeDescriptor>::failure("token is required");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const auto pending_it = pending_.find(request_key);
  if (pending_it == pending_.end()) {
    return common::Result<NodeDescriptor>::failure("pairing request not found");
  }

  auto node_it = nodes_.find(pending_it->second.node_id);
  if (node_it == nodes_.end()) {
    return common::Result<NodeDescriptor>::failure("node not found");
  }

  node_it->second.paired = true;
  node_it->second.connected = true;
  node_it->second.pair_token = token_value;
  for (const auto &capability : pending_it->second.requested_capabilities) {
    node_it->second.capabilities.push_back(capability);
  }
  dedupe_and_sort(&node_it->second.capabilities);
  node_it->second.updated_at = memory::now_rfc3339();

  pending_.erase(pending_it);
  return common::Result<NodeDescriptor>::success(node_it->second);
}

common::Status NodeRegistry::reject_pairing(const std::string_view request_id) {
  const std::string request_key = common::trim(std::string(request_id));
  if (request_key.empty()) {
    return common::Status::error("request_id is required");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  pending_.erase(request_key);
  return common::Status::success();
}

NodeActionExecutor::NodeActionExecutor(std::shared_ptr<security::SecurityPolicy> policy)
    : policy_(std::move(policy)) {}

common::Result<NodeActionResult> NodeActionExecutor::invoke(std::string_view action,
                                                            const tools::ToolArgs &args,
                                                            const tools::ToolContext &ctx) const {
  const std::string normalized = common::to_lower(common::trim(std::string(action)));

  if (normalized == "system.run") {
    return run_system_command(policy_, args, ctx);
  }

  if (normalized == "system.notify") {
    const std::string title = optional_arg(args, "title").value_or("");
    const std::string body = optional_arg(args, "body").value_or("");
    if (title.empty() && body.empty()) {
      return common::Result<NodeActionResult>::failure("title or body is required");
    }

    auto escape_quotes = [](const std::string &value) {
      std::string escaped;
      escaped.reserve(value.size());
      for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
          escaped.push_back('\\');
        }
        escaped.push_back(ch);
      }
      return escaped;
    };

    std::string notify_command;
#if defined(__APPLE__)
    if (command_exists("osascript")) {
      const std::string safe_title = escape_quotes(title.empty() ? "GhostClaw" : title);
      const std::string safe_body = escape_quotes(body);
      const std::string script =
          "display notification \"" + safe_body + "\" with title \"" + safe_title + "\"";
      notify_command = "osascript -e " + shell_quote(script);
    }
#elif !defined(_WIN32)
    if (command_exists("notify-send")) {
      notify_command = "notify-send " + shell_quote(title.empty() ? "GhostClaw" : title) + " " +
                       shell_quote(body);
    }
#endif

    bool delivered = false;
    std::string delivery_error;
    if (!notify_command.empty()) {
      auto sent = run_unrestricted_command(notify_command, 5'000ULL, ctx);
      if (sent.ok() && sent.value().success) {
        delivered = true;
      } else if (sent.ok()) {
        delivery_error = sent.value().output;
      } else {
        delivery_error = sent.error();
      }
    }

    NodeActionResult result;
    result.success = true; // Best-effort: we still accept even when host notification is unavailable.
    result.output = delivered ? "notification delivered" : "notification queued";
    result.metadata["title"] = title;
    result.metadata["body"] = body;
    result.metadata["delivery"] = delivered ? "system" : "fallback";
    if (!delivery_error.empty()) {
      result.metadata["delivery_error"] = delivery_error;
    }
    return common::Result<NodeActionResult>::success(std::move(result));
  }

  if (normalized == "location.get") {
    const char *lat = std::getenv("GHOSTCLAW_GPS_LAT");
    const char *lon = std::getenv("GHOSTCLAW_GPS_LON");

    NodeActionResult result;
    if (lat != nullptr && lon != nullptr && *lat != '\0' && *lon != '\0') {
      result.success = true;
      result.output = std::string("{\"lat\":") + lat + ",\"lon\":" + lon + "}";
      result.metadata["provider"] = "env";
    } else {
      result.success = false;
      result.output = "location unavailable (set GHOSTCLAW_GPS_LAT/GHOSTCLAW_GPS_LON)";
      result.metadata["error"] = "location_unavailable";
    }
    return common::Result<NodeActionResult>::success(std::move(result));
  }

  if (normalized == "camera.snap") {
    const std::string out_path = optional_arg(args, "out_path")
                                     .value_or((std::filesystem::temp_directory_path() /
                                                ("ghostclaw-camera-snap-" + random_hex(6) + ".jpg"))
                                                   .string());
    std::string command;
#if defined(__APPLE__)
    if (command_exists("imagesnap")) {
      command = "imagesnap -q " + shell_quote(out_path);
    } else if (command_exists("ffmpeg")) {
      command = "ffmpeg -y -f avfoundation -framerate 30 -i \"0:none\" -frames:v 1 " +
                shell_quote(out_path);
    }
#elif !defined(_WIN32)
    if (command_exists("ffmpeg")) {
      command = "ffmpeg -y -f video4linux2 -i /dev/video0 -frames:v 1 " + shell_quote(out_path);
    }
#endif
    if (command.empty()) {
      return common::Result<NodeActionResult>::success(unsupported_action_result(normalized));
    }

    auto run = run_unrestricted_command(command, parse_timeout_ms(args, 20'000ULL), ctx);
    if (!run.ok()) {
      return common::Result<NodeActionResult>::failure(run.error());
    }
    auto result = run.value();
    if (result.success && !std::filesystem::exists(out_path)) {
      result.success = false;
      result.output = "camera.snap command finished without output file";
    } else if (result.success) {
      result.output = "{\"path\":\"" + out_path + "\"}";
      result.metadata["path"] = out_path;
    }
    return common::Result<NodeActionResult>::success(std::move(result));
  }

  if (normalized == "camera.clip") {
    std::uint64_t duration_ms = 3'000ULL;
    if (const auto requested = optional_arg(args, "duration_ms"); requested.has_value()) {
      try {
        duration_ms = std::max<std::uint64_t>(500ULL, std::stoull(*requested));
      } catch (...) {
        duration_ms = 3'000ULL;
      }
    }
    const double seconds = static_cast<double>(duration_ms) / 1000.0;
    const std::string out_path = optional_arg(args, "out_path")
                                     .value_or((std::filesystem::temp_directory_path() /
                                                ("ghostclaw-camera-clip-" + random_hex(6) + ".mp4"))
                                                   .string());

    std::ostringstream command;
#if defined(__APPLE__)
    if (!command_exists("ffmpeg")) {
      return common::Result<NodeActionResult>::success(unsupported_action_result(normalized));
    }
    command << "ffmpeg -y -f avfoundation -framerate 30 -i \"0:0\" -t " << seconds << " "
            << shell_quote(out_path);
#elif !defined(_WIN32)
    if (!command_exists("ffmpeg")) {
      return common::Result<NodeActionResult>::success(unsupported_action_result(normalized));
    }
    command << "ffmpeg -y -f video4linux2 -i /dev/video0 -t " << seconds << " "
            << shell_quote(out_path);
#else
    return common::Result<NodeActionResult>::success(unsupported_action_result(normalized));
#endif

    auto run = run_unrestricted_command(command.str(), parse_timeout_ms(args, 90'000ULL), ctx);
    if (!run.ok()) {
      return common::Result<NodeActionResult>::failure(run.error());
    }
    auto result = run.value();
    if (result.success && !std::filesystem::exists(out_path)) {
      result.success = false;
      result.output = "camera.clip command finished without output file";
    } else if (result.success) {
      result.output = "{\"path\":\"" + out_path + "\",\"duration_ms\":" +
                      std::to_string(duration_ms) + "}";
      result.metadata["path"] = out_path;
      result.metadata["duration_ms"] = std::to_string(duration_ms);
    }
    return common::Result<NodeActionResult>::success(std::move(result));
  }

  if (normalized == "screen.record") {
    std::uint64_t duration_ms = 10'000ULL;
    if (const auto requested = optional_arg(args, "duration_ms"); requested.has_value()) {
      try {
        duration_ms = std::max<std::uint64_t>(500ULL, std::stoull(*requested));
      } catch (...) {
        duration_ms = 10'000ULL;
      }
    }
    std::uint64_t fps = 10ULL;
    if (const auto requested = optional_arg(args, "fps"); requested.has_value()) {
      try {
        fps = std::clamp<std::uint64_t>(std::stoull(*requested), 1ULL, 60ULL);
      } catch (...) {
        fps = 10ULL;
      }
    }

    const double seconds = static_cast<double>(duration_ms) / 1000.0;
    const std::string out_path = optional_arg(args, "out_path")
                                     .value_or((std::filesystem::temp_directory_path() /
                                                ("ghostclaw-screen-record-" + random_hex(6) + ".mp4"))
                                                   .string());

    std::ostringstream command;
#if defined(__APPLE__)
    if (!command_exists("ffmpeg")) {
      return common::Result<NodeActionResult>::success(unsupported_action_result(normalized));
    }
    command << "ffmpeg -y -f avfoundation -framerate " << fps
            << " -i \"1:none\" -t " << seconds << " " << shell_quote(out_path);
#elif !defined(_WIN32)
    if (!command_exists("ffmpeg")) {
      return common::Result<NodeActionResult>::success(unsupported_action_result(normalized));
    }
    command << "ffmpeg -y -video_size 1280x720 -framerate " << fps
            << " -f x11grab -i :0.0 -t " << seconds << " " << shell_quote(out_path);
#else
    return common::Result<NodeActionResult>::success(unsupported_action_result(normalized));
#endif

    auto run = run_unrestricted_command(command.str(), parse_timeout_ms(args, 120'000ULL), ctx);
    if (!run.ok()) {
      return common::Result<NodeActionResult>::failure(run.error());
    }
    auto result = run.value();
    if (result.success && !std::filesystem::exists(out_path)) {
      result.success = false;
      result.output = "screen.record command finished without output file";
    } else if (result.success) {
      result.output = "{\"path\":\"" + out_path + "\",\"duration_ms\":" +
                      std::to_string(duration_ms) + "}";
      result.metadata["path"] = out_path;
      result.metadata["duration_ms"] = std::to_string(duration_ms);
      result.metadata["fps"] = std::to_string(fps);
    }
    return common::Result<NodeActionResult>::success(std::move(result));
  }

  return common::Result<NodeActionResult>::failure("unknown node action: " + normalized);
}

std::vector<std::string> default_node_capabilities() {
  return {"camera", "screen", "location", "notify", "system"};
}

std::vector<std::string> default_node_commands() {
  return {"camera.snap", "camera.clip", "screen.record",
          "location.get", "system.notify", "system.run"};
}

} // namespace ghostclaw::nodes
