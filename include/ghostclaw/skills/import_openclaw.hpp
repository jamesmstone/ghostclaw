#pragma once

#include "ghostclaw/common/result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ghostclaw::skills {

struct OpenClawImportSource {
  std::filesystem::path path;
  std::string label;
};

struct OpenClawImportOptions {
  std::filesystem::path destination_root;
  std::vector<OpenClawImportSource> sources;
  bool overwrite_existing = true;
};

struct OpenClawImportResult {
  std::size_t scanned = 0;
  std::size_t imported = 0;
  std::size_t skipped = 0;
  std::vector<std::string> warnings;
};

[[nodiscard]] common::Result<OpenClawImportResult>
import_openclaw_skills(const OpenClawImportOptions &options);

} // namespace ghostclaw::skills
