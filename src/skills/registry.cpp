#include "ghostclaw/skills/registry.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/skills/loader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <set>

namespace ghostclaw::skills {

namespace {

common::Result<std::filesystem::path> ensure_directory(const std::filesystem::path &path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    return common::Result<std::filesystem::path>::failure("failed to create directory: " +
                                                           path.string() + ": " + ec.message());
  }
  return common::Result<std::filesystem::path>::success(path);
}

bool has_skill_manifest(const std::filesystem::path &path) {
  return std::filesystem::exists(path / "SKILL.md") || std::filesystem::exists(path / "SKILL.toml");
}

std::string shell_quote(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 4);
  out.push_back('\'');
  for (const char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
      continue;
    }
    out.push_back(ch);
  }
  out.push_back('\'');
  return out;
}

bool command_exists(const std::string &name) {
  const std::string command = "command -v " + shell_quote(name) + " >/dev/null 2>&1";
  return std::system(command.c_str()) == 0;
}

std::string normalize_repo_url(const std::string &repo) {
  const std::string trimmed = common::trim(repo);
  if (trimmed.empty()) {
    return "";
  }
  if (trimmed.find("://") != std::string::npos || trimmed.rfind("git@", 0) == 0) {
    return trimmed;
  }

  const auto slash = trimmed.find('/');
  if (slash != std::string::npos && slash > 0 && slash + 1 < trimmed.size()) {
    return "https://github.com/" + trimmed + ".git";
  }
  return trimmed;
}

std::filesystem::path make_temp_clone_path() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return std::filesystem::temp_directory_path() /
         ("ghostclaw-skills-sync-" + std::to_string(ticks));
}

common::Status copy_tree(const std::filesystem::path &from, const std::filesystem::path &to) {
  std::error_code ec;
  std::filesystem::create_directories(to, ec);
  if (ec) {
    return common::Status::error("failed to create destination directory: " + ec.message());
  }

  std::filesystem::copy(from, to,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        ec);
  if (ec) {
    return common::Status::error("failed to copy skill tree: " + ec.message());
  }

  return common::Status::success();
}

double score_skill(const Skill &skill, const std::string &query_lower,
                   const std::vector<std::string> &terms) {
  double score = 0.0;

  const std::string name_lower = common::to_lower(skill.name);
  const std::string desc_lower = common::to_lower(skill.description);

  if (name_lower == query_lower) {
    score += 100.0;
  } else if (name_lower.find(query_lower) != std::string::npos) {
    score += 65.0;
  }

  if (!query_lower.empty() && desc_lower.find(query_lower) != std::string::npos) {
    score += 25.0;
  }

  for (const auto &term : terms) {
    if (term.empty()) {
      continue;
    }
    if (name_lower.find(term) != std::string::npos) {
      score += 12.0;
    }
    if (desc_lower.find(term) != std::string::npos) {
      score += 6.0;
    }
    for (const auto &tag : skill.tags) {
      if (common::to_lower(tag).find(term) != std::string::npos) {
        score += 3.0;
      }
    }
  }

  if (skill.source == SkillSource::Workspace) {
    score += 1.0;
  }
  return score;
}
std::filesystem::path bundled_skills_dir() {
  // Try GHOSTCLAW_ROOT env var first
  const char *root = std::getenv("GHOSTCLAW_ROOT");
  if (root != nullptr) {
    auto p = std::filesystem::path(root) / "skills";
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) {
      return p;
    }
  }
  // Try relative to CWD (dev builds)
  std::error_code ec;
  auto cwd = std::filesystem::current_path(ec) / "skills";
  if (!ec && std::filesystem::is_directory(cwd, ec)) {
    return cwd;
  }
  return {}; // empty = not found
}

} // namespace

SkillRegistry::SkillRegistry(std::filesystem::path workspace_skills_dir,
                             std::optional<std::filesystem::path> community_skills_dir)
    : workspace_skills_dir_(std::move(workspace_skills_dir)) {
  if (community_skills_dir.has_value()) {
    community_skills_dir_ = *community_skills_dir;
  } else {
    community_skills_dir_ = workspace_skills_dir_.parent_path() / ".community-skills";
  }
}

