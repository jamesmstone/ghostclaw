#include "ghostclaw/tts/tts.hpp"

#include "ghostclaw/common/fs.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ghostclaw::tts {

namespace {

std::string shell_quote(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 4);
  out.push_back('\'');
  for (const char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
      continue;
    }
    out.push_back(ch);
  }
  out.push_back('\'');
  return out;
}

std::string basename(const std::string &path) {
  const auto slash = path.find_last_of("/\\");
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

int run_with_system(const std::string &command) { return std::system(command.c_str()); }

bool command_exists(const std::string &command) {
  const std::string probe = "command -v " + shell_quote(command) + " >/dev/null 2>&1";
  return std::system(probe.c_str()) == 0;
}

common::Status write_text_file(const std::filesystem::path &path, const std::string &text) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return common::Status::error("failed to create output directory: " + ec.message());
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return common::Status::error("failed to open output file: " + path.string());
  }
  out << text;
  if (!out) {
    return common::Status::error("failed writing output file: " + path.string());
  }
  return common::Status::success();
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  std::vector<std::uint8_t> out;
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if (size > 0) {
    out.resize(static_cast<std::size_t>(size));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!in) {
      return {};
    }
  }
  return out;
}

} // namespace

SystemTtsProvider::SystemTtsProvider(SystemTtsConfig config) : config_(std::move(config)) {
  if (!config_.command_runner) {
    config_.command_runner = run_with_system;
  }
}

std::string_view SystemTtsProvider::id() const { return "system"; }

bool SystemTtsProvider::health_check() {
  if (config_.dry_run) {
    return true;
  }

  std::string backend;
  (void)build_command(TtsRequest{.text = "health-check", .dry_run = true}, &backend);
  if (backend.empty()) {
    return false;
  }
  return command_exists(backend);
}

std::string SystemTtsProvider::build_command(const TtsRequest &request,
                                             std::string *selected_backend) const {
  const std::string configured = common::trim(config_.command);
  std::string backend = configured;

  if (backend.empty()) {
#ifdef __APPLE__
    backend = "say";
#else
    backend = "espeak";
#endif
  }

  if (selected_backend != nullptr) {
    *selected_backend = backend;
  }

  const std::string backend_name = common::to_lower(basename(backend));
  const std::string voice = common::trim(request.voice.value_or(config_.default_voice.value_or("")));

  std::string rate = common::trim(config_.default_rate.value_or(""));
  if (request.speed.has_value()) {
    const double speed = *request.speed;
    if (speed > 0.0) {
      const int scaled = static_cast<int>(speed * 200.0);
      rate = std::to_string(scaled);
    }
  }

  std::ostringstream cmd;
  cmd << shell_quote(backend);

  if (backend_name == "say") {
    if (!voice.empty()) {
      cmd << " -v " << shell_quote(voice);
    }
    if (!rate.empty()) {
      cmd << " -r " << shell_quote(rate);
    }
    if (request.output_path.has_value()) {
      cmd << " -o " << shell_quote(request.output_path->string());
    }
    cmd << " " << shell_quote(request.text);
    return cmd.str();
  }

  if (!voice.empty()) {
    cmd << " -v " << shell_quote(voice);
  }
  if (!rate.empty()) {
    cmd << " -s " << shell_quote(rate);
  }
  if (request.output_path.has_value()) {
    cmd << " -w " << shell_quote(request.output_path->string());
  }
  cmd << " " << shell_quote(request.text);
  return cmd.str();
}

common::Result<TtsAudio> SystemTtsProvider::synthesize(const TtsRequest &request) {
  const std::string text = common::trim(request.text);
  if (text.empty()) {
    return common::Result<TtsAudio>::failure("TTS text is empty");
  }

  TtsAudio audio;
  audio.provider = std::string(id());
  audio.mime_type = "audio/wav";
  audio.output_path = request.output_path;

  if (config_.dry_run || request.dry_run) {
    if (request.output_path.has_value()) {
      const auto status = write_text_file(*request.output_path, "DRYRUN-SYSTEM:" + text);
      if (!status.ok()) {
        return common::Result<TtsAudio>::failure(status.error());
      }
      audio.bytes = read_bytes(*request.output_path);
    } else {
      audio.bytes.assign(text.begin(), text.end());
    }
    return common::Result<TtsAudio>::success(std::move(audio));
  }

  std::string backend;
  const std::string command = build_command(request, &backend);
  if (backend.empty()) {
    return common::Result<TtsAudio>::failure("system TTS backend is empty");
  }
  if (!command_exists(backend)) {
    return common::Result<TtsAudio>::failure("system TTS backend not found on PATH: " + backend);
  }

  const int code = config_.command_runner(command);
  if (code != 0) {
    return common::Result<TtsAudio>::failure("system TTS command failed with exit code " +
                                             std::to_string(code));
  }

  if (request.output_path.has_value()) {
    audio.bytes = read_bytes(*request.output_path);
  }
  return common::Result<TtsAudio>::success(std::move(audio));
}

} // namespace ghostclaw::tts
