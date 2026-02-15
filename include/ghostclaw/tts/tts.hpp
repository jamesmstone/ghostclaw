#pragma once

#include "ghostclaw/common/result.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ghostclaw::tts {

struct ElevenLabsVoiceSettings {
  double stability = 0.5;
  double similarity_boost = 0.75;
  double style = 0.0;
  bool use_speaker_boost = true;
  double speed = 1.0;
};

struct TtsRequest {
  std::string text;
  std::optional<std::string> voice;
  std::optional<std::string> model;
  std::optional<double> speed;
  std::optional<std::filesystem::path> output_path;
  bool dry_run = false;
};

struct TtsAudio {
  std::string provider;
  std::string mime_type;
  std::vector<std::uint8_t> bytes;
  std::optional<std::filesystem::path> output_path;
};

class ITtsProvider {
public:
  virtual ~ITtsProvider() = default;

  [[nodiscard]] virtual std::string_view id() const = 0;
  [[nodiscard]] virtual common::Result<TtsAudio> synthesize(const TtsRequest &request) = 0;
  [[nodiscard]] virtual bool health_check() = 0;
};

struct ElevenLabsConfig {
  std::string api_key;
  std::string base_url = "https://api.elevenlabs.io";
  std::string default_voice_id;
  std::string default_model_id = "eleven_multilingual_v2";
  ElevenLabsVoiceSettings voice_settings;
  std::uint32_t timeout_ms = 30000;
  bool dry_run = false;
};

class ElevenLabsTtsProvider final : public ITtsProvider {
public:
  explicit ElevenLabsTtsProvider(ElevenLabsConfig config);

  [[nodiscard]] std::string_view id() const override;
  [[nodiscard]] common::Result<TtsAudio> synthesize(const TtsRequest &request) override;
  [[nodiscard]] bool health_check() override;

private:
  ElevenLabsConfig config_;
};

using CommandRunner = std::function<int(const std::string &)>;

struct SystemTtsConfig {
  std::string command;
  std::optional<std::string> default_voice;
  std::optional<std::string> default_rate;
  bool dry_run = false;
  CommandRunner command_runner;
};

class SystemTtsProvider final : public ITtsProvider {
public:
  explicit SystemTtsProvider(SystemTtsConfig config);

  [[nodiscard]] std::string_view id() const override;
  [[nodiscard]] common::Result<TtsAudio> synthesize(const TtsRequest &request) override;
  [[nodiscard]] bool health_check() override;

private:
  [[nodiscard]] std::string build_command(const TtsRequest &request,
                                          std::string *selected_backend = nullptr) const;

  SystemTtsConfig config_;
};

class TtsEngine {
public:
  [[nodiscard]] common::Status register_provider(std::unique_ptr<ITtsProvider> provider);
  [[nodiscard]] common::Status set_default_provider(const std::string &provider_id);
  [[nodiscard]] common::Result<TtsAudio>
  synthesize(const TtsRequest &request, const std::string &provider_id = "");
  [[nodiscard]] std::vector<std::string> list_providers() const;

private:
  std::unordered_map<std::string, std::unique_ptr<ITtsProvider>> providers_;
  std::string default_provider_;
};

[[nodiscard]] common::Result<std::string> normalize_elevenlabs_base_url(std::string value);

} // namespace ghostclaw::tts
