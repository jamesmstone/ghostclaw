#include "test_framework.hpp"

#include "ghostclaw/config/config.hpp"

#include <filesystem>
#include <optional>
#include <random>

namespace {

struct EnvGuard {
  std::string key;
  std::optional<std::string> old_value;

  EnvGuard(std::string key_, std::optional<std::string> value) : key(std::move(key_)) {
    if (const char *existing = std::getenv(key.c_str()); existing != nullptr) {
      old_value = std::string(existing);
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
                    ("ghostclaw-config-int-home-" + std::to_string(rng()));
  std::filesystem::create_directories(path);
  return path;
}

} // namespace

void register_config_integration_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;

  tests.push_back({"config_integration_load_save_reload", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());

                     ghostclaw::config::Config config;
                     config.default_provider = "anthropic";
                     config.default_model = "claude-sonnet";
                     config.api_key = "abc123";

                     auto saved = ghostclaw::config::save_config(config);
                     require(saved.ok(), saved.error());

                     auto loaded = ghostclaw::config::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().default_provider == "anthropic",
                             "provider should persist");
                     require(loaded.value().api_key.has_value(), "api_key should persist");
                   }});
}
