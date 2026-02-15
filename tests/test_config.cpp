#include "test_framework.hpp"

#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/config/config.hpp"

#include <filesystem>
#include <fstream>
#include <random>

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

struct ConfigOverrideGuard {
  std::optional<std::filesystem::path> old_override;

  explicit ConfigOverrideGuard(std::optional<std::filesystem::path> next = std::nullopt) {
    old_override = ghostclaw::config::config_path_override();
    if (next.has_value()) {
      ghostclaw::config::set_config_path_override(*next);
    } else {
      ghostclaw::config::clear_config_path_override();
    }
  }

  ~ConfigOverrideGuard() {
    if (old_override.has_value()) {
      ghostclaw::config::set_config_path_override(*old_override);
    } else {
      ghostclaw::config::clear_config_path_override();
    }
  }
};

std::filesystem::path make_temp_home() {
  static std::mt19937_64 rng{std::random_device{}()};
  std::filesystem::path path = std::filesystem::temp_directory_path() /
                               ("ghostclaw-test-home-" + std::to_string(rng()));
  std::filesystem::create_directories(path);
  return path;
}

void write_file(const std::filesystem::path &path, const std::string &content) {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream out(path);
  out << content;
}

} // namespace

void register_config_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace cfg = ghostclaw::config;

  tests.push_back({"config_dir_creates_directory", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const auto dir = cfg::config_dir();
                     require(dir.ok(), dir.error());
                     require(std::filesystem::exists(dir.value()), "config directory should exist");
                   }});

  tests.push_back({"load_config_missing_file_returns_defaults", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());

                     const auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().default_provider == "openrouter",
                             "default provider should be openrouter");
                     require(loaded.value().memory.backend == "sqlite", "default backend should be sqlite");
                   }});

  tests.push_back({"load_config_valid_toml", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const ConfigOverrideGuard cfg_override;
                     const auto path = cfg::config_path();
                     require(path.ok(), path.error());

                     write_file(path.value(),
                                R"(
api_key = "key123"
default_provider = "openai"
default_model = "gpt-4.1"
default_temperature = 0.2

[memory]
backend = "markdown"
embedding_provider = "ollama"
vector_weight = 0.6
keyword_weight = 0.4
)");

                     const auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().api_key.has_value(), "api key should exist");
                     require(*loaded.value().api_key == "key123", "api key mismatch");
                     require(loaded.value().default_provider == "openai", "provider mismatch");
                     require(loaded.value().memory.backend == "markdown", "memory backend mismatch");
                     require(loaded.value().memory.embedding_provider == "ollama",
                             "embedding provider mismatch");
                   }});

  tests.push_back({"partial_toml_fills_defaults", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const ConfigOverrideGuard cfg_override;
                     const auto path = cfg::config_path();
                     require(path.ok(), path.error());

                     write_file(path.value(), "default_provider = \"anthropic\"\n");
                     const auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().default_provider == "anthropic", "provider should be overridden");
                     require(loaded.value().gateway.port == 8080, "default port should remain");
                   }});

  tests.push_back({"load_config_legacy_providers_block", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const ConfigOverrideGuard cfg_override;
                     const auto path = cfg::config_path();
                     require(path.ok(), path.error());

                     write_file(path.value(),
                                R"(
[providers]
default = "xai"
default_model = "grok-3-mini-beta"
default_temperature = 0.4

[providers.xai]
api_key = "legacy-provider-key"
)");

                     const auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().default_provider == "xai",
                             "providers.default should map to default_provider");
                     require(loaded.value().default_model == "grok-3-mini-beta",
                             "providers.default_model should map to default_model");
                     require(loaded.value().default_temperature == 0.4,
                             "providers.default_temperature should map to default_temperature");
                     require(loaded.value().api_key.has_value(),
                             "providers.<provider>.api_key should map to api_key");
                     require(*loaded.value().api_key == "legacy-provider-key",
                             "provider api key should load from legacy block");
                   }});

  tests.push_back({"env_override_precedence", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const EnvGuard env_api{"GHOSTCLAW_API_KEY",
                                            std::optional<std::string>("from-env")};
                     const EnvGuard env_provider{"GHOSTCLAW_PROVIDER",
                                                 std::optional<std::string>("openai")};
                     const EnvGuard env_model{"GHOSTCLAW_MODEL",
                                              std::optional<std::string>("gpt-env")};

                     auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().api_key.has_value(), "api key expected from env");
                     require(*loaded.value().api_key == "from-env", "api key env override failed");
                     require(loaded.value().default_provider == "openai", "provider env override failed");
                     require(loaded.value().default_model == "gpt-env", "model env override failed");
                   }});

  tests.push_back({"load_config_dotenv_expands_tokens_and_xai_key", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const ConfigOverrideGuard cfg_override;
                     const EnvGuard clear_xai("XAI_API_KEY", std::nullopt);
                     const EnvGuard clear_token("TEST_TELEGRAM_TOKEN", std::nullopt);

                     const auto cfg_dir = cfg::config_dir();
                     require(cfg_dir.ok(), cfg_dir.error());
                     const auto path = cfg::config_path();
                     require(path.ok(), path.error());

                     write_file(cfg_dir.value() / ".env",
                                "TEST_TELEGRAM_TOKEN=token-from-dotenv\n"
                                "XAI_API_KEY=xai-from-dotenv\n");
                     write_file(path.value(),
                                R"(
default_provider = "xai"

[channels.telegram]
bot_token = "$TEST_TELEGRAM_TOKEN"
)");

                     const auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().channels.telegram.has_value(),
                             "telegram config should be loaded");
                     require(loaded.value().channels.telegram->bot_token == "token-from-dotenv",
                             "bot token should expand from dotenv");
                     require(loaded.value().api_key.has_value(), "xai key should be loaded");
                     require(*loaded.value().api_key == "xai-from-dotenv",
                             "xai key should come from dotenv");
                   }});

  tests.push_back({"save_and_reload_roundtrip", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const ConfigOverrideGuard cfg_override;

                     cfg::Config config;
                     config.api_key = "abc";
                     config.default_provider = "anthropic";
                     config.memory.backend = "markdown";
                     config.autonomy.allowed_commands = {"ls", "cat"};

                     const auto save = cfg::save_config(config);
                     require(save.ok(), save.error());

                     auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().default_provider == "anthropic", "provider mismatch after reload");
                     require(loaded.value().memory.backend == "markdown", "backend mismatch after reload");
                   }});

  tests.push_back({"validate_valid_config_no_errors", [] {
                     cfg::Config config;
                     config.api_key = "sk";
                     config.default_provider = "openai";
                     auto result = cfg::validate_config(config);
                     require(result.ok(), result.error());
                   }});

  tests.push_back({"validate_invalid_temperature", [] {
                     cfg::Config config;
                     config.default_temperature = 9.0;
                     auto result = cfg::validate_config(config);
                     require(!result.ok(), "validation should fail");
                   }});

  tests.push_back({"validate_unknown_runtime", [] {
                     cfg::Config config;
                     config.runtime.kind = "docker";
                     auto result = cfg::validate_config(config);
                     require(!result.ok(), "validation should fail");
                   }});

  tests.push_back({"validate_public_bind_warning", [] {
                     cfg::Config config;
                     config.gateway.allow_public_bind = true;
                     config.tunnel.provider = "none";
                     config.api_key = "x";
                     auto result = cfg::validate_config(config);
                     require(result.ok(), result.error());
                     bool found = false;
                     for (const auto &warning : result.value()) {
                       if (warning.find("public_bind") != std::string::npos) {
                         found = true;
                         break;
                       }
                     }
                     require(found, "expected public bind warning");
                   }});

  tests.push_back({"validate_invalid_websocket_host", [] {
                     cfg::Config config;
                     config.api_key = "x";
                     config.gateway.websocket_host = "!!!";
                     auto result = cfg::validate_config(config);
                     require(!result.ok(), "invalid websocket host should fail");
                   }});

  tests.push_back({"validate_websocket_tls_requires_files", [] {
                     cfg::Config config;
                     config.api_key = "x";
                     config.gateway.websocket_tls_enabled = true;
                     auto result = cfg::validate_config(config);
                     require(!result.ok(), "tls websocket config without cert/key should fail");
                   }});

  tests.push_back({"validate_websocket_tls_accepts_existing_files", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const auto cert = home / "test-cert.pem";
                     const auto key = home / "test-key.pem";
                     write_file(cert, "dummy-cert");
                     write_file(key, "dummy-key");

                     cfg::Config config;
                     config.api_key = "x";
                     config.gateway.websocket_enabled = true;
                     config.gateway.websocket_tls_enabled = true;
                     config.gateway.websocket_tls_cert_file = cert.string();
                     config.gateway.websocket_tls_key_file = key.string();
                     auto result = cfg::validate_config(config);
                     require(result.ok(), result.error());
                   }});

  tests.push_back({"load_config_gateway_send_policy_fields", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const ConfigOverrideGuard cfg_override;
                     const auto path = cfg::config_path();
                     require(path.ok(), path.error());

                     write_file(path.value(),
                                R"(
[gateway]
session_send_policy_enabled = true
session_send_policy_max_per_window = 7
session_send_policy_window_seconds = 12
)");

                     const auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().gateway.session_send_policy_enabled,
                             "send policy should be enabled");
                     require(loaded.value().gateway.session_send_policy_max_per_window == 7,
                             "send policy max should match");
                     require(loaded.value().gateway.session_send_policy_window_seconds == 12,
                             "send policy window should match");
                   }});

  tests.push_back({"validate_gateway_send_policy_rejects_zero_values", [] {
                     cfg::Config config;
                     config.api_key = "x";
                     config.gateway.session_send_policy_enabled = true;
                     config.gateway.session_send_policy_max_per_window = 0;
                     auto first = cfg::validate_config(config);
                     require(!first.ok(), "max_per_window=0 should fail validation");

                     config.gateway.session_send_policy_max_per_window = 5;
                     config.gateway.session_send_policy_window_seconds = 0;
                     auto second = cfg::validate_config(config);
                     require(!second.ok(), "window_seconds=0 should fail validation");
                   }});

  tests.push_back({"load_config_legacy_memory_embeddings_keys", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const ConfigOverrideGuard cfg_override;
                     const auto path = cfg::config_path();
                     require(path.ok(), path.error());

                     write_file(path.value(),
                                R"(
[memory]
backend = "sqlite"

[memory.embeddings]
provider = "noop"
model = "legacy-model"
dimensions = 8
)");

                     const auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().memory.embedding_provider == "noop",
                             "legacy provider key should map to canonical field");
                     require(loaded.value().memory.embedding_model == "legacy-model",
                             "legacy model key should map to canonical field");
                     require(loaded.value().memory.embedding_dimensions == 8,
                             "legacy dimensions key should map to canonical field");
                   }});

  tests.push_back({"config_path_override_supports_custom_file", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const auto custom_path = home / "alt-config" / "gc.toml";
                     const ConfigOverrideGuard cfg_override(custom_path);

                     cfg::Config config;
                     config.default_provider = "anthropic";
                     auto saved = cfg::save_config(config);
                     require(saved.ok(), saved.error());
                     require(std::filesystem::exists(custom_path), "custom config file should be created");

                     auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().default_provider == "anthropic",
                             "load_config should read from override path");
                   }});

  tests.push_back({"config_path_override_supports_env_variable", [] {
                     const auto home = make_temp_home();
                     const EnvGuard env_home("HOME", home.string());
                     const ConfigOverrideGuard cfg_override;
                     const auto custom_path = home / "env-config" / "config.toml";
                     const EnvGuard env_cfg("GHOSTCLAW_CONFIG_PATH", custom_path.string());

                     write_file(custom_path,
                                R"(
default_provider = "openai"
)");

                     auto loaded = cfg::load_config();
                     require(loaded.ok(), loaded.error());
                     require(loaded.value().default_provider == "openai",
                             "env config path should be honored");
                   }});
}
