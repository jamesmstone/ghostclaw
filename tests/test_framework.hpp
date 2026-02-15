#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ghostclaw::tests {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace ghostclaw::tests
