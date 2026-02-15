#pragma once

#include "ghostclaw/common/result.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghostclaw::voice {

struct WakeWordConfig {
  std::vector<std::string> wake_words = {"ghostclaw"};
  bool case_sensitive = false;
};

struct WakeMatch {
  bool detected = false;
  std::string wake_word;
  std::string original_text;
  std::string command_text;
  std::size_t position = std::string::npos;
};

class WakeWordDetector {
public:
  explicit WakeWordDetector(WakeWordConfig config = {});

  void set_config(WakeWordConfig config);
  [[nodiscard]] const WakeWordConfig &config() const;
  [[nodiscard]] WakeMatch detect(std::string_view text) const;

private:
  [[nodiscard]] static std::string normalize_for_match(std::string value,
                                                       bool case_sensitive);

  WakeWordConfig config_;
};

class PushToTalkBuffer {
public:
  [[nodiscard]] common::Status start();
  void stop();
  [[nodiscard]] bool active() const;

  [[nodiscard]] common::Status feed(std::string_view chunk);
  [[nodiscard]] std::string consume();
  void clear();

private:
  mutable std::mutex mutex_;
  bool active_ = false;
  std::vector<std::string> chunks_;
};

enum class VoiceInputEventType {
  None,
  WakeWord,
  PushToTalk,
};

struct VoiceInputEvent {
  VoiceInputEventType type = VoiceInputEventType::None;
  std::string text;
  std::optional<std::string> wake_word;
};

class VoiceWakeController {
public:
  explicit VoiceWakeController(WakeWordConfig config = {});

  [[nodiscard]] WakeWordDetector &detector();
  [[nodiscard]] const WakeWordDetector &detector() const;
  [[nodiscard]] PushToTalkBuffer &push_to_talk();
  [[nodiscard]] const PushToTalkBuffer &push_to_talk() const;

  [[nodiscard]] VoiceInputEvent process_transcript(std::string_view text,
                                                   bool final_chunk,
                                                   bool use_push_to_talk);

private:
  WakeWordDetector detector_;
  PushToTalkBuffer push_to_talk_;
};

} // namespace ghostclaw::voice
