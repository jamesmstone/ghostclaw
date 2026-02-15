#include "test_framework.hpp"

#include "ghostclaw/tts/tts.hpp"
#include "ghostclaw/voice/wake.hpp"

#include <filesystem>
#include <random>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto dir = std::filesystem::temp_directory_path() /
                   ("ghostclaw-tts-voice-test-" + std::to_string(rng()));
  std::filesystem::create_directories(dir);
  return dir;
}

std::string bytes_to_string(const std::vector<std::uint8_t> &bytes) {
  return {bytes.begin(), bytes.end()};
}

} // namespace

void register_tts_voice_tests(std::vector<ghostclaw::tests::TestCase> &tests) {
  using ghostclaw::tests::require;
  namespace tts = ghostclaw::tts;
  namespace voice = ghostclaw::voice;

  tests.push_back({"tts_engine_register_and_list_providers", [] {
                     tts::TtsEngine engine;

                     auto status = engine.register_provider(
                         std::make_unique<tts::SystemTtsProvider>(tts::SystemTtsConfig{.dry_run = true}));
                     require(status.ok(), status.error());

                     tts::ElevenLabsConfig eleven;
                     eleven.default_voice_id = "voice-test";
                     eleven.dry_run = true;
                     status = engine.register_provider(
                         std::make_unique<tts::ElevenLabsTtsProvider>(std::move(eleven)));
                     require(status.ok(), status.error());

                     auto providers = engine.list_providers();
                     require(providers.size() == 2, "expected two registered providers");
                   }});

  tests.push_back({"tts_system_dry_run_synthesizes", [] {
                     const auto output = make_temp_dir() / "system-dry-run.txt";

                     tts::SystemTtsProvider provider(tts::SystemTtsConfig{.dry_run = true});
                     tts::TtsRequest request;
                     request.text = "hello from system";
                     request.output_path = output;

                     auto audio = provider.synthesize(request);
                     require(audio.ok(), audio.error());
                     require(audio.value().provider == "system", "unexpected provider id");
                     require(std::filesystem::exists(output), "output file should exist");
                     require(!audio.value().bytes.empty(), "dry-run bytes should not be empty");
                     require(bytes_to_string(audio.value().bytes).find("DRYRUN-SYSTEM") != std::string::npos,
                             "expected dry-run marker in output bytes");
                   }});

  tests.push_back({"tts_elevenlabs_dry_run_synthesizes", [] {
                     const auto output = make_temp_dir() / "elevenlabs-dry-run.bin";

                     tts::ElevenLabsConfig config;
                     config.default_voice_id = "voice-test";
                     config.dry_run = true;
                     tts::ElevenLabsTtsProvider provider(config);

                     tts::TtsRequest request;
                     request.text = "hello from elevenlabs";
                     request.output_path = output;

                     auto audio = provider.synthesize(request);
                     require(audio.ok(), audio.error());
                     require(audio.value().provider == "elevenlabs", "unexpected provider id");
                     require(std::filesystem::exists(output), "output file should exist");
                     require(!audio.value().bytes.empty(), "dry-run bytes should not be empty");
                     require(bytes_to_string(audio.value().bytes).find("DRYRUN-ELEVENLABS") !=
                                 std::string::npos,
                             "expected dry-run marker in output bytes");
                   }});

  tests.push_back({"tts_normalize_elevenlabs_base_url", [] {
                     auto normalized =
                         tts::normalize_elevenlabs_base_url("https://api.elevenlabs.io///");
                     require(normalized.ok(), normalized.error());
                     require(normalized.value() == "https://api.elevenlabs.io",
                             "trailing slash should be removed");

                     auto invalid = tts::normalize_elevenlabs_base_url("api.elevenlabs.io");
                     require(!invalid.ok(), "scheme-less URL should be rejected");
                   }});

  tests.push_back({"voice_wake_detector_extracts_command_text", [] {
                     voice::WakeWordDetector detector(
                         voice::WakeWordConfig{.wake_words = {"ghostclaw"}, .case_sensitive = false});

                     const auto match = detector.detect("GhostClaw: run diagnostics now");
                     require(match.detected, "wake word should be detected");
                     require(match.wake_word == "ghostclaw", "wake word should round-trip");
                     require(match.command_text == "run diagnostics now", "command text mismatch");
                   }});

  tests.push_back({"voice_wake_detector_respects_boundaries", [] {
                     voice::WakeWordDetector detector(
                         voice::WakeWordConfig{.wake_words = {"ghost"}, .case_sensitive = false});

                     const auto no_match = detector.detect("ghostwriter please continue");
                     require(!no_match.detected,
                             "wake word should not match as substring inside larger token");

                     const auto yes_match = detector.detect("ghost, continue");
                     require(yes_match.detected, "wake word at token boundary should match");
                   }});

  tests.push_back({"voice_push_to_talk_buffer_roundtrip", [] {
                     voice::PushToTalkBuffer buffer;
                     auto status = buffer.start();
                     require(status.ok(), status.error());
                     require(buffer.feed("hello").ok(), "feed 1 should succeed");
                     require(buffer.feed("world").ok(), "feed 2 should succeed");

                     const auto text = buffer.consume();
                     require(text == "hello world", "push-to-talk transcript mismatch");
                     buffer.stop();
                   }});

  tests.push_back({"voice_controller_push_to_talk_event", [] {
                     voice::VoiceWakeController controller;
                     auto status = controller.push_to_talk().start();
                     require(status.ok(), status.error());

                     auto first = controller.process_transcript("hello", false, true);
                     require(first.type == voice::VoiceInputEventType::None,
                             "intermediate chunk should not emit event");

                     auto second = controller.process_transcript("world", true, true);
                     require(second.type == voice::VoiceInputEventType::PushToTalk,
                             "final chunk should emit push-to-talk event");
                     require(second.text == "hello world", "push-to-talk event text mismatch");
                     controller.push_to_talk().stop();
                   }});

  tests.push_back({"voice_controller_wake_event", [] {
                     voice::VoiceWakeController controller(
                         voice::WakeWordConfig{.wake_words = {"ghostclaw"}, .case_sensitive = false});
                     const auto event = controller.process_transcript("ghostclaw, open config", true, false);
                     require(event.type == voice::VoiceInputEventType::WakeWord,
                             "wake controller should emit wake event");
                     require(event.text == "open config", "wake event command mismatch");
                     require(event.wake_word.has_value(), "wake word should be present");
                   }});
}
