add_executable(ghostclaw_benchmarks
  startup_bench.cpp
  memory_bench.cpp
  prompt_bench.cpp
  config_bench.cpp
  performance_bench.cpp
  bench_main.cpp
)

target_link_libraries(ghostclaw_benchmarks PRIVATE ghostclaw_lib)
