#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/skills/skill.hpp"

#include <filesystem>

namespace ghostclaw::skills {

struct SkillLoadOptions {
  SkillSource source = SkillSource::Unknown;
  bool require_description = false;
};

class SkillLoader {
public:
  [[nodiscard]] static common::Result<Skill>
  load_skill_toml(const std::filesystem::path &path,
                  const SkillLoadOptions &options = {});

  [[nodiscard]] static common::Result<Skill>
  load_skill_md(const std::filesystem::path &path,
                const SkillLoadOptions &options = {});

  [[nodiscard]] static common::Result<Skill>
  load_skill(const std::filesystem::path &skill_dir,
             const SkillLoadOptions &options = {});

  [[nodiscard]] static std::string extract_markdown_instructions(const std::string &markdown);
};

} // namespace ghostclaw::skills