common::Result<std::vector<Skill>> SkillRegistry::list_dir(const std::filesystem::path &root,
                                                           const SkillSource source) const {
  auto ensured = ensure_directory(root);
  if (!ensured.ok()) {
    return common::Result<std::vector<Skill>>::failure(ensured.error());
  }

  std::vector<Skill> out;
  for (const auto &entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_directory()) {
      continue;
    }
    if (!has_skill_manifest(entry.path())) {
      continue;
    }

    auto loaded = SkillLoader::load_skill(entry.path(), {.source = source});
    if (!loaded.ok()) {
      continue;
    }
    out.push_back(std::move(loaded.value()));
  }

  std::sort(out.begin(), out.end(), [](const Skill &a, const Skill &b) {
    return common::to_lower(a.name) < common::to_lower(b.name);
  });
  return common::Result<std::vector<Skill>>::success(std::move(out));
}

common::Result<std::vector<Skill>> SkillRegistry::list() const { return list_workspace(); }

common::Result<std::vector<Skill>> SkillRegistry::list_workspace() const {
  return list_dir(workspace_skills_dir_, SkillSource::Workspace);
}

common::Result<std::vector<Skill>> SkillRegistry::list_community() const {
  return list_dir(community_skills_dir_, SkillSource::Community);
}

common::Result<std::vector<Skill>> SkillRegistry::list_all() const {
  auto local = list_workspace();
  if (!local.ok()) {
    return common::Result<std::vector<Skill>>::failure(local.error());
  }
  auto community = list_community();
  if (!community.ok()) {
    return common::Result<std::vector<Skill>>::failure(community.error());
  }

  // Bundled skills (lowest priority)
  std::vector<Skill> bundled;
  const auto bundled_dir = bundled_skills_dir();
  if (!bundled_dir.empty()) {
    std::error_code ec;
    if (std::filesystem::is_directory(bundled_dir, ec)) {
      for (const auto &entry : std::filesystem::directory_iterator(bundled_dir, ec)) {
        if (!entry.is_directory()) {
          continue;
        }
        if (!has_skill_manifest(entry.path())) {
          continue;
        }
        auto loaded = SkillLoader::load_skill(entry.path(), {.source = SkillSource::Bundled});
        if (loaded.ok()) {
          bundled.push_back(std::move(loaded.value()));
        }
      }
    }
  }

  std::unordered_map<std::string, Skill> by_name;
  // Bundled first (lowest priority, will be overridden)
  for (auto &skill : bundled) {
    by_name[common::to_lower(skill.name)] = std::move(skill);
  }
  for (auto &skill : community.value()) {
    by_name[common::to_lower(skill.name)] = std::move(skill);
  }
  for (auto &skill : local.value()) {
    by_name[common::to_lower(skill.name)] = std::move(skill);
  }

  std::vector<Skill> out;
  out.reserve(by_name.size());
  for (auto &[name, skill] : by_name) {
    (void)name;
    out.push_back(std::move(skill));
  }
  std::sort(out.begin(), out.end(), [](const Skill &a, const Skill &b) {
    return common::to_lower(a.name) < common::to_lower(b.name);
  });
  return common::Result<std::vector<Skill>>::success(std::move(out));
}

