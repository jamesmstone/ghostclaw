#pragma once

#include "ghostclaw/skills/skill.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ghostclaw::skills {

struct SkillCompatIssue {
  std::string type = "capability_unavailable";
  std::string capability;
  std::string message;
  std::string next_action;
};

struct SkillCompatResolution {
  std::string rewritten_text;
  std::vector<SkillCompatIssue> issues;
};

[[nodiscard]] std::string resolve_base_dir_tokens(const Skill &skill, std::string text);

[[nodiscard]] SkillCompatResolution resolve_openclaw_compatibility(const Skill &skill,
                                                                   std::string text);

[[nodiscard]] std::vector<std::string>
compatibility_notes_for_skill(const Skill &skill, const std::string &text);

[[nodiscard]] std::string format_compatibility_notes(const std::vector<std::string> &notes);
[[nodiscard]] std::string format_compatibility_issues(const std::vector<SkillCompatIssue> &issues);

[[nodiscard]] std::string skill_instruction_body(const Skill &skill);

[[nodiscard]] std::string prepared_skill_instructions(const Skill &skill, std::size_t max_chars,
                                                      bool include_compatibility_notes);

} // namespace ghostclaw::skills
