#pragma once

#include "ghostclaw/common/result.hpp"
#include "ghostclaw/identity/identity.hpp"

#include <filesystem>
#include <string>

namespace ghostclaw::identity {

class OpenClawLoader {
public:
  static constexpr std::size_t MAX_FILE_SIZE = 20 * 1024;

  [[nodiscard]] static common::Result<Identity> load(const std::filesystem::path &workspace);

private:
  [[nodiscard]] static std::string read_identity_file(const std::filesystem::path &workspace,
                                                      std::string_view filename);
  [[nodiscard]] static std::string truncate_content(const std::string &content,
                                                    std::size_t max_size);
};

} // namespace ghostclaw::identity
