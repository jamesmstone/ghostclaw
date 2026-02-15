#include "tests/helpers/test_helpers.hpp"

#include <fstream>
#include <random>

namespace ghostclaw::testing {

config::Config mock_config() {
  config::Config config;
  config.default_provider = "openai";
  config.default_model = "gpt-4o-mini";
  config.api_key = "test-key";
  config.memory.backend = "sqlite";
  config.gateway.require_pairing = false;
  config.observability.backend = "none";
  return config;
}

void MockProvider::set_response(std::string response) {
  response_ = std::move(response);
  error_.reset();
}

void MockProvider::set_error(std::string error_message) {
  error_ = std::move(error_message);
  response_.reset();
}

common::Result<std::string> MockProvider::chat(const std::string &, const std::string &, double) {
  if (error_.has_value()) {
    return common::Result<std::string>::failure(*error_);
  }
  return common::Result<std::string>::success(response_.value_or("mock-response"));
}

common::Result<std::string>
MockProvider::chat_with_system(const std::optional<std::string> &, const std::string &,
                               const std::string &, double) {
  if (error_.has_value()) {
    return common::Result<std::string>::failure(*error_);
  }
  return common::Result<std::string>::success(response_.value_or("mock-response"));
}

common::Status MockProvider::warmup() {
  if (error_.has_value()) {
    return common::Status::error(*error_);
  }
  return common::Status::success();
}

TempWorkspace::TempWorkspace() {
  static std::mt19937_64 rng{std::random_device{}()};
  path_ = std::filesystem::temp_directory_path() / ("ghostclaw-test-workspace-" + std::to_string(rng()));
  std::filesystem::create_directories(path_);
}

TempWorkspace::~TempWorkspace() {
  std::error_code ec;
  std::filesystem::remove_all(path_, ec);
}

void TempWorkspace::create_file(const std::string &name, const std::string &content) const {
  const auto file_path = path_ / name;
  std::error_code ec;
  std::filesystem::create_directories(file_path.parent_path(), ec);
  std::ofstream out(file_path, std::ios::trunc);
  out << content;
}

config::Config temp_config(const TempWorkspace &workspace) {
  auto config = mock_config();
  config.autonomy.workspace_only = true;
  config.identity.format = "openclaw";

  std::error_code ec;
  std::filesystem::create_directories(workspace.path() / "memory", ec);

  return config;
}

} // namespace ghostclaw::testing
