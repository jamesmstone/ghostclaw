#pragma once

#include "ghostclaw/common/result.hpp"

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <vector>

namespace ghostclaw::canvas {

class CanvasHost {
public:
  [[nodiscard]] common::Result<void> push(const std::string &html);
  [[nodiscard]] common::Result<void> eval(const std::string &js);
  [[nodiscard]] common::Result<std::string> snapshot();
  [[nodiscard]] common::Result<void> reset();

private:
  [[nodiscard]] common::Result<void> apply_eval_locked(const std::string &js);
  void append_script_locked(const std::string &js);
  void touch_locked();

  mutable std::shared_mutex mutex_;
  std::string html_;
  std::vector<std::string> recent_scripts_;
  std::uint64_t script_count_ = 0;
  std::uint64_t version_ = 0;
  std::uint64_t updated_at_ms_ = 0;
};

} // namespace ghostclaw::canvas
