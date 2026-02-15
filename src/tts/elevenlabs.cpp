#include "ghostclaw/tts/tts.hpp"

#include "ghostclaw/common/fs.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace ghostclaw::tts {

namespace {

std::string json_escape(const std::string &value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (const char ch : value) {
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out.push_back(ch);
      break;
    }
  }
  return out;
}

size_t write_bytes_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
  if (userdata == nullptr) {
    return 0;
  }
  auto *buffer = static_cast<std::vector<std::uint8_t> *>(userdata);
  const size_t total = size * nmemb;
  const auto *begin = static_cast<const std::uint8_t *>(ptr);
  buffer->insert(buffer->end(), begin, begin + total);
  return total;
}

common::Status write_bytes_file(const std::filesystem::path &path,
                                const std::vector<std::uint8_t> &bytes) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return common::Status::error("failed to create output directory: " + ec.message());
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return common::Status::error("failed to open output file: " + path.string());
  }

  if (!bytes.empty()) {
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }
  if (!out) {
    return common::Status::error("failed writing output file: " + path.string());
  }
  return common::Status::success();
}

std::string resolve_api_key(const ElevenLabsConfig &config) {
  if (!common::trim(config.api_key).empty()) {
    return common::trim(config.api_key);
  }
  if (const char *env = std::getenv("ELEVENLABS_API_KEY"); env != nullptr && *env != '\0') {
    return std::string(env);
  }
  if (const char *env = std::getenv("XI_API_KEY"); env != nullptr && *env != '\0') {
    return std::string(env);
  }
  return "";
}

bool is_valid_voice_settings(const ElevenLabsVoiceSettings &settings) {
  if (settings.stability < 0.0 || settings.stability > 1.0) {
    return false;
  }
  if (settings.similarity_boost < 0.0 || settings.similarity_boost > 1.0) {
    return false;
  }
  if (settings.style < 0.0 || settings.style > 1.0) {
    return false;
  }
  if (settings.speed < 0.5 || settings.speed > 2.0) {
    return false;
  }
  return true;
}

} // namespace

ElevenLabsTtsProvider::ElevenLabsTtsProvider(ElevenLabsConfig config) : config_(std::move(config)) {
  auto base_url = normalize_elevenlabs_base_url(config_.base_url);
  if (base_url.ok()) {
    config_.base_url = base_url.value();
  }
}

std::string_view ElevenLabsTtsProvider::id() const { return "elevenlabs"; }

bool ElevenLabsTtsProvider::health_check() {
  return !resolve_api_key(config_).empty() || config_.dry_run;
}

common::Result<TtsAudio> ElevenLabsTtsProvider::synthesize(const TtsRequest &request) {
  const std::string text = common::trim(request.text);
  if (text.empty()) {
    return common::Result<TtsAudio>::failure("TTS text is empty");
  }

  const std::string voice_id =
      common::trim(request.voice.value_or(config_.default_voice_id));
  if (voice_id.empty()) {
    return common::Result<TtsAudio>::failure("ElevenLabs voice ID is required");
  }

  const std::string model_id = common::trim(
      request.model.value_or(common::trim(config_.default_model_id).empty()
                                 ? "eleven_multilingual_v2"
                                 : config_.default_model_id));

  ElevenLabsVoiceSettings voice_settings = config_.voice_settings;
  if (request.speed.has_value()) {
    voice_settings.speed = *request.speed;
  }
  if (!is_valid_voice_settings(voice_settings)) {
    return common::Result<TtsAudio>::failure("invalid ElevenLabs voice settings range");
  }

  TtsAudio audio;
  audio.provider = std::string(id());
  audio.mime_type = "audio/mpeg";

  const bool dry_run = config_.dry_run || request.dry_run;
  if (dry_run) {
    const std::string simulated = "DRYRUN-ELEVENLABS:" + text;
    audio.bytes.assign(simulated.begin(), simulated.end());
    if (request.output_path.has_value()) {
      auto status = write_bytes_file(*request.output_path, audio.bytes);
      if (!status.ok()) {
        return common::Result<TtsAudio>::failure(status.error());
      }
      audio.output_path = request.output_path;
    }
    return common::Result<TtsAudio>::success(std::move(audio));
  }

  const std::string api_key = resolve_api_key(config_);
  if (api_key.empty()) {
    return common::Result<TtsAudio>::failure(
        "ELEVENLABS_API_KEY (or XI_API_KEY) is required for ElevenLabs TTS");
  }

  CURL *curl = curl_easy_init();
  if (curl == nullptr) {
    return common::Result<TtsAudio>::failure("failed to initialize CURL for ElevenLabs request");
  }

  std::vector<std::uint8_t> response_bytes;
  response_bytes.reserve(4096);

  std::ostringstream body;
  body << "{";
  body << "\"text\":\"" << json_escape(text) << "\",";
  body << "\"model_id\":\"" << json_escape(model_id) << "\",";
  body << "\"voice_settings\":{";
  body << "\"stability\":" << voice_settings.stability << ",";
  body << "\"similarity_boost\":" << voice_settings.similarity_boost << ",";
  body << "\"style\":" << voice_settings.style << ",";
  body << "\"use_speaker_boost\":" << (voice_settings.use_speaker_boost ? "true" : "false")
       << ",";
  body << "\"speed\":" << voice_settings.speed;
  body << "}";
  body << "}";
  const std::string body_text = body.str();

  const std::string url = config_.base_url + "/v1/text-to-speech/" + voice_id;
  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: audio/mpeg");
  const std::string key_header = "xi-api-key: " + api_key;
  headers = curl_slist_append(headers, key_header.c_str());

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_text.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                   static_cast<curl_off_t>(body_text.size()));
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(config_.timeout_ms));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_bytes_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_bytes);

  const CURLcode code = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (code != CURLE_OK) {
    return common::Result<TtsAudio>::failure(std::string("ElevenLabs request failed: ") +
                                             curl_easy_strerror(code));
  }
  if (http_code < 200 || http_code >= 300) {
    return common::Result<TtsAudio>::failure("ElevenLabs request failed with HTTP status " +
                                             std::to_string(http_code));
  }

  audio.bytes = std::move(response_bytes);
  if (request.output_path.has_value()) {
    auto status = write_bytes_file(*request.output_path, audio.bytes);
    if (!status.ok()) {
      return common::Result<TtsAudio>::failure(status.error());
    }
    audio.output_path = request.output_path;
  }

  return common::Result<TtsAudio>::success(std::move(audio));
}

} // namespace ghostclaw::tts
