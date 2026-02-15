#include "ghostclaw/onboard/wizard.hpp"

#include "ghostclaw/auth/oauth.hpp"
#include "ghostclaw/common/fs.hpp"
#include "ghostclaw/config/config.hpp"
#include "ghostclaw/identity/templates.hpp"
#include "ghostclaw/providers/traits.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghostclaw::onboard {

namespace {

// ── ANSI color helpers ───────────────────────────────────────────────────────

constexpr const char *RST = "\033[0m";
constexpr const char *BOLD = "\033[1m";
constexpr const char *DIM = "\033[2m";
constexpr const char *CYAN = "\033[36m";
constexpr const char *GREEN = "\033[32m";
constexpr const char *YELLOW = "\033[33m";
constexpr const char *RED = "\033[31m";
constexpr const char *WHITE = "\033[37m";

bool use_color() {
  static const bool enabled = [] {
    if (std::getenv("NO_COLOR") != nullptr) {
      return false;
    }
    return isatty(fileno(stdout)) != 0;
  }();
  return enabled;
}

std::string green(const std::string &s) { return use_color() ? std::string(GREEN) + s + RST : s; }
std::string red(const std::string &s) { return use_color() ? std::string(RED) + s + RST : s; }
std::string dim(const std::string &s) { return use_color() ? std::string(DIM) + s + RST : s; }
std::string bold_green(const std::string &s) {
  return use_color() ? std::string(BOLD) + GREEN + s + RST : s;
}

void print_step(int current, int total, const std::string &title) {
  std::cout << "\n";
  if (use_color()) {
    std::cout << "  " << BOLD << CYAN << "[" << current << "/" << total << "]" << RST << " "
              << BOLD << title << RST << "\n";
  } else {
    std::cout << "  [" << current << "/" << total << "] " << title << "\n";
  }
}

void print_welcome_banner() {
  if (use_color()) {
    std::cout << "\n";
    std::cout << BOLD << CYAN
              << "  ╔══════════════════════════════════════════════════════════════╗\n"
              << "  ║                                                              ║\n"
              << "  ║" << WHITE << "   " << BOLD
              << "   GhostClaw Setup Wizard                              " << CYAN
              << "║\n"
              << "  ║" << RST << DIM
              << "      Ghost Protocol. Claw Execution. Zero Compromise.        " << BOLD << CYAN
              << "║\n"
              << "  ║                                                              ║\n"
              << "  ╚══════════════════════════════════════════════════════════════╝" << RST
              << "\n";
  } else {
    std::cout << "\n";
    std::cout << "  ================================================================\n";
    std::cout << "       GhostClaw Setup Wizard\n";
    std::cout << "       Ghost Protocol. Claw Execution. Zero Compromise.\n";
    std::cout << "  ================================================================\n";
  }
}

bool provider_needs_key(const std::string &provider) {
  static const std::set<std::string> no_key = {"ollama", "vllm", "litellm", "synthetic"};
  return no_key.find(provider) == no_key.end();
}

// ── Provider / model catalog ──────────────────────────────────────────────────

struct ModelSuggestion {
  std::string name;
  std::string note;
};

const std::vector<MenuGroup> &provider_groups() {
  static const std::vector<MenuGroup> groups = {
      {"Aggregators",
       {
           {"OpenRouter (100+ models, one key) [Recommended]", "openrouter"},
           {"LiteLLM (proxy)", "litellm"},
       }},
      {"Major Cloud",
       {
           {"OpenAI", "openai"},
           {"OpenAI Codex", "openai-codex"},
           {"Anthropic", "anthropic"},
           {"Google Gemini", "google"},
           {"Google Vertex AI", "google-vertex"},
           {"xAI / Grok", "grok"},
           {"Mistral", "mistral"},
           {"DeepSeek", "deepseek"},
       }},
      {"Specialized & Regional",
       {
           {"Groq", "groq"},
           {"Cerebras", "cerebras"},
           {"Perplexity", "perplexity"},
           {"Cohere", "cohere"},
           {"Fireworks AI", "fireworks"},
           {"Together AI", "together"},
           {"NVIDIA NIM", "nvidia"},
           {"Venice AI", "venice"},
           {"Cloudflare Workers AI", "cloudflare"},
           {"GLM (Zhipu)", "glm"},
           {"Qianfan (Baidu)", "qianfan"},
           {"Qwen Portal (DashScope)", "qwen-portal"},
           {"Minimax", "minimax"},
           {"Moonshot AI", "moonshot"},
           {"Kimi Coding", "kimi-coding"},
           {"Xiaomi MiLM", "xiaomi"},
           {"HuggingFace Inference", "huggingface"},
       }},
      {"Developer / IDE",
       {
           {"GitHub Copilot", "github-copilot"},
           {"OpenCode", "opencode"},
           {"Z.ai", "zai"},
           {"Vercel AI Gateway", "vercel"},
       }},
      {"Local / Self-Hosted",
       {
           {"Ollama (local, no API key)", "ollama"},
           {"vLLM (local)", "vllm"},
       }},
      {"Testing",
       {
           {"Synthetic (mock responses)", "synthetic"},
       }},
      {"Custom",
       {
           {"Custom endpoint (custom:https://...)", "custom"},
       }},
  };
  return groups;
}

const std::unordered_map<std::string, std::vector<ModelSuggestion>> &model_suggestions() {
  static const std::unordered_map<std::string, std::vector<ModelSuggestion>> catalog = {
      {"openrouter",
       {{"openai/gpt-4o", "Recommended"},
        {"openai/gpt-4o-mini", "faster, cheaper"},
        {"anthropic/claude-sonnet-4-5-20250929", "reasoning"},
        {"google/gemini-2.0-flash-exp", "multimodal"},
        {"meta-llama/llama-3.1-70b-instruct", "open-source"}}},
      {"openai",
       {{"gpt-4o", "Recommended"},
        {"gpt-4o-mini", "faster, cheaper"},
        {"o1", "reasoning"},
        {"o1-mini", "compact reasoning"}}},
      {"openai-codex",
       {{"gpt-4o", "Recommended"},
        {"gpt-4o-mini", "faster, cheaper"},
        {"o1-mini", "reasoning"}}},
      {"anthropic",
       {{"claude-sonnet-4-5-20250929", "Recommended"},
        {"claude-opus-4-6", "most capable"},
        {"claude-3-haiku-20240307", "fast & cheap"}}},
      {"google",
       {{"gemini-2.0-flash-exp", "Recommended"},
        {"gemini-1.5-pro", "long context"},
        {"gemini-1.5-flash", "fast"}}},
      {"google-vertex",
       {{"gemini-2.0-flash-exp", "Recommended"},
        {"gemini-1.5-pro", "long context"}}},
      {"grok",
       {{"grok-2-latest", "Recommended"},
        {"grok-2-mini", "smaller"}}},
      {"groq",
       {{"llama-3.1-70b-versatile", "Recommended"},
        {"llama-3.1-8b-instant", "fastest"},
        {"mixtral-8x7b-32768", "balanced"}}},
      {"cerebras",
       {{"llama3.1-70b", "Recommended"},
        {"llama3.1-8b", "fastest"}}},
      {"mistral",
       {{"mistral-large-latest", "Recommended"},
        {"mistral-medium-latest", "balanced"},
        {"mistral-small-latest", "fast"}}},
      {"deepseek",
       {{"deepseek-chat", "Recommended"},
        {"deepseek-coder", "coding"}}},
      {"perplexity",
       {{"llama-3.1-sonar-large-128k-online", "Recommended"},
        {"llama-3.1-sonar-small-128k-online", "compact"}}},
      {"cohere",
       {{"command-r-plus", "Recommended"},
        {"command-r", "compact"}}},
      {"fireworks",
       {{"accounts/fireworks/models/llama-v3p1-70b-instruct", "Recommended"},
        {"accounts/fireworks/models/mixtral-8x7b-instruct", "balanced"}}},
      {"together",
       {{"meta-llama/Meta-Llama-3.1-70B-Instruct-Turbo", "Recommended"},
        {"mistralai/Mixtral-8x7B-Instruct-v0.1", "balanced"}}},
      {"nvidia",
       {{"meta/llama-3.1-70b-instruct", "Recommended"},
        {"meta/llama-3.1-8b-instruct", "fast"}}},
      {"moonshot",
       {{"moonshot-v1-128k", "Recommended"},
        {"moonshot-v1-32k", "lighter"}}},
      {"qwen-portal",
       {{"qwen-max", "Recommended"},
        {"qwen-plus", "balanced"},
        {"qwen-turbo", "fast"}}},
      {"minimax",
       {{"abab6.5s-chat", "Recommended"},
        {"abab5.5-chat", "lighter"}}},
      {"glm",
       {{"glm-4", "Recommended"},
        {"glm-3-turbo", "fast"}}},
      {"ollama",
       {{"llama3.1:8b", "default"},
        {"codellama:13b", "coding"},
        {"mistral:7b", "light"}}},
      {"vllm",
       {{"meta-llama/Llama-3.1-8B-Instruct", "default"}}},
      {"litellm",
       {{"gpt-4o", "proxy default"}}},
      {"huggingface",
       {{"meta-llama/Meta-Llama-3.1-70B-Instruct", "Recommended"}}},
      {"cloudflare",
       {{"@cf/meta/llama-3.1-8b-instruct", "Recommended"}}},
  };
  return catalog;
}

const std::vector<MenuGroup> &memory_groups() {
  static const std::vector<MenuGroup> groups = {
      {"Memory backend",
       {
           {"SQLite (recommended, full-featured)", "sqlite"},
           {"Markdown (simple file-based)", "markdown"},
       }},
  };
  return groups;
}

// ── Bundled skills catalog ────────────────────────────────────────────────────

struct SkillInfo {
  std::string name;
  std::string description;
};

struct SkillCategory {
  std::string heading;
  std::vector<SkillInfo> skills;
};

const std::vector<SkillCategory> &bundled_skill_categories() {
  static const std::vector<SkillCategory> cats = {
      {"Development",
       {{"coding-agent", "Autonomous coding assistant"},
        {"github", "GitHub integration (PRs, issues)"},
        {"skill-creator", "Create new skills"}}},
      {"Productivity",
       {{"canvas", "Visual canvas / whiteboard"},
        {"summarize", "Summarize text and documents"},
        {"session-logs", "Session logging & replay"},
        {"tmux", "Terminal multiplexer control"}}},
      {"Integrations",
       {{"slack", "Slack messaging"},
        {"discord", "Discord bot integration"},
        {"notion", "Notion workspace"},
        {"obsidian", "Obsidian vault"}}},
      {"Apple Ecosystem",
       {{"apple-notes", "Apple Notes read/write"},
        {"apple-reminders", "Apple Reminders"}}},
      {"Monitoring",
       {{"healthcheck", "System health checks"},
        {"model-usage", "Track model usage & costs"},
        {"weather", "Weather forecasts"}}},
  };
  return cats;
}

// ── Env-var helpers ───────────────────────────────────────────────────────────

std::string detect_api_key(const std::string &provider) {
  static const std::vector<std::pair<std::string, std::string>> mappings = {
      {"openai", "OPENAI_API_KEY"},
      {"openai-codex", "OPENAI_API_KEY"},
      {"openrouter", "OPENROUTER_API_KEY"},
      {"anthropic", "ANTHROPIC_API_KEY"},
      {"google", "GOOGLE_API_KEY"},
      {"google-vertex", "GOOGLE_API_KEY"},
      {"groq", "GROQ_API_KEY"},
      {"mistral", "MISTRAL_API_KEY"},
      {"deepseek", "DEEPSEEK_API_KEY"},
      {"cohere", "COHERE_API_KEY"},
      {"grok", "XAI_API_KEY"},
      {"perplexity", "PERPLEXITY_API_KEY"},
      {"fireworks", "FIREWORKS_API_KEY"},
      {"together", "TOGETHER_API_KEY"},
      {"nvidia", "NVIDIA_API_KEY"},
      {"cerebras", "CEREBRAS_API_KEY"},
      {"huggingface", "HUGGINGFACE_TOKEN"},
      {"cloudflare", "CLOUDFLARE_API_TOKEN"},
      {"venice", "VENICE_API_KEY"},
      {"moonshot", "MOONSHOT_API_KEY"},
      {"qwen-portal", "DASHSCOPE_API_KEY"},
      {"minimax", "MINIMAX_API_KEY"},
      {"glm", "ZHIPU_API_KEY"},
      {"qianfan", "QIANFAN_API_KEY"},
      {"github-copilot", "GITHUB_TOKEN"},
  };

  for (const auto &[prov, env_var] : mappings) {
    if (prov == provider) {
      const char *v = std::getenv(env_var.c_str());
      if (v != nullptr && v[0] != '\0') {
        return v;
      }
      break;
    }
  }
  // Generic fallback
  const char *generic = std::getenv("GHOSTCLAW_API_KEY");
  if (generic != nullptr && generic[0] != '\0') {
    return generic;
  }
  return "";
}

// ── Workspace bootstrap ───────────────────────────────────────────────────────

common::Status ensure_workspace_files(const std::filesystem::path &workspace) {
  std::error_code ec;
  std::filesystem::create_directories(workspace / "memory", ec);
  if (ec) {
    return common::Status::error("failed to create workspace: " + ec.message());
  }
  return identity::templates::create_default_identity_files(workspace);
}

void copy_bundled_skills(const std::filesystem::path &workspace,
                         const std::optional<std::vector<std::string>> &filter) {
  const auto ws_skills = workspace / "skills";

  std::vector<std::filesystem::path> candidates;
  const char *exe_dir_env = std::getenv("GHOSTCLAW_ROOT");
  if (exe_dir_env != nullptr) {
    candidates.emplace_back(std::filesystem::path(exe_dir_env) / "skills");
  }
  candidates.emplace_back(std::filesystem::current_path() / "skills");

  for (const auto &src : candidates) {
    std::error_code ec;
    if (!std::filesystem::is_directory(src, ec)) {
      continue;
    }
    for (const auto &entry : std::filesystem::directory_iterator(src, ec)) {
      if (!entry.is_directory()) {
        continue;
      }
      const auto skill_name = entry.path().filename().string();
      // If filter is set, only copy matching skill names
      if (filter.has_value()) {
        bool found = false;
        for (const auto &allowed : *filter) {
          if (allowed == skill_name) {
            found = true;
            break;
          }
        }
        if (!found) {
          continue;
        }
      }
      const auto dest = ws_skills / entry.path().filename();
      if (std::filesystem::exists(dest, ec)) {
        continue; // don't overwrite existing
      }
      std::filesystem::create_directories(dest, ec);
      std::filesystem::copy(entry.path(), dest,
                            std::filesystem::copy_options::recursive |
                                std::filesystem::copy_options::skip_existing,
                            ec);
    }
    break; // used the first valid candidate
  }
}

} // namespace

