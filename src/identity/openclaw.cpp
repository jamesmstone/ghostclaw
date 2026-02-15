#include "ghostclaw/identity/openclaw.hpp"

#include "ghostclaw/common/fs.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ghostclaw::identity {

common::Result<Identity> OpenClawLoader::load(const std::filesystem::path &workspace) {
  Identity identity;

  const std::string identity_content = read_identity_file(workspace, "IDENTITY.md");
  const std::string soul_content = read_identity_file(workspace, "SOUL.md");
  const std::string agents_content = read_identity_file(workspace, "AGENTS.md");
  const std::string user_content = read_identity_file(workspace, "USER.md");
  const std::string tools_content = read_identity_file(workspace, "TOOLS.md");

  identity.name = "GhostClaw";
  if (!identity_content.empty()) {
    const auto newline = identity_content.find('\n');
    std::string first_line = identity_content.substr(0, newline);
    if (common::starts_with(first_line, "# ")) {
      first_line = common::trim(first_line.substr(2));
      if (!first_line.empty()) {
        identity.name = first_line;
      }
    }
  }

  identity.personality = soul_content;
  identity.directives = agents_content;
  identity.user_context = user_content;

  std::string prompt;
  if (!identity_content.empty()) {
    prompt += identity_content + "\n\n";
  }
  if (!soul_content.empty()) {
    prompt += soul_content + "\n\n";
  }
  if (!agents_content.empty()) {
    prompt += agents_content + "\n\n";
  }
  if (!user_content.empty()) {
    prompt += "## About the User\n" + user_content + "\n\n";
  }
  if (!tools_content.empty()) {
    prompt += "## Tool Usage Guidelines\n" + tools_content + "\n\n";
  }

  identity.raw_system_prompt = std::move(prompt);
  return common::Result<Identity>::success(std::move(identity));
}

std::string OpenClawLoader::read_identity_file(const std::filesystem::path &workspace,
                                               const std::string_view filename) {
  const auto path = workspace / filename;
  if (!std::filesystem::exists(path)) {
    return "";
  }

  std::ifstream in(path);
  if (!in) {
    return "";
  }

  std::stringstream buffer;
  buffer << in.rdbuf();
  return truncate_content(buffer.str(), MAX_FILE_SIZE);
}

std::string OpenClawLoader::truncate_content(const std::string &content, const std::size_t max_size) {
  if (content.size() <= max_size) {
    return content;
  }
  return content.substr(0, max_size) + "\n[... truncated ...]";
}

} // namespace ghostclaw::identity
