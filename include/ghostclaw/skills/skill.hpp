#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ghostclaw::skills {

enum class SkillSource {
  Unknown,
  Workspace,
  Community,
  Bundled,
};

[[nodiscard]] std::string_view skill_source_to_string(SkillSource source);

struct SkillTool {
  std::string name;
  std::string description;
  std::string kind = "shell";
  std::string command;
  std::vector<std::string> args;
  std::unordered_map<std::string, std::string> env;
};

struct SkillInstallSpec {
  std::string id;
  std::string kind;
  std::string label;
  std::string formula;
  std::string package;
  std::string module;
  std::string url;
  std::string target_dir;
  std::string version;
  std::vector<std::string> bins;
  std::vector<std::string> os;
};

struct Skill {
  std::string name;
  std::string description;
  std::string version = "1.0.0";
  std::optional<std::string> author;
  std::vector<std::string> tags;
  std::vector<SkillTool> tools;
  std::vector<SkillInstallSpec> install_specs;

  std::string instructions_markdown;
  std::vector<std::string> prompts;

  std::optional<std::filesystem::path> location;
  std::optional<std::filesystem::path> readme_path;
  SkillSource source = SkillSource::Unknown;
  std::unordered_map<std::string, std::string> metadata;
};

} // namespace ghostclaw::skills