// ── Public helpers ────────────────────────────────────────────────────────────

std::string prompt_value(const std::string &label, const std::string &fallback) {
  if (use_color()) {
    if (fallback.empty()) {
      std::cout << "  " << BOLD << label << RST << ": ";
    } else {
      std::cout << "  " << BOLD << label << RST << " " << DIM << "[" << fallback << "]" << RST
                << ": ";
    }
  } else {
    if (fallback.empty()) {
      std::cout << label << ": ";
    } else {
      std::cout << label << " [" << fallback << "]: ";
    }
  }
  std::string input;
  if (!std::getline(std::cin, input)) {
    return fallback;
  }
  const std::string trimmed = common::trim(input);
  return trimmed.empty() ? fallback : trimmed;
}

std::string prompt_menu(const std::string &title, const std::vector<MenuGroup> &groups,
                        const std::string &default_value) {
  std::cout << "\n";
  if (use_color()) {
    std::cout << "  " << BOLD << title << RST << "\n";
  } else {
    std::cout << title << "\n";
  }

  std::vector<std::string> values;
  int counter = 1;
  int default_index = 1;

  for (const auto &group : groups) {
    if (use_color()) {
      std::cout << "\n  " << BOLD << CYAN << group.heading << ":" << RST << "\n";
    } else {
      std::cout << "\n  " << group.heading << ":\n";
    }
    for (const auto &entry : group.entries) {
      const bool is_default = (entry.value == default_value);
      if (is_default) {
        default_index = counter;
      }
      if (use_color()) {
        std::cout << "    " << GREEN << counter << RST << ") " << entry.label;
        if (is_default) {
          std::cout << " " << YELLOW << "*" << RST;
        }
        std::cout << "\n";
      } else {
        std::cout << "    " << counter << ") " << entry.label;
        if (is_default) {
          std::cout << " *";
        }
        std::cout << "\n";
      }
      values.push_back(entry.value);
      ++counter;
    }
  }

  if (use_color()) {
    std::cout << "\n  " << BOLD << "Enter number" << RST << " " << DIM << "[" << default_index
              << "]" << RST << ": ";
  } else {
    std::cout << "\nEnter number [" << default_index << "]: ";
  }

  std::string input;
  if (!std::getline(std::cin, input)) {
    return default_value.empty() ? values[0] : default_value;
  }
  const std::string trimmed = common::trim(input);
  if (trimmed.empty()) {
    return default_value.empty() ? values[0] : default_value;
  }
  try {
    const int choice = std::stoi(trimmed);
    if (choice >= 1 && choice <= static_cast<int>(values.size())) {
      return values[static_cast<std::size_t>(choice - 1)];
    }
  } catch (...) {
    // Not a number - maybe they typed a value directly
    return trimmed;
  }
  return default_value.empty() ? values[0] : default_value;
}

