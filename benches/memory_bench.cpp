#include "bench_common.hpp"

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/memory/memory.hpp"

#include <filesystem>

void run_memory_benchmark() {
  ghostclaw::config::Config config;
  config.memory.backend = "markdown";
  const auto workspace = std::filesystem::temp_directory_path() / "ghostclaw-memory-bench";
  auto memory = ghostclaw::memory::create_memory(config, workspace);
  if (memory == nullptr) {
    return;
  }

  ghostclaw::bench::run_bench("memory_store", 500, [&] {
    static int i = 0;
    (void)memory->store("bench-" + std::to_string(i++), "benchmark payload",
                        ghostclaw::memory::MemoryCategory::Daily);
  });

  ghostclaw::bench::run_bench("memory_recall", 200,
                              [&] { (void)memory->recall("benchmark", 5); });
}
