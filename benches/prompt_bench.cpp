#include "bench_common.hpp"

#include "ghostclaw/agent/context.hpp"

#include <filesystem>

void run_prompt_benchmark() {
  const auto workspace = std::filesystem::temp_directory_path() / "ghostclaw-prompt-bench";
  std::error_code ec;
  std::filesystem::create_directories(workspace, ec);

  ghostclaw::config::IdentityConfig identity;
  identity.format = "openclaw";
  ghostclaw::agent::ContextBuilder builder(workspace, identity);

  ghostclaw::bench::run_bench("prompt_build", 200,
                              [&] { (void)builder.build_system_prompt({}, {}); });
}
