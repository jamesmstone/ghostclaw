#pragma once

#include "ghostclaw/common/result.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace ghostclaw::sandbox {

struct DockerCommandOptions {
  bool allow_failure = false;
  std::chrono::milliseconds timeout{30'000};
};

struct DockerProcessResult {
  int exit_code = 0;
  std::string stdout_text;
  std::string stderr_text;
};

class IDockerRunner {
public:
  virtual ~IDockerRunner() = default;

  [[nodiscard]] virtual common::Result<DockerProcessResult>
  run(const std::vector<std::string> &args, const DockerCommandOptions &options = {}) = 0;
};

class DockerCliRunner final : public IDockerRunner {
public:
  [[nodiscard]] common::Result<DockerProcessResult>
  run(const std::vector<std::string> &args, const DockerCommandOptions &options = {}) override;
};

} // namespace ghostclaw::sandbox
