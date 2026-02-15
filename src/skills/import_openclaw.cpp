#include "ghostclaw/skills/import_openclaw.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/common/toml.hpp"
#include "ghostclaw/skills/loader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <unordered_set>

namespace ghostclaw::skills {

namespace {

std::uint64_t fnv1a64(const std::string &text) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char ch : text) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string hex_u64(std::uint64_t value) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[static_cast<std::size_t>(i)] = kHex[value & 0x0fU];
    value >>= 4U;
  }
  return out;
}

std::string slugify(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  bool prev_dash = false;
  for (const char ch : input) {
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isalnum(uch) != 0) {
      out.push_back(static_cast<char>(std::tolower(uch)));
      prev_dash = false;
      continue;
    }
    if (!prev_dash) {
      out.push_back('-');
      prev_dash = true;
    }
  }

  while (!out.empty() && out.front() == '-') {
    out.erase(out.begin());
  }
  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }

  if (out.empty()) {
    return "skill";
  }
  return out;
}

std::string detect_compatibility_hint(const std::string &instructions) {
  const std::string lower = common::to_lower(instructions);
  if (lower.find("openclaw") != std::string::npos) {
    return "Replace openclaw command references with ghostclaw equivalents.";
  }
  return "";
}

common::Status copy_selected_entries(const std::filesystem::path &src_dir,
                                     const std::filesystem::path &dest_dir) {
  std::error_code ec;
  std::filesystem::create_directories(dest_dir, ec);
  if (ec) {
    return common::Status::error("failed to create import destination: " + ec.message());
  }

  const std::array<std::string, 4> kDirs = {"scripts", "references", "assets", "agents"};

  if (std::filesystem::exists(src_dir / "SKILL.md")) {
    std::filesystem::copy_file(src_dir / "SKILL.md", dest_dir / "SKILL.md",
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      return common::Status::error("failed to copy SKILL.md: " + ec.message());
    }
  }

  for (const auto &dir_name : kDirs) {
    const auto from = src_dir / dir_name;
    if (!std::filesystem::exists(from) || !std::filesystem::is_directory(from)) {
      continue;
    }
    std::filesystem::copy(from, dest_dir / dir_name,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
      return common::Status::error("failed to copy " + from.string() + ": " + ec.message());
    }
  }

  return common::Status::success();
}

void write_normalized_manifest(const std::filesystem::path &dest_dir, const Skill &loaded,
                               const std::string &source_label,
                               const std::string &source_relative) {
  std::ofstream out(dest_dir / "SKILL.toml", std::ios::trunc);
  if (!out) {
    return;
  }

  std::vector<std::string> tags = loaded.tags;
  tags.push_back("openclaw");
  tags.push_back("openclaw-import");
  if (!source_label.empty()) {
    tags.push_back(source_label);
  }
  std::sort(tags.begin(), tags.end());
  tags.erase(std::unique(tags.begin(), tags.end()), tags.end());

  out << "name = " << common::quote_toml_string(loaded.name) << "\n";
  out << "description = " << common::quote_toml_string(
      loaded.description.empty() ? "Imported OpenClaw skill" : loaded.description) << "\n";
  out << "version = " << common::quote_toml_string("1.0.0") << "\n";
  out << "tags = [";
  for (std::size_t i = 0; i < tags.size(); ++i) {
    if (i > 0) {
      out << ", ";
    }
    out << common::quote_toml_string(tags[i]);
  }
  out << "]\n";
  out << "\n[metadata]\n";
  out << "source = " << common::quote_toml_string("openclaw-import") << "\n";
  out << "openclaw_source = " << common::quote_toml_string(source_relative) << "\n";
  out << "openclaw_group = " << common::quote_toml_string(source_label) << "\n";
  const std::string compat = detect_compatibility_hint(loaded.instructions_markdown);
  if (!compat.empty()) {
    out << "compatibility = " << common::quote_toml_string(compat) << "\n";
  }
}

std::vector<std::filesystem::path> discover_skill_dirs(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> dirs;
  std::error_code ec;
  if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
    return dirs;
  }

  std::set<std::filesystem::path> unique;
  for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end && !ec; ++it) {
    if (!it->is_regular_file()) {
      continue;
    }
    if (it->path().filename() != "SKILL.md") {
      continue;
    }
    unique.insert(it->path().parent_path());
  }

  dirs.assign(unique.begin(), unique.end());
  std::sort(dirs.begin(), dirs.end());
  return dirs;
}

} // namespace

common::Result<OpenClawImportResult> import_openclaw_skills(const OpenClawImportOptions &options) {
  if (options.destination_root.empty()) {
    return common::Result<OpenClawImportResult>::failure("destination root is required");
  }

  std::error_code ec;
  std::filesystem::create_directories(options.destination_root, ec);
  if (ec) {
    return common::Result<OpenClawImportResult>::failure("failed to create destination root: " +
                                                         ec.message());
  }

  OpenClawImportResult summary;
  std::unordered_set<std::string> used_destinations;

  for (const auto &source : options.sources) {
    const auto candidates = discover_skill_dirs(source.path);
    for (const auto &skill_dir : candidates) {
      ++summary.scanned;

      auto loaded = SkillLoader::load_skill(skill_dir, {.source = SkillSource::Workspace});
      if (!loaded.ok()) {
        summary.warnings.push_back("skip " + skill_dir.string() + ": " + loaded.error());
        ++summary.skipped;
        continue;
      }

      if (common::trim(loaded.value().name).empty()) {
        summary.warnings.push_back("skip " + skill_dir.string() + ": empty skill name");
        ++summary.skipped;
        continue;
      }

      std::error_code rel_ec;
      auto rel = std::filesystem::relative(skill_dir, source.path, rel_ec);
      const std::string relative_str = rel_ec ? skill_dir.filename().string() : rel.string();
      std::string destination_name = slugify(loaded.value().name);

      if (used_destinations.contains(destination_name)) {
        destination_name += "-" + hex_u64(fnv1a64(relative_str)).substr(0, 8);
      }
      used_destinations.insert(destination_name);

      const auto dest_dir = options.destination_root / destination_name;
      if (std::filesystem::exists(dest_dir) && options.overwrite_existing) {
        std::filesystem::remove_all(dest_dir, ec);
        if (ec) {
          return common::Result<OpenClawImportResult>::failure("failed to reset destination " +
                                                               dest_dir.string() + ": " + ec.message());
        }
      }

      auto copied = copy_selected_entries(skill_dir, dest_dir);
      if (!copied.ok()) {
        return common::Result<OpenClawImportResult>::failure(copied.error());
      }

      write_normalized_manifest(dest_dir, loaded.value(), source.label, relative_str);
      ++summary.imported;
    }
  }

  if (summary.scanned == 0) {
    return common::Result<OpenClawImportResult>::failure(
        "no OpenClaw skills discovered under configured source roots");
  }

  return common::Result<OpenClawImportResult>::success(std::move(summary));
}

} // namespace ghostclaw::skills