bool prompt_yes_no(const std::string &label, bool default_yes) {
  const std::string hint = default_yes ? "Y/n" : "y/N";
  if (use_color()) {
    std::cout << "  " << BOLD << label << RST << " " << DIM << "[" << hint << "]" << RST << ": ";
  } else {
    std::cout << label << " [" << hint << "]: ";
  }
  std::string input;
  if (!std::getline(std::cin, input)) {
    return default_yes;
  }
  const std::string trimmed = common::to_lower(common::trim(input));
  if (trimmed.empty()) {
    return default_yes;
  }
  return trimmed == "y" || trimmed == "yes";
}

// ── Main wizard ───────────────────────────────────────────────────────────────

WizardResult run_wizard(const WizardOptions &options) {
  config::Config config;
  if (config::config_exists()) {
    auto loaded = config::load_config();
    if (!loaded.ok()) {
      return {false, false, loaded.error()};
    }
    config = loaded.value();
  }

  if (options.channels_only) {
    if (options.interactive) {
      const std::string enable_webhook = prompt_value("Enable webhook channel? (yes/no)", "no");
      if (common::to_lower(enable_webhook) == "yes") {
        config::WebhookConfig webhook;
        webhook.secret = prompt_value("Webhook secret", "change-me");
        config.channels.webhook = std::move(webhook);
      }
    }
    auto save = config::save_config(config);
    if (!save.ok()) {
      return {false, false, save.error()};
    }
    return {true, false, ""};
  }

  // Apply any CLI flags
  if (options.api_key.has_value()) {
    config.api_key = *options.api_key;
  }
  if (options.provider.has_value()) {
    config.default_provider = *options.provider;
  }
  if (options.model.has_value()) {
    config.default_model = *options.model;
  }
  if (options.memory_backend.has_value()) {
    config.memory.backend = *options.memory_backend;
  }

  constexpr int total_steps = 7;

  if (options.interactive) {
    print_welcome_banner();

    // ── Step 1/7: Provider ──
    print_step(1, total_steps, "Choose your AI provider");
    config.default_provider =
        prompt_menu("Select your AI provider:", provider_groups(), config.default_provider);

    // ── Step 2/7: Model ──
    print_step(2, total_steps, "Pick a model");
    const auto &catalog = model_suggestions();
    auto it = catalog.find(config.default_provider);
    if (it != catalog.end() && !it->second.empty()) {
      std::vector<MenuGroup> model_groups;
      MenuGroup mg;
      mg.heading = "Models for " + config.default_provider;
      for (const auto &m : it->second) {
        mg.entries.push_back({m.name + " (" + m.note + ")", m.name});
      }
      mg.entries.push_back({"Custom model name", "__custom__"});
      model_groups.push_back(std::move(mg));
      const std::string chosen =
          prompt_menu("Select a model:", model_groups, it->second[0].name);
      if (chosen == "__custom__") {
        config.default_model = prompt_value("Enter model name", config.default_model);
      } else {
        config.default_model = chosen;
      }
    } else {
      config.default_model = prompt_value("Model name", config.default_model);
    }

    // ── Step 3/7: Authentication ──
    print_step(3, total_steps, "Authentication");
    if (!config.api_key.has_value()) {
      if (!provider_needs_key(config.default_provider)) {
        std::cout << "  " << dim("No API key needed for " + config.default_provider + ".") << "\n";
      } else {
        const std::string detected = detect_api_key(config.default_provider);
        if (!detected.empty()) {
          std::cout << "  " << green("Found API key in environment.") << "\n";
          if (prompt_yes_no("Use detected key?", true)) {
            config.api_key = detected;
          } else {
            const std::string manual = prompt_value("API key", "");
            if (!manual.empty()) {
              config.api_key = manual;
            }
          }
        } else {
          // For OpenAI providers, offer ChatGPT login as an alternative
          const bool is_openai = config.default_provider == "openai" ||
                                 config.default_provider == "openai-codex";
          if (is_openai) {
            std::vector<MenuGroup> auth_groups = {
                {"Authentication method",
                 {{"Login with ChatGPT (no API key needed)", "chatgpt"},
                  {"Enter an API key manually", "manual"}}}};
            const std::string auth_choice =
                prompt_menu("How would you like to authenticate?", auth_groups, "chatgpt");

            if (auth_choice == "chatgpt") {
              auto http = std::make_shared<providers::CurlHttpClient>();
              auto login_status = auth::run_device_login(*http);
              if (!login_status.ok()) {
                std::cout << "  " << red("ChatGPT login failed: " + login_status.error()) << "\n";
                std::cout << "  " << dim("Falling back to manual API key entry.") << "\n";
                const std::string api = prompt_value("API key", "");
                if (!api.empty()) {
                  config.api_key = api;
                }
              }
              // On success, no api_key needed -- factory reads auth.json
            } else {
              const std::string api = prompt_value("API key", "");
              if (!api.empty()) {
                config.api_key = api;
              }
            }
          } else {
            const std::string api = prompt_value("API key", "");
            if (!api.empty()) {
              config.api_key = api;
            }
          }
        }
      }
    } else {
      std::cout << "  " << dim("API key provided via CLI flag.") << "\n";
    }

    // ── Step 4/7: Memory backend ──
    print_step(4, total_steps, "Memory backend");
    config.memory.backend =
        prompt_menu("Select memory backend:", memory_groups(), config.memory.backend);

    // ── Step 5/7: Channels (optional) ──
    print_step(5, total_steps, "Channels (optional)");
    std::cout << "  " << dim("Connect messaging channels to interact with GhostClaw remotely.")
              << "\n\n";

    if (prompt_yes_no("Set up Telegram?", false)) {
      config::TelegramConfig tg;
      tg.bot_token = prompt_value("Telegram bot token", "");
      const std::string users = prompt_value("Allowed usernames (comma-separated)", "");
      if (!users.empty()) {
        // Split by comma
        std::string current;
        for (const char c : users) {
          if (c == ',') {
            const auto trimmed = common::trim(current);
            if (!trimmed.empty()) {
              tg.allowed_users.push_back(trimmed);
            }
            current.clear();
          } else {
            current += c;
          }
        }
        const auto trimmed = common::trim(current);
        if (!trimmed.empty()) {
          tg.allowed_users.push_back(trimmed);
        }
      }
      if (!tg.bot_token.empty()) {
        config.channels.telegram = std::move(tg);
      }
    }

    if (prompt_yes_no("Set up Discord?", false)) {
      config::DiscordConfig dc;
      dc.bot_token = prompt_value("Discord bot token", "");
      dc.guild_id = prompt_value("Guild (server) ID", "");
      if (!dc.bot_token.empty()) {
        config.channels.discord = std::move(dc);
      }
    }

    if (prompt_yes_no("Set up Slack?", false)) {
      config::SlackConfig sc;
      sc.bot_token = prompt_value("Slack bot token (xoxb-...)", "");
      sc.channel_id = prompt_value("Channel ID", "");
      if (!sc.bot_token.empty()) {
        config.channels.slack = std::move(sc);
      }
    }

    // ── Step 6/7: Skills ──
    print_step(6, total_steps, "Skills");
    std::cout << "  " << dim("GhostClaw ships with 16 bundled skills:") << "\n\n";

    const auto &skill_cats = bundled_skill_categories();
    for (const auto &cat : skill_cats) {
      if (use_color()) {
        std::cout << "  " << BOLD << CYAN << cat.heading << ":" << RST << "\n";
      } else {
        std::cout << "  " << cat.heading << ":\n";
      }
      for (const auto &skill : cat.skills) {
        if (use_color()) {
          std::cout << "    " << GREEN << skill.name << RST << " - " << DIM << skill.description
                    << RST << "\n";
        } else {
          std::cout << "    " << skill.name << " - " << skill.description << "\n";
        }
      }
    }

    std::vector<MenuGroup> skills_choice_groups = {
        {"Skills setup",
         {{"Enable all 16 skills [Recommended]", "all"},
          {"Choose individually", "choose"},
          {"Skip (install later with 'ghostclaw skills install')", "skip"}}}};
    const std::string skills_choice =
        prompt_menu("How would you like to set up skills?", skills_choice_groups, "all");

    std::optional<std::vector<std::string>> skill_filter;
    if (skills_choice == "choose") {
      std::vector<std::string> selected;
      for (const auto &cat : skill_cats) {
        for (const auto &skill : cat.skills) {
          if (prompt_yes_no("Enable " + skill.name + "?", true)) {
            selected.push_back(skill.name);
          }
        }
      }
      skill_filter = std::move(selected);
    } else if (skills_choice == "skip") {
      skill_filter = std::vector<std::string>{}; // empty = copy nothing
    }
    // skills_choice == "all" leaves skill_filter as nullopt = copy all

    // ── Step 7/7: Review & confirm ──
    print_step(7, total_steps, "Review & confirm");

    if (use_color()) {
      std::cout << "\n";
      std::cout << "  " << BOLD << CYAN
                << "┌──────────────────────────────────────────┐" << RST << "\n";
      std::cout << "  " << BOLD << CYAN << "│" << RST << BOLD
                << "  Configuration Summary                   " << BOLD << CYAN << "│" << RST
                << "\n";
      std::cout << "  " << BOLD << CYAN
                << "├──────────────────────────────────────────┤" << RST << "\n";
      std::cout << "  " << BOLD << CYAN << "│" << RST << "  Provider : " << GREEN
                << config.default_provider << RST;
      // Pad to fill the box
      const int pad1 =
          27 - static_cast<int>(config.default_provider.size());
      for (int i = 0; i < pad1; ++i) {
        std::cout << " ";
      }
      std::cout << BOLD << CYAN << "│" << RST << "\n";

      std::cout << "  " << BOLD << CYAN << "│" << RST << "  Model    : " << GREEN
                << config.default_model << RST;
      const int pad2 = 27 - static_cast<int>(config.default_model.size());
      for (int i = 0; i < pad2; ++i) {
        std::cout << " ";
      }
      std::cout << BOLD << CYAN << "│" << RST << "\n";

      const std::string key_display = config.api_key.has_value() ? "****" : "(none)";
      std::cout << "  " << BOLD << CYAN << "│" << RST << "  API key  : " << key_display;
      const int pad3 = 27 - static_cast<int>(key_display.size());
      for (int i = 0; i < pad3; ++i) {
        std::cout << " ";
      }
      std::cout << BOLD << CYAN << "│" << RST << "\n";

      std::cout << "  " << BOLD << CYAN << "│" << RST << "  Memory   : " << GREEN
                << config.memory.backend << RST;
      const int pad4 = 27 - static_cast<int>(config.memory.backend.size());
      for (int i = 0; i < pad4; ++i) {
        std::cout << " ";
      }
      std::cout << BOLD << CYAN << "│" << RST << "\n";

      // Channels summary
      std::string channels_str;
      if (config.channels.telegram.has_value()) {
        channels_str += "Telegram ";
      }
      if (config.channels.discord.has_value()) {
        channels_str += "Discord ";
      }
      if (config.channels.slack.has_value()) {
        channels_str += "Slack ";
      }
      if (channels_str.empty()) {
        channels_str = "(none)";
      }
      std::cout << "  " << BOLD << CYAN << "│" << RST << "  Channels : " << channels_str;
      const int pad5 = 27 - static_cast<int>(channels_str.size());
      for (int i = 0; i < pad5; ++i) {
        std::cout << " ";
      }
      std::cout << BOLD << CYAN << "│" << RST << "\n";

      // Skills summary
      std::string skills_str;
      if (!skill_filter.has_value()) {
        skills_str = "All 16";
      } else if (skill_filter->empty()) {
        skills_str = "(none)";
      } else {
        skills_str = std::to_string(skill_filter->size()) + " selected";
      }
      std::cout << "  " << BOLD << CYAN << "│" << RST << "  Skills   : " << skills_str;
      const int pad6 = 27 - static_cast<int>(skills_str.size());
      for (int i = 0; i < pad6; ++i) {
        std::cout << " ";
      }
      std::cout << BOLD << CYAN << "│" << RST << "\n";

      std::cout << "  " << BOLD << CYAN
                << "└──────────────────────────────────────────┘" << RST << "\n";
    } else {
      std::cout << "\n";
      std::cout << "  ──────────────────────────────────────\n";
      std::cout << "  Configuration Summary\n";
      std::cout << "  ──────────────────────────────────────\n";
      std::cout << "  Provider : " << config.default_provider << "\n";
      std::cout << "  Model    : " << config.default_model << "\n";
      std::cout << "  API key  : " << (config.api_key.has_value() ? "****" : "(none)") << "\n";
      std::cout << "  Memory   : " << config.memory.backend << "\n";
      std::cout << "  ──────────────────────────────────────\n";
    }

    if (!prompt_yes_no("Save this configuration?", true)) {
      std::cout << "  " << dim("Setup cancelled.") << "\n";
      return {true, false, ""};
    }

    // Save, create workspace, copy skills
    auto validation = config::validate_config(config);
    if (!validation.ok()) {
      return {false, false, validation.error()};
    }

    auto save = config::save_config(config);
    if (!save.ok()) {
      return {false, false, save.error()};
    }

    auto workspace = config::workspace_dir();
    if (!workspace.ok()) {
      return {false, false, workspace.error()};
    }
    auto ws_status = ensure_workspace_files(workspace.value());
    if (!ws_status.ok()) {
      return {false, false, ws_status.error()};
    }

    copy_bundled_skills(workspace.value(), skill_filter);

    std::cout << "\n  " << bold_green("Setup complete!") << "\n";
    auto cfg_path = config::config_path();
    if (cfg_path.ok()) {
      std::cout << "  " << dim("Config: " + cfg_path.value().string()) << "\n";
    }
    std::cout << "  " << dim("Workspace: " + workspace.value().string()) << "\n";

    // Offer launch
    if (options.offer_launch) {
      std::cout << "\n";
      bool do_launch = prompt_yes_no("Launch GhostClaw agent now?", true);
      return {true, do_launch, ""};
    }

    return {true, false, ""};
  }

  // ── Non-interactive mode (unchanged logic, no colors) ──────────────────────
  if (!options.provider.has_value()) {
    config.default_provider =
        config.default_provider.empty() ? "openrouter" : config.default_provider;
  }
  if (!options.model.has_value()) {
    config.default_model =
        config.default_model.empty() ? "gpt-4o-mini" : config.default_model;
  }

  auto validation = config::validate_config(config);
  if (!validation.ok()) {
    return {false, false, validation.error()};
  }

  auto save = config::save_config(config);
  if (!save.ok()) {
    return {false, false, save.error()};
  }

  auto workspace = config::workspace_dir();
  if (!workspace.ok()) {
    return {false, false, workspace.error()};
  }
  auto ws_status = ensure_workspace_files(workspace.value());
  if (!ws_status.ok()) {
    return {false, false, ws_status.error()};
  }

  // Copy all bundled skills in non-interactive mode
  copy_bundled_skills(workspace.value(), std::nullopt);

  std::cout << "\nSetup complete.\n";
  auto cfg_path = config::config_path();
  if (cfg_path.ok()) {
    std::cout << "Config: " << cfg_path.value().string() << "\n";
  }
  std::cout << "Workspace: " << workspace.value().string() << "\n";
  std::cout << "Provider: " << config.default_provider << "\n";
  std::cout << "Model: " << config.default_model << "\n";

  return {true, false, ""};
}

} // namespace ghostclaw::onboard