common::Result<std::vector<SkillSearchResult>>
SkillRegistry::search(const std::string &query, const bool include_community) const {
  const std::string normalized_query = common::to_lower(common::trim(query));
  if (normalized_query.empty()) {
    return common::Result<std::vector<SkillSearchResult>>::failure("search query is required");
  }

  auto listed = list_all();
  if (!listed.ok()) {
    return common::Result<std::vector<SkillSearchResult>>::failure(listed.error());
  }

  std::vector<Skill> candidates;
  candidates.reserve(listed.value().size());
  for (const auto &skill : listed.value()) {
    if (!include_community && skill.source == SkillSource::Community) {
      continue;
    }
    candidates.push_back(skill);
  }

  std::istringstream stream(normalized_query);
  std::vector<std::string> terms;
  std::string term;
  while (stream >> term) {
    terms.push_back(term);
  }

  std::vector<SkillSearchResult> results;
  for (const auto &skill : candidates) {
    const double score = score_skill(skill, normalized_query, terms);
    if (score <= 0.0) {
      continue;
    }
    results.push_back({.skill = skill, .score = score});
  }

  std::sort(results.begin(), results.end(), [](const SkillSearchResult &a,
                                               const SkillSearchResult &b) {
    if (a.score == b.score) {
      return common::to_lower(a.skill.name) < common::to_lower(b.skill.name);
    }
    return a.score > b.score;
  });

  return common::Result<std::vector<SkillSearchResult>>::success(std::move(results));
}

common::Result<std::optional<Skill>> SkillRegistry::find(const std::string &name,
                                                         const bool include_community) const {
  const std::string target = common::to_lower(common::trim(name));
  if (target.empty()) {
    return common::Result<std::optional<Skill>>::failure("skill name is required");
  }

  auto listed = include_community ? list_all() : list_workspace();
  if (!listed.ok()) {
    return common::Result<std::optional<Skill>>::failure(listed.error());
  }

  for (const auto &skill : listed.value()) {
    if (common::to_lower(skill.name) == target) {
      return common::Result<std::optional<Skill>>::success(skill);
    }
  }

  return common::Result<std::optional<Skill>>::success(std::nullopt);
}

common::Result<bool> SkillRegistry::install_from_loaded(const Skill &loaded,
                                                         const std::filesystem::path &source_dir) {
  auto ensured = ensure_directory(workspace_skills_dir_);
  if (!ensured.ok()) {
    return common::Result<bool>::failure(ensured.error());
  }

  const std::string normalized_name = common::trim(loaded.name);
  if (normalized_name.empty()) {
    return common::Result<bool>::failure("skill name is empty");
  }

  const auto dest = workspace_skills_dir_ / normalized_name;
  if (std::filesystem::exists(dest)) {
    return common::Result<bool>::success(false);
  }

  auto status = copy_tree(source_dir, dest);
  if (!status.ok()) {
    return common::Result<bool>::failure(status.error());
  }

  return common::Result<bool>::success(true);
}

common::Result<bool> SkillRegistry::install(const std::filesystem::path &source_dir) {
  if (!std::filesystem::exists(source_dir) || !std::filesystem::is_directory(source_dir)) {
    return common::Result<bool>::failure("source skill directory not found");
  }

  auto loaded = SkillLoader::load_skill(source_dir, {.source = SkillSource::Workspace});
  if (!loaded.ok()) {
    return common::Result<bool>::failure(loaded.error());
  }

  return install_from_loaded(loaded.value(), source_dir);
}

common::Result<bool> SkillRegistry::install(const std::string &name_or_path,
                                            const bool prefer_community) {
  const std::filesystem::path candidate_path(name_or_path);
  if (std::filesystem::exists(candidate_path) && std::filesystem::is_directory(candidate_path)) {
    return install(candidate_path);
  }

  if (!prefer_community) {
    return common::Result<bool>::failure("skill path not found: " + name_or_path);
  }

  auto community = list_community();
  if (!community.ok()) {
    return common::Result<bool>::failure(community.error());
  }

  const std::string target = common::to_lower(common::trim(name_or_path));
  for (const auto &skill : community.value()) {
    if (common::to_lower(skill.name) == target) {
      if (!skill.location.has_value()) {
        return common::Result<bool>::failure("community skill is missing source location");
      }
      return install_from_loaded(skill, *skill.location);
    }
  }

  return common::Result<bool>::failure("community skill not found: " + name_or_path);
}

