#include <iostream>

void run_startup_benchmark();
void run_memory_benchmark();
void run_prompt_benchmark();
void run_config_benchmark();
void run_performance_benchmarks();

int main() {
  std::cout << "GhostClaw Benchmarks\n";
  run_startup_benchmark();
  run_memory_benchmark();
  run_prompt_benchmark();
  run_config_benchmark();
  run_performance_benchmarks();
  return 0;
}
