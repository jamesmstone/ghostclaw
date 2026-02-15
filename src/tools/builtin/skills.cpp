#include "ghostclaw/tools/builtin/skills.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/skills/compat.hpp"
#include "ghostclaw/skills/registry.hpp"

#include <algorithm>
#include <sstream>

namespace ghostclaw::tools {

namespace {

bool parse_bool(const std::string &value, const bool fallback) {
  const std::string lower = common::to_lower(common::trim(value));
  if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
    return true;
  }
  if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
    return false;
  }
  return fallback;
}

std::size_t parse_limit(const ToolArgs &args, const std::size_t fallback) {
  const auto it = args.find("limit");
  if (it == args.end()) {
    return fallback;
  }
  try {
    return static_cast<std::size_t>(std::stoull(it->second));
  } catch (...) {
    return fallback;
  }
}

std::string format_skill_line(const skills::Skill &skill) {
  std::ostringstream out;
  out << skill.name << " [" << skills::skill_source_to_string(skill.source) << "]";
  if (!common::trim(skill.description).empty()) {
    out << " - " << skill.description;
  }
  return out.str();
}

} // namespace

std::string_view SkillsTool::name() const { return "skills"; }

std::string_view SkillsTool::description() const {
  return "List, search, and load skills by name";
}

std::string SkillsTool::parameters_schema() const {
  return R"({"type":"object","required":["action"],"properties":{"action":{"type":"string","enum":["list","search","load"]},"query":{"type":"string"},"name":{"type":"string"},"limit":{"type":"string"},"include_community":{"type":"string"}}})";
}

common::Result<ToolResult> SkillsTool::execute(const ToolArgs &args, const ToolContext &ctx) {
  const auto action_it = args.find("action");
  if (action_it == args.end()) {
    return common::Result<ToolResult>::failure("Missing action");
  }
  const std::string action = common::to_lower(common::trim(action_it->second));

  skills::SkillRegistry registry(ctx.workspace_path / "skills", ctx.workspace_path / ".community-skills");

  ToolResult result;
  result.success = true;

  if (action == "list") {
    auto listed = registry.list_all();
    if (!listed.ok()) {
      return common::Result<ToolResult>::failure(listed.error());
    }
    const auto limit = parse_limit(args, 200);
    std::ostringstream out;
    std::size_t emitted = 0;
    for (const auto &skill : listed.value()) {
      if (emitted >= limit) {
        out << "... (" << (listed.value().size() - emitted) << " more)\n";
        break;
      }
      out << "- " << format_skill_line(skill) << "\n";
      ++emitted;
    }
    result.output = out.str();
    result.metadata["count"] = std::to_string(listed.value().size());
    return common::Result<ToolResult>::success(std::move(result));
  }

  if (action == "search") {
    const auto query_it = args.find("query");
    if (query_it == args.end() || common::trim(query_it->second).empty()) {
      return common::Result<ToolResult>::failure("Missing query");
    }
    const bool include_community = parse_bool(
        args.contains("include_community") ? args.at("include_community") : "true", true);
    const auto limit = parse_limit(args, 10);

    auto searched = registry.search(query_it->second, include_community);
    if (!searched.ok()) {
      return common::Result<ToolResult>::failure(searched.error());
    }

    std::ostringstream out;
    std::size_t emitted = 0;
    for (const auto &entry : searched.value()) {
      if (emitted >= limit) {
        break;
      }
      out << "- " << format_skill_line(entry.skill) << " (score=" << entry.score << ")\n";
      ++emitted;
    }
    result.output = out.str();
    result.metadata["count"] = std::to_string(searched.value().size());
    return common::Result<ToolResult>::success(std::move(result));
  }

  if (action == "load") {
    const auto name_it = args.find("name");
    if (name_it == args.end() || common::trim(name_it->second).empty()) {
      return common::Result<ToolResult>::failure("Missing name");
    }
    auto found = registry.find(name_it->second, true);
    if (!found.ok()) {
      return common::Result<ToolResult>::failure(found.error());
    }
    if (!found.value().has_value()) {
      return common::Result<ToolResult>::failure("Skill not found: " + name_it->second);
    }

    const auto &skill = *found.value();
    const auto max_chars = parse_limit(args, 12'000);
    std::string instructions = skills::prepared_skill_instructions(skill, max_chars, true);
    if (instructions.empty()) {
      instructions = "(No instructions available for this skill.)";
    }

    std::ostringstream out;
    out << "Skill: " << skill.name << "\n";
    out << "Source: " << skills::skill_source_to_string(skill.source) << "\n";
    if (skill.location.has_value()) {
      out << "BaseDir: " << skill.location->string() << "\n";
    }
    out << "\n" << instructions;

    result.output = out.str();
    result.metadata["name"] = skill.name;
    result.metadata["source"] = std::string(skills::skill_source_to_string(skill.source));
    return common::Result<ToolResult>::success(std::move(result));
  }

  return common::Result<ToolResult>::failure("Unsupported action: " + action);
}

bool SkillsTool::is_safe() const { return true; }

std::string_view SkillsTool::group() const { return "skills"; }

} // namespace ghostclaw::tools
