#include "bench_common.hpp"

#include "ghostclaw/channels/allowlist.hpp"
#include "ghostclaw/heartbeat/cron.hpp"
#include "ghostclaw/heartbeat/cron_store.hpp"
#include "ghostclaw/security/external_content.hpp"
#include "ghostclaw/security/secrets.hpp"
#include "ghostclaw/sessions/session_key.hpp"
#include "ghostclaw/sessions/store.hpp"

#include <filesystem>
#include <random>
#include <thread>
#include <vector>

namespace {

std::filesystem::path make_temp_dir() {
  static std::mt19937_64 rng{std::random_device{}()};
  const auto base = std::filesystem::temp_directory_path() /
                    ("ghostclaw-perf-bench-" + std::to_string(rng()));
  std::filesystem::create_directories(base);
  return base;
}

} // namespace

void run_concurrency_benchmark() {
  std::cout << "\n=== Concurrency Benchmarks ===\n";

  // Session store concurrent access
  {
    const auto dir = make_temp_dir();
    ghostclaw::sessions::SessionStore store(dir);

    ghostclaw::bench::run_bench("session_concurrent_writes", 100, [&] {
      std::vector<std::thread> threads;
      for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&store, i]() {
          for (int j = 0; j < 25; ++j) {
            auto key = ghostclaw::sessions::make_session_key({
                .agent_id = "agent",
                .channel_id = "ch",
                .peer_id = "user" + std::to_string(i * 100 + j)
            });
            if (!key.ok()) continue;

            ghostclaw::sessions::SessionState state;
            state.session_id = key.value();
            state.model = "test";
            (void)store.upsert_state(state);
          }
        });
      }
      for (auto &t : threads) {
        t.join();
      }
    });
  }

  // Cron store concurrent access
  {
    const auto dir = make_temp_dir();
    ghostclaw::heartbeat::CronStore store(dir / "jobs.db");

    ghostclaw::bench::run_bench("cron_concurrent_operations", 50, [&] {
      std::vector<std::thread> threads;
      for (int i = 0; i < 4; ++i) {
        threads.emplace_back([&store, i]() {
          for (int j = 0; j < 10; ++j) {
            ghostclaw::heartbeat::CronJob job;
            job.id = "job-" + std::to_string(i * 100 + j);
            job.expression = "* * * * *";
            job.command = "test";
            job.next_run = std::chrono::system_clock::now();
            (void)store.add_job(job);
          }
        });
      }
      for (auto &t : threads) {
        t.join();
      }
    });
  }
}

void run_latency_benchmark() {
  std::cout << "\n=== Latency Benchmarks ===\n";

  // Session key creation
  ghostclaw::bench::run_bench("session_key_creation", 10000, [] {
    auto key = ghostclaw::sessions::make_session_key({
        .agent_id = "ghostclaw",
        .channel_id = "telegram",
        .peer_id = "user123"
    });
    (void)key;
  });

  // Cron expression parsing
  ghostclaw::bench::run_bench("cron_expression_parse", 10000, [] {
    auto expr = ghostclaw::heartbeat::CronExpression::parse("*/5 * * * *");
    (void)expr;
  });

  // Allowlist check
  {
    std::vector<std::string> allowlist = {"alice", "bob", "charlie", "david", "eve"};
    ghostclaw::bench::run_bench("allowlist_check", 10000, [&] {
      (void)ghostclaw::channels::check_allowlist("charlie", allowlist);
    });
  }

  // External content wrapping
  ghostclaw::bench::run_bench("external_content_wrap", 5000, [] {
    (void)ghostclaw::security::wrap_external_content(
        "Test content with some text",
        ghostclaw::security::ExternalSource::Webhook);
  });

  // Suspicious pattern detection
  ghostclaw::bench::run_bench("suspicious_pattern_detect", 5000, [] {
    (void)ghostclaw::security::detect_suspicious_patterns(
        "This is a normal message without any suspicious content");
  });

  // Homoglyph normalization
  ghostclaw::bench::run_bench("homoglyph_normalize", 5000, [] {
    (void)ghostclaw::security::normalize_homoglyphs(
        "Normal ASCII text with some unicode: café résumé");
  });
}

void run_crypto_benchmark() {
  std::cout << "\n=== Crypto Benchmarks ===\n";

  auto key = ghostclaw::security::generate_key();

  ghostclaw::bench::run_bench("key_generation", 1000, [] {
    auto k = ghostclaw::security::generate_key();
    (void)k;
  });

  ghostclaw::bench::run_bench("encrypt_short", 5000, [&] {
    auto encrypted = ghostclaw::security::encrypt_secret(key, "short secret");
    (void)encrypted;
  });

  std::string long_secret(1000, 'x');
  ghostclaw::bench::run_bench("encrypt_long", 2000, [&] {
    auto encrypted = ghostclaw::security::encrypt_secret(key, long_secret);
    (void)encrypted;
  });

  auto encrypted = ghostclaw::security::encrypt_secret(key, "test secret");
  if (encrypted.ok()) {
    ghostclaw::bench::run_bench("decrypt", 5000, [&] {
      auto decrypted = ghostclaw::security::decrypt_secret(key, encrypted.value());
      (void)decrypted;
    });
  }
}

void run_session_benchmark() {
  std::cout << "\n=== Session Benchmarks ===\n";

  const auto dir = make_temp_dir();
  ghostclaw::sessions::SessionStore store(dir);

  // Create sessions
  ghostclaw::bench::run_bench("session_create", 500, [&] {
    static int i = 0;
    auto key = ghostclaw::sessions::make_session_key({
        .agent_id = "agent",
        .channel_id = "ch",
        .peer_id = "user" + std::to_string(i++)
    });
    if (!key.ok()) return;

    ghostclaw::sessions::SessionState state;
    state.session_id = key.value();
    state.model = "test";
    (void)store.upsert_state(state);
  });

  // Append transcripts
  auto key = ghostclaw::sessions::make_session_key({
      .agent_id = "agent",
      .channel_id = "ch",
      .peer_id = "transcript-user"
  });
  if (key.ok()) {
    ghostclaw::sessions::SessionState state;
    state.session_id = key.value();
    (void)store.upsert_state(state);

    ghostclaw::bench::run_bench("transcript_append", 1000, [&] {
      ghostclaw::sessions::TranscriptEntry entry;
      entry.role = ghostclaw::sessions::TranscriptRole::User;
      entry.content = "Test message content";
      entry.model = "test";
      (void)store.append_transcript(key.value(), entry);
    });

    ghostclaw::bench::run_bench("transcript_load", 500, [&] {
      (void)store.load_transcript(key.value(), 100);
    });
  }
}

void run_performance_benchmarks() {
  std::cout << "\n========================================\n";
  std::cout << "GhostClaw Performance Benchmarks\n";
  std::cout << "========================================\n";

  run_latency_benchmark();
  run_crypto_benchmark();
  run_session_benchmark();
  run_concurrency_benchmark();

  std::cout << "\n========================================\n";
  std::cout << "Performance benchmarks complete\n";
  std::cout << "========================================\n";
}
