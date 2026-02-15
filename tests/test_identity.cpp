#include "test_framework.hpp"

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/identity/aieos.hpp"
#include "ghostclaw/identity/factory.hpp"
#include "ghostclaw/identity/identity.hpp"
#include "ghostclaw/identity/openclaw.hpp"
#include "ghostclaw/identity/templates.hpp"

#include <filesystem>
#include <fstream>
#include <random>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto path = std::filesystem::temp_directory_path() /
                    ("ghostclaw-identity-test-" + std::to_string(rng()));
  std::filesystem::create_directories(path);
  return path;
}

void write_file(const std::filesystem::path &path, const std::string &content) {
  std::ofstream out(path, std::ios::trunc);
  out << content;
}

} // namespace

void register_identity_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace id = ghostclaw::identity;

  tests.push_back({"identity_format_parse", [] {
                     require(id::parse_identity_format("openclaw") == id::IdentityFormat::OpenClaw,
                             "openclaw should parse");
                     require(id::parse_identity_format("aieos") == id::IdentityFormat::Aieos,
                             "aieos should parse");
                   }});

  tests.push_back({"identity_openclaw_loads_files", [] {
                     const auto ws = make_temp_dir();
                     write_file(ws / "IDENTITY.md", "# GhostClawX\nidentity");
                     write_file(ws / "SOUL.md", "Soul text");
                     write_file(ws / "AGENTS.md", "Agent directives");
                     write_file(ws / "USER.md", "User context");
                     write_file(ws / "TOOLS.md", "Tool guidance");

                     auto loaded = id::OpenClawLoader::load(ws);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().name == "GhostClawX", "name should come from heading");
                     require(loaded.value().raw_system_prompt.find("Soul text") != std::string::npos,
                             "prompt should include soul");
                   }});

  tests.push_back({"identity_openclaw_missing_files_ok", [] {
                     const auto ws = make_temp_dir();
                     write_file(ws / "IDENTITY.md", "# Ghost");
                     auto loaded = id::OpenClawLoader::load(ws);
                     require(loaded.ok(), loaded.error());
                     require(!loaded.value().raw_system_prompt.empty(),
                             "prompt should include available identity file");
                   }});

  tests.push_back({"identity_openclaw_truncates_large_file", [] {
                     const auto ws = make_temp_dir();
                     std::string huge(30 * 1024, 'a');
                     write_file(ws / "SOUL.md", huge);
                     auto loaded = id::OpenClawLoader::load(ws);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().raw_system_prompt.find("[... truncated ...]") !=
                                 std::string::npos,
                             "large file should be truncated");
                   }});

  tests.push_back({"identity_aieos_inline_loads", [] {
                     const std::string json =
                         R"({"identity":{"first":"Ghost","last":"Claw","nickname":"GC","bio":"AI helper"},"psychology":{"traits":{"mbti":"INTJ"},"moral_compass":{"alignment":"Neutral Good"}},"motivations":{"core_drive":"assist users"},"capabilities":{"skills":["debugging","automation"],"limitations":["no secrets"]}})";
                     auto loaded = id::AieosLoader::load_from_string(json);
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().name.find("Ghost Claw") != std::string::npos,
                             "aieos should compose full name");
                     require(loaded.value().raw_system_prompt.find("INTJ") != std::string::npos,
                             "aieos prompt should include mbti");
                   }});

  tests.push_back({"identity_factory_selects_loader", [] {
                     const auto ws = make_temp_dir();
                     write_file(ws / "IDENTITY.md", "# Factory Ghost");

                     ghostclaw::config::IdentityConfig cfg;
                     cfg.format = "openclaw";
                     auto openclaw_loaded = id::load_identity(cfg, ws);
                     require(openclaw_loaded.ok(), openclaw_loaded.error());
                     require(openclaw_loaded.value().name == "Factory Ghost", "openclaw factory mismatch");

                     cfg.format = "aieos";
                     cfg.aieos_inline =
                         R"({"identity":{"first":"A","last":"I"},"psychology":{"traits":{"mbti":"ENTP"}}})";
                     auto aieos_loaded = id::load_identity(cfg, ws);
                     require(aieos_loaded.ok(), aieos_loaded.error());
                     require(aieos_loaded.value().name == "A I", "aieos factory mismatch");
                   }});

  tests.push_back({"identity_templates_create_defaults", [] {
                     const auto ws = make_temp_dir();
                     auto status = id::templates::create_default_identity_files(ws);
                     require(status.ok(), status.error());
                     require(std::filesystem::exists(ws / "SOUL.md"), "SOUL should be created");
                     require(std::filesystem::exists(ws / "IDENTITY.md"),
                             "IDENTITY should be created");
                   }});
}
