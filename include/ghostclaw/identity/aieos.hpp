#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/identity/identity.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ghostclaw::identity {

struct AieosIdentity {
  std::optional<std::string> first_name;
  std::optional<std::string> last_name;
  std::optional<std::string> nickname;
  std::optional<std::string> bio;
  std::optional<std::string> mbti;
  std::optional<std::string> alignment;
  std::optional<std::string> core_drive;
  std::vector<std::string> skills;
  std::vector<std::string> limitations;
  std::vector<std::string> catchphrases;
  std::vector<std::string> short_term_goals;
  std::vector<std::string> long_term_goals;
};

class AieosLoader {
public:
  [[nodiscard]] static common::Result<Identity> load_from_file(const std::filesystem::path &path);
  [[nodiscard]] static common::Result<Identity> load_from_string(const std::string &json_str);

private:
  [[nodiscard]] static common::Result<AieosIdentity> parse_json_like(const std::string &json);
  [[nodiscard]] static Identity convert_to_identity(const AieosIdentity &aieos);
  [[nodiscard]] static std::string build_system_prompt(const AieosIdentity &aieos);

  [[nodiscard]] static std::string format_mbti(const std::string &mbti);
  [[nodiscard]] static std::string format_alignment(const std::string &alignment);
};

} // namespace ghostclaw::identity
