#include "test_framework.hpp"

#include "ghostclaw/skills/registry.hpp"
#include "tests/helpers/test_helpers.hpp"

#include <filesystem>
#include <fstream>

namespace {

void write_file(const std::filesystem::path &path, const std::string &content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  out << content;
}

} // namespace

void register_skills_integration_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;

  tests.push_back({"skills_integration_install_and_discover", [] {
                     ghostclaw::testing::TempWorkspace workspace;
                     const auto source = workspace.path() / "source-skill";
                     write_file(source / "SKILL.toml",
                                "name = \"integration-skill\"\n"
                                "description = \"integration test skill\"\n");
                     write_file(source / "SKILL.md", "# Integration Skill\nDo useful work.");

                     ghostclaw::skills::SkillRegistry registry(workspace.path() / "skills");
                     auto installed = registry.install(source);
                     require(installed.ok(), installed.error());
                     require(installed.value(), "skill should be installed");

                     auto listed = registry.list();
                     require(listed.ok(), listed.error());
                     require(!listed.value().empty(), "installed skill should be discoverable");
                   }});
}
