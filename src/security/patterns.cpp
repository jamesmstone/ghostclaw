#include "ghostclaw/security/external_content.hpp"

#include <array>
#include <cstdint>
#include <regex>

namespace ghostclaw::security {

namespace {

struct PatternEntry {
  const char *label;
  std::regex regex;
};

const std::array<PatternEntry, 12> kSuspiciousPatterns = {
    PatternEntry{"ignore previous instructions",
                 std::regex(R"(ignore\s+(all\s+)?(previous|prior|above)\s+(instructions?|prompts?))",
                            std::regex::icase)},
    PatternEntry{"disregard previous",
                 std::regex(R"(disregard\s+(all\s+)?(previous|prior|above))", std::regex::icase)},
    PatternEntry{"forget instructions",
                 std::regex(R"(forget\s+(everything|all|your)\s+(instructions?|rules?|guidelines?))",
                            std::regex::icase)},
    PatternEntry{"you are now",
                 std::regex(R"(you\s+are\s+now\s+(a|an)\s+)", std::regex::icase)},
    PatternEntry{"new instructions",
                 std::regex(R"(new\s+instructions?:)", std::regex::icase)},
    PatternEntry{"system override",
                 std::regex(R"(system\s*:?\s*(prompt|override|command))", std::regex::icase)},
    PatternEntry{"exec command",
                 std::regex(R"(\bexec\b.*command\s*=)", std::regex::icase)},
    PatternEntry{"elevated true",
                 std::regex(R"(elevated\s*=\s*true)", std::regex::icase)},
    PatternEntry{"destructive rm",
                 std::regex(R"(rm\s+-rf)", std::regex::icase)},
    PatternEntry{"delete all",
                 std::regex(R"(delete\s+all\s+(emails?|files?|data))", std::regex::icase)},
    PatternEntry{"xml system tag",
                 std::regex(R"(<\/?system>)", std::regex::icase)},
    PatternEntry{"role boundary",
                 std::regex(R"(\]\s*\n\s*\[?(system|assistant|user)\]?:)", std::regex::icase)},
};

bool decode_utf8_codepoint(const std::string &input, std::size_t &index, std::uint32_t &cp,
                           std::string &raw) {
  if (index >= input.size()) {
    return false;
  }

  const unsigned char lead = static_cast<unsigned char>(input[index]);
  if (lead < 0x80U) {
    cp = lead;
    raw.assign(1, static_cast<char>(lead));
    ++index;
    return true;
  }

  std::size_t extra = 0;
  std::uint32_t value = 0;
  if ((lead & 0xE0U) == 0xC0U) {
    extra = 1;
    value = lead & 0x1FU;
  } else if ((lead & 0xF0U) == 0xE0U) {
    extra = 2;
    value = lead & 0x0FU;
  } else if ((lead & 0xF8U) == 0xF0U) {
    extra = 3;
    value = lead & 0x07U;
  } else {
    cp = lead;
    raw.assign(1, static_cast<char>(lead));
    ++index;
    return true;
  }

  if (index + extra >= input.size()) {
    cp = lead;
    raw.assign(1, static_cast<char>(lead));
    ++index;
    return true;
  }

  raw.clear();
  raw.push_back(static_cast<char>(lead));
  for (std::size_t i = 1; i <= extra; ++i) {
    const unsigned char cont = static_cast<unsigned char>(input[index + i]);
    if ((cont & 0xC0U) != 0x80U) {
      cp = lead;
      raw.assign(1, static_cast<char>(lead));
      ++index;
      return true;
    }
    value = (value << 6U) | static_cast<std::uint32_t>(cont & 0x3FU);
    raw.push_back(static_cast<char>(cont));
  }

  index += extra + 1;
  cp = value;
  return true;
}

std::string fold_codepoint(const std::uint32_t cp, const std::string &raw) {
  if (cp >= 0xFF21U && cp <= 0xFF3AU) {
    return std::string(1, static_cast<char>(cp - 0xFEE0U));
  }
  if (cp >= 0xFF41U && cp <= 0xFF5AU) {
    return std::string(1, static_cast<char>(cp - 0xFEE0U));
  }

  switch (cp) {
  case 0xFF1CU:
  case 0x2329U:
  case 0x3008U:
  case 0x2039U:
  case 0x27E8U:
  case 0xFE64U:
    return "<";
  case 0xFF1EU:
  case 0x232AU:
  case 0x3009U:
  case 0x203AU:
  case 0x27E9U:
  case 0xFE65U:
    return ">";
  default:
    break;
  }

  return raw;
}

} // namespace

std::vector<std::string> detect_suspicious_patterns(const std::string &content) {
  std::vector<std::string> matches;
  matches.reserve(kSuspiciousPatterns.size());
  for (const auto &entry : kSuspiciousPatterns) {
    if (std::regex_search(content, entry.regex)) {
      matches.push_back(entry.label);
    }
  }
  return matches;
}

std::string normalize_homoglyphs(const std::string &content) {
  std::string output;
  output.reserve(content.size());

  std::size_t index = 0;
  while (index < content.size()) {
    std::uint32_t cp = 0;
    std::string raw;
    if (!decode_utf8_codepoint(content, index, cp, raw)) {
      break;
    }
    output += fold_codepoint(cp, raw);
  }

  return output;
}

} // namespace ghostclaw::security
