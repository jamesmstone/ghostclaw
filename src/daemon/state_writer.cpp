#include "ghostclaw/daemon/state_writer.hpp"

#include "ghostclaw/health/health.hpp"
#include "ghostclaw/memory/memory.hpp"

#include <fstream>
#include <sstream>

namespace ghostclaw::daemon {

StateWriter::StateWriter(std::filesystem::path state_file) : state_file_(std::move(state_file)) {}

StateWriter::~StateWriter() { stop(); }

void StateWriter::start() {
  if (running_) {
    return;
  }
  running_ = true;
  started_at_ = std::chrono::steady_clock::now();
  thread_ = std::thread([this]() { write_loop(); });
}

void StateWriter::stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool StateWriter::is_running() const { return running_; }

void StateWriter::write_loop() {
  while (running_) {
    write_state();
    for (int i = 0; i < 50 && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
  write_state();
}

void StateWriter::write_state() const {
  const auto snapshot = health::snapshot();
  const auto now = memory::now_rfc3339();
  const auto uptime =
      std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_at_)
          .count();

  std::ostringstream json;
  json << "{";
  json << "\"written_at\":\"" << now << "\",";
  json << "\"uptime_seconds\":" << uptime << ",";
  json << "\"components\":{";
  bool first = true;
  for (const auto &[name, state] : snapshot.components) {
    if (!first) {
      json << ",";
    }
    first = false;
    json << "\"" << name << "\":{";
    json << "\"status\":\"" << state.status << "\",";
    json << "\"restart_count\":" << state.restart_count;
    if (!state.updated_at.empty()) {
      json << ",\"updated_at\":\"" << state.updated_at << "\"";
    }
    if (state.last_ok.has_value()) {
      json << ",\"last_ok\":\"" << *state.last_ok << "\"";
    }
    if (state.last_error.has_value()) {
      json << ",\"last_error\":\"" << *state.last_error << "\"";
    }
    json << "}";
  }
  json << "}";
  json << "}";

  std::error_code ec;
  std::filesystem::create_directories(state_file_.parent_path(), ec);
  const auto temp_path = state_file_.string() + ".tmp";
  std::ofstream out(temp_path, std::ios::trunc);
  if (!out) {
    return;
  }
  out << json.str();
  out.close();
  std::filesystem::rename(temp_path, state_file_, ec);
}

} // namespace ghostclaw::daemon
