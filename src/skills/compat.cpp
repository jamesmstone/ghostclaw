#include "ghostclaw/skills/compat.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/json_util.hpp"
#include "ghostclaw/skills/loader.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace ghostclaw::skills {

namespace {

void replace_all(std::string &text, const std::string &needle, const std::string &replacement) {
  if (needle.empty()) {
    return;
  }
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

std::string trim_to_limit(const std::string &input, const std::size_t max_chars) {
  if (max_chars == 0 || input.size() <= max_chars) {
    return input;
  }
  std::string out = input.substr(0, max_chars);
  out += "\n[truncated]";
  return out;
}

std::string issue_key(const SkillCompatIssue &issue) {
  return issue.type + "|" + issue.capability + "|" + issue.message + "|" + issue.next_action;
}

} // namespace

std::string resolve_base_dir_tokens(const Skill &skill, std::string text) {
  if (text.empty()) {
    return text;
  }
  if (!skill.location.has_value()) {
    return text;
  }
  replace_all(text, "{baseDir}", skill.location->string());
  return text;
}

SkillCompatResolution resolve_openclaw_compatibility(const Skill &skill, std::string text) {
  SkillCompatResolution resolution;
  resolution.rewritten_text = std::move(text);

  // Feasible command shims.
  replace_all(resolution.rewritten_text, "openclaw skills", "ghostclaw skills");
  replace_all(resolution.rewritten_text, "openclaw message", "ghostclaw message");
  replace_all(resolution.rewritten_text, "`openclaw skills`", "`ghostclaw skills`");
  replace_all(resolution.rewritten_text, "`openclaw message`", "`ghostclaw message`");

  const std::string lower = common::to_lower(resolution.rewritten_text);
  if (lower.find("plugin") != std::string::npos &&
      (lower.find("openclaw") != std::string::npos || lower.find("install") != std::string::npos)) {
    resolution.issues.push_back({
        .type = "capability_unavailable",
        .capability = "openclaw_plugins",
        .message = "This skill references OpenClaw plugin workflows that GhostClaw cannot execute directly.",
        .next_action = "Use GhostClaw native tools (`skills`, `message`, `calendar`, `email`, `reminder`) or install an equivalent adapter backend.",
    });
  }

  if (lower.find("composio") != std::string::npos) {
    resolution.issues.push_back({
        .type = "capability_unavailable",
        .capability = "external_plugin_runtime",
        .message = "This skill depends on an external plugin runtime not guaranteed in GhostClaw.",
        .next_action = "Configure GhostClaw-native integrations or replace the plugin step with explicit tool calls.",
    });
  }

  // Detect unsupported `openclaw <command>` references and emit explicit structured fallback guidance.
  std::regex cmd_re(R"(\bopenclaw\s+([a-zA-Z0-9_-]+)\b)");
  std::smatch match;
  std::string remaining = resolution.rewritten_text;
  const std::unordered_set<std::string> supported = {"skills", "message"};
  while (std::regex_search(remaining, match, cmd_re)) {
    const std::string command = common::to_lower(match[1].str());
    if (!supported.contains(command)) {
      resolution.issues.push_back({
          .type = "capability_unavailable",
          .capability = "openclaw_command:" + command,
          .message = "Referenced command `openclaw " + command +
                     "` has no direct GhostClaw shim.",
          .next_action = "Map this step to a GhostClaw tool or CLI command and update the skill instructions.",
      });
    }
    remaining = match.suffix().str();
  }

  if (skill.metadata.contains("source") &&
      common::to_lower(skill.metadata.at("source")) == "openclaw-import") {
    resolution.issues.push_back({
        .type = "compatibility_hint",
        .capability = "openclaw_import",
        .message = "Imported from OpenClaw; command and plugin steps may need GhostClaw mappings.",
        .next_action = "Prefer `ghostclaw skills` and `ghostclaw message`, and validate each side-effecting step.",
    });
  }

  std::vector<SkillCompatIssue> unique;
  unique.reserve(resolution.issues.size());
  std::unordered_set<std::string> seen;
  for (const auto &issue : resolution.issues) {
    if (seen.insert(issue_key(issue)).second) {
      unique.push_back(issue);
    }
  }
  resolution.issues = std::move(unique);
  return resolution;
}

std::vector<std::string> compatibility_notes_for_skill(const Skill &skill, const std::string &text) {
  std::vector<std::string> notes;
  const std::string lower = common::to_lower(text);

  if (lower.find("openclaw") != std::string::npos) {
    notes.push_back("Use `ghostclaw` command names in place of `openclaw`.");
  }
  if (lower.find("plugin") != std::string::npos &&
      lower.find("install") != std::string::npos) {
    notes.push_back(
        "If this skill expects OpenClaw plugins, map to GhostClaw tools or return a capability-unavailable response.");
  }
  if (skill.metadata.contains("source") && common::to_lower(skill.metadata.at("source")) == "openclaw-import") {
    notes.push_back("Imported from OpenClaw; verify tool names before executing side effects.");
  }

  std::sort(notes.begin(), notes.end());
  notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
  return notes;
}

std::string format_compatibility_notes(const std::vector<std::string> &notes) {
  if (notes.empty()) {
    return "";
  }
  std::ostringstream out;
  out << "\nCompatibility Notes:\n";
  for (const auto &note : notes) {
    out << "- " << note << "\n";
  }
  return out.str();
}

std::string format_compatibility_issues(const std::vector<SkillCompatIssue> &issues) {
  if (issues.empty()) {
    return "";
  }
  std::ostringstream out;
  out << "\nCompatibility Issues (Structured):\n";
  for (const auto &issue : issues) {
    out << "{"
        << "\"type\":\"" << common::json_escape(issue.type) << "\","
        << "\"capability\":\"" << common::json_escape(issue.capability) << "\","
        << "\"message\":\"" << common::json_escape(issue.message) << "\","
        << "\"next_action\":\"" << common::json_escape(issue.next_action) << "\""
        << "}\n";
  }
  return out.str();
}

std::string skill_instruction_body(const Skill &skill) {
  if (!common::trim(skill.instructions_markdown).empty()) {
    return skill.instructions_markdown;
  }

  if (skill.location.has_value()) {
    auto loaded = SkillLoader::load_skill(*skill.location, {.source = skill.source});
    if (loaded.ok() && !common::trim(loaded.value().instructions_markdown).empty()) {
      return loaded.value().instructions_markdown;
    }
  }

  if (skill.readme_path.has_value()) {
    std::ifstream in(*skill.readme_path);
    if (in) {
      std::stringstream buffer;
      buffer << in.rdbuf();
      return SkillLoader::extract_markdown_instructions(buffer.str());
    }
  }

  return "";
}

std::string prepared_skill_instructions(const Skill &skill, const std::size_t max_chars,
                                        const bool include_compatibility_notes) {
  std::string body = resolve_base_dir_tokens(skill, skill_instruction_body(skill));
  auto resolved = resolve_openclaw_compatibility(skill, body);
  body = std::move(resolved.rewritten_text);
  if (include_compatibility_notes) {
    const auto notes = compatibility_notes_for_skill(skill, body);
    body += format_compatibility_notes(notes);
    body += format_compatibility_issues(resolved.issues);
  }
  return trim_to_limit(common::trim(body), max_chars);
}

} // namespace ghostclaw::skills
