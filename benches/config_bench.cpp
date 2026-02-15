#include "bench_common.hpp"

#include "ghostclaw/config/config.hpp"

void run_config_benchmark() {
  ghostclaw::bench::run_bench("config_validate", 2000, [] {
    ghostclaw::config::Config config;
    (void)ghostclaw::config::validate_config(config);
  });
}
