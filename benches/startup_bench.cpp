#include "bench_common.hpp"

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/memory/memory.hpp"
#include "ghostclaw/providers/factory.hpp"

#include <filesystem>

void run_startup_benchmark() {
  ghostclaw::bench::run_bench("startup", 50, [] {
    ghostclaw::config::Config config;
    const auto workspace = std::filesystem::temp_directory_path() / "ghostclaw-bench-workspace";
    auto memory = ghostclaw::memory::create_memory(config, workspace);
    auto provider = ghostclaw::providers::create_provider("openai", config.api_key);
    (void)memory;
    (void)provider;
  });
}
