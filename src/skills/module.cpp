#include "ghostclaw/skills/skill.hpp"

namespace ghostclaw::skills {

std::string_view skill_source_to_string(const SkillSource source) {
  switch (source) {
  case SkillSource::Workspace:
    return "workspace";
  case SkillSource::Community:
    return "community";
  case SkillSource::Bundled:
    return "bundled";
  case SkillSource::Unknown:
  default:
    return "unknown";
  }
}

} // namespace ghostclaw::skills