common::Result<bool> SkillRegistry::remove(const std::string &name) {
  const std::string trimmed = common::trim(name);
  if (trimmed.empty()) {
    return common::Result<bool>::failure("skill name is required");
  }

  const auto path = workspace_skills_dir_ / trimmed;
  if (!std::filesystem::exists(path)) {
    return common::Result<bool>::success(false);
  }
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
  if (ec) {
    return common::Result<bool>::failure("failed to remove skill: " + ec.message());
  }
  return common::Result<bool>::success(true);
}

common::Result<std::size_t> SkillRegistry::sync_github(const std::string &repo,
                                                        const std::string &branch,
                                                        const std::string &skills_subdir,
                                                        const bool prune_missing) {
  const std::string trimmed_repo = common::trim(repo);
  if (trimmed_repo.empty()) {
    return common::Result<std::size_t>::failure("repository argument is required");
  }

  auto ensured = ensure_directory(community_skills_dir_);
  if (!ensured.ok()) {
    return common::Result<std::size_t>::failure(ensured.error());
  }

  std::filesystem::path repo_root;
  std::filesystem::path temp_clone_dir;
  bool cleanup_temp = false;

  const std::filesystem::path local_repo(trimmed_repo);
  if (std::filesystem::exists(local_repo) && std::filesystem::is_directory(local_repo)) {
    repo_root = std::filesystem::canonical(local_repo);
  } else {
    if (!command_exists("git")) {
      return common::Result<std::size_t>::failure("git is required for GitHub sync");
    }

    temp_clone_dir = make_temp_clone_path();
    const std::string repo_url = normalize_repo_url(trimmed_repo);
    const std::string clone_command =
        "git clone --depth 1 --branch " + shell_quote(common::trim(branch).empty() ? "main" : branch) +
        " " + shell_quote(repo_url) + " " + shell_quote(temp_clone_dir.string()) +
        " >/dev/null 2>&1";
    if (std::system(clone_command.c_str()) != 0) {
      return common::Result<std::size_t>::failure("failed to clone repository: " + repo_url);
    }
    repo_root = temp_clone_dir;
    cleanup_temp = true;
  }

  std::size_t imported = 0;
  std::set<std::string> synced_names;

  std::filesystem::path skills_root = repo_root;
  const std::string dir = common::trim(skills_subdir);
  if (!dir.empty()) {
    skills_root = repo_root / dir;
  }

  if (!std::filesystem::exists(skills_root) || !std::filesystem::is_directory(skills_root)) {
    if (cleanup_temp) {
      std::error_code ec;
      std::filesystem::remove_all(temp_clone_dir, ec);
    }
    return common::Result<std::size_t>::failure("skills directory not found in repository: " +
                                                 skills_root.string());
  }

  for (const auto &entry : std::filesystem::directory_iterator(skills_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    if (!has_skill_manifest(entry.path())) {
      continue;
    }

    auto loaded = SkillLoader::load_skill(entry.path(), {.source = SkillSource::Community});
    if (!loaded.ok()) {
      continue;
    }

    const std::string skill_name = common::trim(loaded.value().name);
    if (skill_name.empty()) {
      continue;
    }

    const std::filesystem::path dest = community_skills_dir_ / skill_name;
    std::error_code rm_ec;
    std::filesystem::remove_all(dest, rm_ec);
    auto copy_status = copy_tree(entry.path(), dest);
    if (!copy_status.ok()) {
      if (cleanup_temp) {
        std::error_code ec;
        std::filesystem::remove_all(temp_clone_dir, ec);
      }
      return common::Result<std::size_t>::failure(copy_status.error());
    }

    synced_names.insert(skill_name);
    ++imported;
  }

  if (prune_missing) {
    for (const auto &entry : std::filesystem::directory_iterator(community_skills_dir_)) {
      if (!entry.is_directory()) {
        continue;
      }
      const std::string name = entry.path().filename().string();
      if (!synced_names.contains(name)) {
        std::error_code ec;
        std::filesystem::remove_all(entry.path(), ec);
      }
    }
  }

  if (cleanup_temp) {
    std::error_code ec;
    std::filesystem::remove_all(temp_clone_dir, ec);
  }

  return common::Result<std::size_t>::success(imported);
}

} // namespace ghostclaw::skills
