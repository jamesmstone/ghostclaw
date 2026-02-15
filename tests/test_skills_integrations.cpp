#include "test_framework.hpp"

#include "ghostclaw/integrations/registry.hpp"
#include "ghostclaw/skills/import_openclaw.hpp"
#include "ghostclaw/skills/loader.hpp"
#include "ghostclaw/skills/registry.hpp"

#include <filesystem>
#include <fstream>
#include <random>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto base = std::filesystem::temp_directory_path() /
                    ("ghostclaw-skills-test-" + std::to_string(rng()));
  std::filesystem::create_directories(base);
  return base;
}

void write_file(const std::filesystem::path &path, const std::string &content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  out << content;
}

} // namespace

void register_skills_integrations_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace sk = ghostclaw::skills;
  namespace ig = ghostclaw::integrations;

  tests.push_back({"skills_load_toml_manifest", [] {
                     const auto dir = make_temp_dir() / "skill-a";
                     write_file(dir / "SKILL.toml",
                                "name = \"skill-a\"\n"
                                "description = \"Skill A\"\n"
                                "version = \"1.2.3\"\n"
                                "author = \"Alice\"\n"
                                "tags = [\"one\", \"two\"]\n");
                     write_file(dir / "SKILL.md", "Prompt text");

                     auto loaded = sk::SkillLoader::load_skill_toml(dir);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().name == "skill-a", "name mismatch");
                     require(loaded.value().description == "Skill A", "description mismatch");
                     require(loaded.value().version == "1.2.3", "version mismatch");
                     require(loaded.value().author.has_value(), "author should exist");
                   }});

  tests.push_back({"skills_load_md_frontmatter", [] {
                     const auto dir = make_temp_dir() / "skill-b";
                     write_file(dir / "SKILL.md",
                                "---\n"
                                "name: skill-b\n"
                                "description: Skill B\n"
                                "version: 2.0.0\n"
                                "---\n"
                                "Body prompt");

                     auto loaded = sk::SkillLoader::load_skill_md(dir);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().name == "skill-b", "frontmatter name mismatch");
                     require(!loaded.value().prompts.empty(), "prompt should be loaded");
                   }});

  tests.push_back({"skills_load_md_complex_yaml_metadata_block", [] {
                     const auto dir = make_temp_dir() / "skill-metadata";
                     write_file(dir / "SKILL.md",
                                "---\n"
                                "name: skill-metadata\n"
                                "description: Metadata test skill\n"
                                "metadata:\n"
                                "  {\n"
                                "    \"openclaw\": {\n"
                                "      \"emoji\": \"tool\",\n"
                                "      \"requires\": { \"bins\": [\"jq\"] }\n"
                                "    }\n"
                                "  }\n"
                                "---\n"
                                "Body prompt");

                     auto loaded = sk::SkillLoader::load_skill_md(dir);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().name == "skill-metadata", "name mismatch");
                     require(loaded.value().description == "Metadata test skill",
                             "description mismatch");
                   }});

  tests.push_back({"skills_load_md_toml_frontmatter_tool_and_install_specs", [] {
                     const auto dir = make_temp_dir() / "skill-c";
                     write_file(
                         dir / "SKILL.md",
                         "+++\n"
                         "name = \"skill-c\"\n"
                         "description = \"Skill C\"\n"
                         "tags = [\"automation\", \"ops\"]\n"
                         "[[tools]]\n"
                         "name = \"doctor\"\n"
                         "description = \"Run diagnostics\"\n"
                         "kind = \"shell\"\n"
                         "command = \"ghostclaw\"\n"
                         "args = [\"doctor\"]\n"
                         "[[install]]\n"
                         "id = \"jq\"\n"
                         "kind = \"brew\"\n"
                         "formula = \"jq\"\n"
                         "+++\n"
                         "## Instructions\n"
                         "Use this skill for diagnostics.\n");

                     auto loaded = sk::SkillLoader::load_skill_md(dir);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().name == "skill-c", "name mismatch");
                     require(loaded.value().tools.size() == 1, "tool spec should be loaded");
                     require(loaded.value().tools[0].name == "doctor", "tool name mismatch");
                     require(loaded.value().install_specs.size() == 1,
                             "install spec should be loaded");
                     require(loaded.value().install_specs[0].kind == "brew",
                             "install kind mismatch");
                     require(loaded.value().instructions_markdown.find("diagnostics") !=
                                 std::string::npos,
                             "instructions should be extracted");
                   }});

  tests.push_back({"skills_registry_install_list_remove", [] {
                     const auto root = make_temp_dir();
                     const auto source = root / "src-skill";
                     write_file(source / "SKILL.toml",
                                "name = \"registry-skill\"\n"
                                "description = \"Registry test\"\n");

                     sk::SkillRegistry registry(root / "workspace-skills");
                     auto installed = registry.install(source);
                     require(installed.ok(), installed.error());
                     require(installed.value(), "install should return true");

                     auto listed = registry.list();
                     require(listed.ok(), listed.error());
                     require(!listed.value().empty(), "registry should list installed skill");

                     auto removed = registry.remove("registry-skill");
                     require(removed.ok(), removed.error());
                     require(removed.value(), "remove should return true");
                   }});

  tests.push_back({"skills_registry_sync_search_and_install_from_community", [] {
                     const auto root = make_temp_dir();
                     const auto repo = root / "repo";
                     const auto remote_skill = repo / "skills" / "community-skill";
                     write_file(remote_skill / "SKILL.toml",
                                "name = \"community-skill\"\n"
                                "description = \"Community synced skill\"\n"
                                "tags = [\"community\", \"sync\"]\n");

                     sk::SkillRegistry registry(root / "workspace-skills");

                     auto synced = registry.sync_github(repo.string(), "main", "skills", false);
                     require(synced.ok(), synced.error());
                     require(synced.value() == 1, "expected one synced skill");

                     auto community = registry.list_community();
                     require(community.ok(), community.error());
                     require(community.value().size() == 1, "community listing should have one skill");

                     auto search = registry.search("community", true);
                     require(search.ok(), search.error());
                     require(!search.value().empty(), "search should return community skill");
                     require(search.value()[0].skill.name == "community-skill",
                             "search result mismatch");

                     auto installed = registry.install("community-skill", true);
                     require(installed.ok(), installed.error());
                     require(installed.value(), "community install should succeed");

                     auto local = registry.list_workspace();
                     require(local.ok(), local.error());
                     require(!local.value().empty(), "workspace list should include installed skill");
                   }});

  tests.push_back({"skills_import_openclaw_copies_and_normalizes", [] {
                     const auto root = make_temp_dir();
                     const auto source = root / "references" / "openclaw";
                     write_file(source / "skills" / "alpha" / "SKILL.md",
                                "---\nname: alpha\ndescription: Alpha skill\n---\nUse alpha");
                     write_file(source / "skills" / "alpha" / "references" / "guide.md", "guide");
                     write_file(source / "extensions" / "pkg" / "skills" / "beta" / "SKILL.md",
                                "---\nname: beta\ndescription: Beta skill\n---\nUse beta");
                     write_file(source / ".agents" / "skills" / "gamma" / "SKILL.md",
                                "---\nname: gamma\ndescription: Gamma skill\n---\nUse gamma");

                     sk::OpenClawImportOptions options;
                     options.destination_root = root / "workspace" / "skills";
                     options.sources = {
                         {.path = source / "skills", .label = "core"},
                         {.path = source / "extensions", .label = "extensions"},
                         {.path = source / ".agents" / "skills", .label = "agents"},
                     };

                     auto imported = sk::import_openclaw_skills(options);
                     require(imported.ok(), imported.error());
                     require(imported.value().imported == 3, "expected three imported skills");

                     sk::SkillRegistry registry(root / "workspace" / "skills");
                     auto listed = registry.list_workspace();
                     require(listed.ok(), listed.error());
                     require(listed.value().size() == 3, "workspace should include imported skills");
                   }});

  tests.push_back({"integrations_registry_catalog_lookup", [] {
                     ig::IntegrationRegistry registry;
                     require(registry.all().size() >= 50, "catalog should include 50+ integrations");
                     auto slack = registry.find("slack");
                     require(slack.has_value(), "slack integration should exist");
                     auto chat_items = registry.by_category("chat");
                     require(!chat_items.empty(), "chat category should not be empty");
                   }});
}
