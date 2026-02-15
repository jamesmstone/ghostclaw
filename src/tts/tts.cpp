#include "ghostclaw/tts/tts.hpp"

#include "ghostclaw/common/fs.hpp"

namespace ghostclaw::tts {

common::Result<std::string> normalize_elevenlabs_base_url(std::string value) {
  value = common::trim(std::move(value));
  if (value.empty()) {
    return common::Result<std::string>::success("https://api.elevenlabs.io");
  }

  if (!common::starts_with(common::to_lower(value), "http://") &&
      !common::starts_with(common::to_lower(value), "https://")) {
    return common::Result<std::string>::failure("ElevenLabs base URL must start with http:// or https://");
  }

  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }

  return common::Result<std::string>::success(std::move(value));
}

common::Status TtsEngine::register_provider(std::unique_ptr<ITtsProvider> provider) {
  if (provider == nullptr) {
    return common::Status::error("provider is required");
  }

  const std::string key(provider->id());
  if (common::trim(key).empty()) {
    return common::Status::error("provider id is empty");
  }
  if (providers_.contains(key)) {
    return common::Status::error("provider already registered: " + key);
  }

  providers_.insert_or_assign(key, std::move(provider));
  if (default_provider_.empty()) {
    default_provider_ = key;
  }
  return common::Status::success();
}

common::Status TtsEngine::set_default_provider(const std::string &provider_id) {
  const std::string key = common::trim(provider_id);
  if (key.empty()) {
    return common::Status::error("provider id is required");
  }
  if (!providers_.contains(key)) {
    return common::Status::error("unknown provider: " + key);
  }

  default_provider_ = key;
  return common::Status::success();
}

common::Result<TtsAudio> TtsEngine::synthesize(const TtsRequest &request,
                                               const std::string &provider_id) {
  if (providers_.empty()) {
    return common::Result<TtsAudio>::failure("no TTS providers are registered");
  }

  std::string selected = common::trim(provider_id);
  if (selected.empty()) {
    selected = default_provider_;
  }
  if (selected.empty()) {
    selected = providers_.begin()->first;
  }

  const auto it = providers_.find(selected);
  if (it == providers_.end()) {
    return common::Result<TtsAudio>::failure("unknown TTS provider: " + selected);
  }

  return it->second->synthesize(request);
}

std::vector<std::string> TtsEngine::list_providers() const {
  std::vector<std::string> out;
  out.reserve(providers_.size());
  for (const auto &[name, provider] : providers_) {
    (void)provider;
    out.push_back(name);
  }
  return out;
}

} // namespace ghostclaw::tts
