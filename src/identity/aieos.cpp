#include "ghostclaw/identity/aieos.hpp"

#include "ghostclaw/common/fs.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>

namespace ghostclaw::identity {

namespace {

std::optional<std::string> extract_string_field(const std::string &json, const std::string &key) {
  const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
  std::smatch match;
  if (std::regex_search(json, match, pattern) && match.size() > 1) {
    return match[1].str();
  }
  return std::nullopt;
}

std::vector<std::string> extract_array_field(const std::string &json, const std::string &key) {
  const std::regex array_pattern("\"" + key + "\"\\s*:\\s*\\[(.*?)\\]");
  std::smatch array_match;
  if (!std::regex_search(json, array_match, array_pattern) || array_match.size() < 2) {
    return {};
  }

  std::vector<std::string> out;
  const std::string body = array_match[1].str();
  const std::regex item_pattern(R"re("([^"]+)")re");
  auto begin = std::sregex_iterator(body.begin(), body.end(), item_pattern);
  auto end = std::sregex_iterator();
  for (auto it = begin; it != end; ++it) {
    out.push_back((*it)[1].str());
  }
  return out;
}

std::string join_lines(const std::vector<std::string> &items, const std::string &prefix = "- ") {
  std::ostringstream out;
  for (const auto &item : items) {
    out << prefix << item << "\n";
  }
  return out.str();
}

} // namespace

common::Result<Identity> AieosLoader::load_from_file(const std::filesystem::path &path) {
  std::ifstream in(path);
  if (!in) {
    return common::Result<Identity>::failure("failed to open AIEOS file: " + path.string());
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  return load_from_string(buffer.str());
}

common::Result<Identity> AieosLoader::load_from_string(const std::string &json_str) {
  auto parsed = parse_json_like(json_str);
  if (!parsed.ok()) {
    return common::Result<Identity>::failure(parsed.error());
  }
  return common::Result<Identity>::success(convert_to_identity(parsed.value()));
}

common::Result<AieosIdentity> AieosLoader::parse_json_like(const std::string &json) {
  if (json.find('{') == std::string::npos || json.find('}') == std::string::npos) {
    return common::Result<AieosIdentity>::failure("invalid AIEOS JSON payload");
  }

  AieosIdentity out;
  out.first_name = extract_string_field(json, "first");
  out.last_name = extract_string_field(json, "last");
  out.nickname = extract_string_field(json, "nickname");
  out.bio = extract_string_field(json, "bio");
  out.mbti = extract_string_field(json, "mbti");
  out.alignment = extract_string_field(json, "alignment");
  out.core_drive = extract_string_field(json, "core_drive");

  out.skills = extract_array_field(json, "skills");
  out.limitations = extract_array_field(json, "limitations");
  out.catchphrases = extract_array_field(json, "catchphrases");
  out.short_term_goals = extract_array_field(json, "short_term_goals");
  out.long_term_goals = extract_array_field(json, "long_term_goals");

  return common::Result<AieosIdentity>::success(std::move(out));
}

Identity AieosLoader::convert_to_identity(const AieosIdentity &aieos) {
  Identity identity;
  std::string name;
  if (aieos.first_name.has_value()) {
    name = *aieos.first_name;
  }
  if (aieos.last_name.has_value()) {
    if (!name.empty()) {
      name += " ";
    }
    name += *aieos.last_name;
  }
  if (name.empty() && aieos.nickname.has_value()) {
    name = *aieos.nickname;
  }
  identity.name = name.empty() ? "GhostClaw" : name;

  if (aieos.mbti.has_value()) {
    identity.personality = format_mbti(*aieos.mbti);
  }
  identity.directives = build_system_prompt(aieos);
  identity.user_context = aieos.bio.value_or("");
  identity.raw_system_prompt = identity.directives;
  return identity;
}

std::string AieosLoader::build_system_prompt(const AieosIdentity &aieos) {
  std::ostringstream prompt;

  std::string name;
  if (aieos.first_name.has_value()) {
    name = *aieos.first_name;
  }
  if (aieos.last_name.has_value()) {
    if (!name.empty()) {
      name += " ";
    }
    name += *aieos.last_name;
  }
  if (name.empty() && aieos.nickname.has_value()) {
    name = *aieos.nickname;
  }
  if (!name.empty()) {
    prompt << "Your name is " << name;
    if (aieos.nickname.has_value()) {
      prompt << " (" << *aieos.nickname << ")";
    }
    prompt << ".\n";
  }

  if (aieos.bio.has_value()) {
    prompt << *aieos.bio << "\n";
  }

  if (aieos.mbti.has_value()) {
    prompt << format_mbti(*aieos.mbti) << "\n";
  }

  if (aieos.alignment.has_value()) {
    prompt << format_alignment(*aieos.alignment) << "\n";
  }

  if (aieos.core_drive.has_value()) {
    prompt << "Core drive: " << *aieos.core_drive << "\n";
  }

  if (!aieos.skills.empty()) {
    prompt << "Skills:\n" << join_lines(aieos.skills);
  }
  if (!aieos.limitations.empty()) {
    prompt << "Limitations:\n" << join_lines(aieos.limitations);
  }
  if (!aieos.catchphrases.empty()) {
    prompt << "Catchphrases:\n" << join_lines(aieos.catchphrases);
  }
  if (!aieos.short_term_goals.empty()) {
    prompt << "Short-term goals:\n" << join_lines(aieos.short_term_goals);
  }
  if (!aieos.long_term_goals.empty()) {
    prompt << "Long-term goals:\n" << join_lines(aieos.long_term_goals);
  }

  return prompt.str();
}

std::string AieosLoader::format_mbti(const std::string &mbti) {
  static const std::unordered_map<std::string, std::string> descriptions = {
      {"INTJ", "You are an INTJ: strategic, independent, and logic-driven."},
      {"INTP", "You are an INTP: analytical, curious, and inventive."},
      {"ENTJ", "You are an ENTJ: decisive, ambitious, and leadership-oriented."},
      {"ENTP", "You are an ENTP: inventive, energetic, and challenge-seeking."},
      {"INFJ", "You are an INFJ: insightful, principled, and empathetic."},
      {"INFP", "You are an INFP: idealistic, creative, and authenticity-focused."},
      {"ENFJ", "You are an ENFJ: inspiring, empathetic, and socially aware."},
      {"ENFP", "You are an ENFP: enthusiastic, imaginative, and people-oriented."},
      {"ISTJ", "You are an ISTJ: dependable, practical, and methodical."},
      {"ISFJ", "You are an ISFJ: caring, reliable, and detail-focused."},
      {"ESTJ", "You are an ESTJ: organized, direct, and results-focused."},
      {"ESFJ", "You are an ESFJ: helpful, social, and harmony-driven."},
      {"ISTP", "You are an ISTP: practical, observant, and technically sharp."},
      {"ISFP", "You are an ISFP: gentle, adaptable, and present-focused."},
      {"ESTP", "You are an ESTP: action-oriented, pragmatic, and bold."},
      {"ESFP", "You are an ESFP: energetic, friendly, and expressive."},
  };
  if (const auto it = descriptions.find(mbti); it != descriptions.end()) {
    return it->second;
  }
  return "Your personality type is " + mbti + ".";
}

std::string AieosLoader::format_alignment(const std::string &alignment) {
  static const std::unordered_map<std::string, std::string> descriptions = {
      {"Lawful Good", "Alignment: Lawful Good (principled and benevolent)."},
      {"Neutral Good", "Alignment: Neutral Good (helpful and pragmatic)."},
      {"Chaotic Good", "Alignment: Chaotic Good (freedom-focused and benevolent)."},
      {"Lawful Neutral", "Alignment: Lawful Neutral (order and consistency)."},
      {"True Neutral", "Alignment: True Neutral (balanced and situational)."},
      {"Chaotic Neutral", "Alignment: Chaotic Neutral (independent and unconstrained)."},
      {"Lawful Evil", "Alignment: Lawful Evil (order used for self-interest)."},
      {"Neutral Evil", "Alignment: Neutral Evil (self-interest over all else)."},
      {"Chaotic Evil", "Alignment: Chaotic Evil (destructive and unbounded)."},
  };
  if (const auto it = descriptions.find(alignment); it != descriptions.end()) {
    return it->second;
  }
  return "Alignment: " + alignment + ".";
}

} // namespace ghostclaw::identity
