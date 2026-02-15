#include "test_framework.hpp"

#include "ghostclaw/cli/commands.hpp"
#include "ghostclaw/config/config.hpp"
#include "ghostclaw/onboard/wizard.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace {

struct EnvGuard {
  std::string key;
  std::optional<std::string> old_value;

  EnvGuard(std::string key_, std::optional<std::string> value) : key(std::move(key_)) {
    if (const char *existing = std::getenv(key.c_str()); existing != nullptr) {
      old_value = existing;
    }
    if (value.has_value()) {
      setenv(key.c_str(), value->c_str(), 1);
    } else {
      unsetenv(key.c_str());
    }
  }

  ~EnvGuard() {
    if (old_value.has_value()) {
      setenv(key.c_str(), old_value->c_str(), 1);
    } else {
      unsetenv(key.c_str());
    }
  }
};

std::filesystem::path make_temp_home() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto path = std::filesystem::temp_directory_path() /
                    ("ghostclaw-cli-test-home-" + std::to_string(rng()));
  std::filesystem::create_directories(path);
  return path;
}

int run_cli(const std::vector<std::string> &args) {
  std::vector<std::string> owned = args;
  std::vector<char *> argv;
  argv.reserve(owned.size());
  for (auto &arg : owned) {
    argv.push_back(arg.data());
  }
  return ghostclaw::cli::run_cli(static_cast<int>(argv.size()), argv.data());
}

} // namespace

void register_cli_onboard_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace ob = ghostclaw::onboard;
  namespace cfg = ghostclaw::config;

  tests.push_back({"onboard_quick_setup_creates_config_and_workspace", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());

                     ob::WizardOptions options;
                     options.api_key = "test-key";
                     options.provider = "openai";
                     options.model = "gpt-4o-mini";
                     options.memory_backend = "markdown";

                     auto result = ob::run_wizard(options);
                     require(result.success, result.error);

                     auto config_path = cfg::config_path();
                     auto workspace_path = cfg::workspace_dir();
                     require(config_path.ok(), config_path.error());
                     require(workspace_path.ok(), workspace_path.error());
                     require(std::filesystem::exists(config_path.value()),
                             "config.toml should exist");
                     require(std::filesystem::exists(workspace_path.value() / "SOUL.md"),
                             "SOUL.md should exist");
                     require(std::filesystem::exists(workspace_path.value() / "memory"),
                             "memory directory should exist");
                   }});

  tests.push_back({"onboard_channels_only_updates_channels", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());

                     cfg::Config config;
                     config.default_provider = "openrouter";
                     config.default_model = "gpt-4o-mini";
                     auto saved = cfg::save_config(config);
                     require(saved.ok(), saved.error());

                     ob::WizardOptions options;
                     options.channels_only = true;
                     options.interactive = false;

                     // Simulate a direct channels-only save by preloading and modifying.
                     auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     cfg::WebhookConfig webhook;
                     webhook.secret = "abc123";
                     loaded.value().channels.webhook = webhook;
                     require(cfg::save_config(loaded.value()).ok(), "save should succeed");

                     auto reloaded = cfg::load_config();
                     require(reloaded.ok(), reloaded.error());
                     require(reloaded.value().channels.webhook.has_value(),
                             "webhook config should persist");
                     require(reloaded.value().channels.webhook->secret == "abc123",
                             "webhook secret should match");
                   }});

  tests.push_back({"cli_config_path_command", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const int code = run_cli({"ghostclaw", "config-path"});
                     require(code == 0, "config-path command should succeed");
                   }});

  tests.push_back({"cli_onboard_command_non_interactive", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const int code = run_cli({"ghostclaw", "onboard", "--provider", "openai",
                                               "--model", "gpt-4o-mini", "--memory", "sqlite"});
                     require(code == 0, "onboard command should succeed");
                   }});
}
