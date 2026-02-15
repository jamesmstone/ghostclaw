#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/skills/skill.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace ghostclaw::skills {

struct SkillSearchResult {
  Skill skill;
  double score = 0.0;
};

class SkillRegistry {
public:
  explicit SkillRegistry(std::filesystem::path workspace_skills_dir,
                         std::optional<std::filesystem::path> community_skills_dir = std::nullopt);

  [[nodiscard]] common::Result<std::vector<Skill>> list() const;
  [[nodiscard]] common::Result<std::vector<Skill>> list_workspace() const;
  [[nodiscard]] common::Result<std::vector<Skill>> list_community() const;
  [[nodiscard]] common::Result<std::vector<Skill>> list_all() const;

  [[nodiscard]] common::Result<std::vector<SkillSearchResult>>
  search(const std::string &query, bool include_community = true) const;
  [[nodiscard]] common::Result<std::optional<Skill>>
  find(const std::string &name, bool include_community = true) const;

  [[nodiscard]] common::Result<bool> install(const std::filesystem::path &source_dir);
  [[nodiscard]] common::Result<bool>
  install(const std::string &name_or_path, bool prefer_community = true);
  [[nodiscard]] common::Result<bool> remove(const std::string &name);

  [[nodiscard]] common::Result<std::size_t>
  sync_github(const std::string &repo,
              const std::string &branch = "main",
              const std::string &skills_subdir = "skills",
              bool prune_missing = false);

private:
  [[nodiscard]] common::Result<std::vector<Skill>> list_dir(const std::filesystem::path &root,
                                                            SkillSource source) const;
  [[nodiscard]] common::Result<bool> install_from_loaded(const Skill &loaded,
                                                         const std::filesystem::path &source_dir);

  std::filesystem::path workspace_skills_dir_;
  std::filesystem::path community_skills_dir_;
};

} // namespace ghostclaw::skills
