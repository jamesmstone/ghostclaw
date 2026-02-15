#include "ghostclaw/agent/session.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace ghostclaw::agent {

namespace {

std::string escape_json(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(c);
      break;
    }
  }
  return out;
}

std::string extract_json_field(const std::string &line, const std::string &field) {
  const std::string key = "\"" + field + "\":\"";
  const auto pos = line.find(key);
  if (pos == std::string::npos) {
    return "";
  }
  const auto start = pos + key.size();
  auto end = line.find('"', start);
  if (end == std::string::npos) {
    return "";
  }
  return line.substr(start, end - start);
}

} // namespace

Session::Session(std::string id, std::filesystem::path sessions_dir) : id_(std::move(id)) {
  std::error_code ec;
  std::filesystem::create_directories(sessions_dir, ec);
  file_path_ = sessions_dir / (id_ + ".jsonl");
}

common::Status Session::append(const SessionEntry &entry) {
  std::ofstream out(file_path_, std::ios::app);
  if (!out) {
    return common::Status::error("failed to open session file");
  }

  out << "{\"role\":\"" << escape_json(entry.role) << "\","
      << "\"content\":\"" << escape_json(entry.content) << "\","
      << "\"timestamp\":\"" << escape_json(entry.timestamp) << "\"}"
      << "\n";

  return out ? common::Status::success() : common::Status::error("failed to append session entry");
}

common::Result<std::vector<SessionEntry>> Session::load_history(const std::size_t limit) const {
  std::ifstream in(file_path_);
  if (!in) {
    return common::Result<std::vector<SessionEntry>>::success({});
  }

  std::vector<SessionEntry> entries;
  std::string line;
  while (std::getline(in, line)) {
    SessionEntry entry;
    entry.role = extract_json_field(line, "role");
    entry.content = extract_json_field(line, "content");
    entry.timestamp = extract_json_field(line, "timestamp");
    entries.push_back(std::move(entry));
  }

  if (limit > 0 && entries.size() > limit) {
    entries.erase(entries.begin(), entries.end() - static_cast<long>(limit));
  }

  return common::Result<std::vector<SessionEntry>>::success(std::move(entries));
}

common::Status Session::compact(const std::size_t keep_recent) {
  auto loaded = load_history();
  if (!loaded.ok()) {
    return common::Status::error(loaded.error());
  }

  auto entries = std::move(loaded.value());
  if (entries.size() > keep_recent) {
    entries.erase(entries.begin(), entries.end() - static_cast<long>(keep_recent));
  }

  std::ofstream out(file_path_, std::ios::trunc);
  if (!out) {
    return common::Status::error("failed to rewrite session file");
  }

  for (const auto &entry : entries) {
    out << "{\"role\":\"" << escape_json(entry.role) << "\","
        << "\"content\":\"" << escape_json(entry.content) << "\","
        << "\"timestamp\":\"" << escape_json(entry.timestamp) << "\"}"
        << "\n";
  }

  return common::Status::success();
}

const std::string &Session::id() const { return id_; }

} // namespace ghostclaw::agent
