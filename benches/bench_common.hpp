#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <string>

namespace ghostclaw::bench {

inline void run_bench(const std::string &name, int iterations,
                      const std::function<void()> &fn) {
  const auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < iterations; ++i) {
    fn();
  }
  const auto end = std::chrono::steady_clock::now();
  const auto total =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  const double avg = static_cast<double>(total) / static_cast<double>(iterations);
  std::cout << name << ": iterations=" << iterations << " total_us=" << total
            << " avg_us=" << avg << "\n";
}

} // namespace ghostclaw::bench
