#include "ghostclaw/identity/templates.hpp"

#include <filesystem>
#include <fstream>

namespace ghostclaw::identity::templates {

namespace {

common::Status write_if_missing(const std::filesystem::path &path, const std::string_view content) {
  if (std::filesystem::exists(path)) {
    return common::Status::success();
  }
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return common::Status::error("failed to write " + path.string());
  }
  out << content;
  return common::Status::success();
}

} // namespace

common::Status create_default_identity_files(const std::filesystem::path &workspace) {
  std::error_code ec;
  std::filesystem::create_directories(workspace, ec);
  if (ec) {
    return common::Status::error("failed to create workspace: " + ec.message());
  }

  auto status = write_if_missing(workspace / "SOUL.md", DEFAULT_SOUL);
  if (!status.ok()) {
    return status;
  }
  status = write_if_missing(workspace / "IDENTITY.md", DEFAULT_IDENTITY);
  if (!status.ok()) {
    return status;
  }
  status = write_if_missing(workspace / "AGENTS.md", DEFAULT_AGENTS);
  if (!status.ok()) {
    return status;
  }
  status = write_if_missing(workspace / "USER.md", DEFAULT_USER);
  if (!status.ok()) {
    return status;
  }
  return write_if_missing(workspace / "TOOLS.md", DEFAULT_TOOLS);
}

} // namespace ghostclaw::identity::templates
