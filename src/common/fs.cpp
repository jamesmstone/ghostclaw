#include "ghostclaw/common/fs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>

namespace ghostclaw::common {

std::string trim(const std::string &input) {
  auto first = std::find_if_not(input.begin(), input.end(), [](unsigned char c) {
    return std::isspace(c) != 0;
  });
  auto last = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char c) {
    return std::isspace(c) != 0;
  }).base();

  if (first >= last) {
    return "";
  }
  return std::string(first, last);
}

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string to_lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

Result<std::filesystem::path> home_dir() {
  if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return Result<std::filesystem::path>::success(std::filesystem::path(home));
  }
  return Result<std::filesystem::path>::failure("HOME is not set");
}

Result<std::filesystem::path> ensure_dir(const std::filesystem::path &path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    return Result<std::filesystem::path>::failure("Failed to create directory: " +
                                                  path.string() + ": " + ec.message());
  }
  return Result<std::filesystem::path>::success(path);
}

std::string expand_path(std::string value) {
  if (value.empty()) {
    return value;
  }

  if (value[0] == '~') {
    if (auto home = home_dir(); home.ok()) {
      value.replace(0, 1, home.value().string());
    }
  }

  std::regex env_pattern(R"(\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?)");
  std::smatch match;
  std::string expanded;
  std::string remaining = value;

  while (std::regex_search(remaining, match, env_pattern)) {
    expanded += match.prefix().str();
    const std::string var_name = match[1].str();
    if (const char *var = std::getenv(var_name.c_str()); var != nullptr) {
      expanded += var;
    }
    remaining = match.suffix().str();
  }

  expanded += remaining;
  return expanded;
}

bool is_subpath(const std::filesystem::path &candidate, const std::filesystem::path &parent) {
  auto c_it = candidate.begin();
  auto p_it = parent.begin();

  for (; p_it != parent.end(); ++p_it, ++c_it) {
    if (c_it == candidate.end() || *c_it != *p_it) {
      return false;
    }
  }

  return true;
}

} // namespace ghostclaw::common
