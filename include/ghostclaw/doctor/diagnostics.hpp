#pragma once

#include "ghostclaw/config/schema.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ghostclaw::doctor {

enum class CheckStatus {
  Pass,
  Fail,
  Warn,
};

struct DiagnosticCheck {
  std::string name;
  CheckStatus status = CheckStatus::Pass;
  std::string message;
  std::optional<std::chrono::milliseconds> latency;
};

struct DiagnosticsReport {
  std::vector<DiagnosticCheck> checks;
  int passed = 0;
  int failed = 0;
  int warnings = 0;
};

[[nodiscard]] DiagnosticsReport run_diagnostics(const config::Config &config);
void print_diagnostics_report(const DiagnosticsReport &report);

} // namespace ghostclaw::doctor
