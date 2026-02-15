#pragma once

#include "ghostclaw/config/schema.hpp"
#include "ghostclaw/providers/traits.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace ghostclaw::testing {

config::Config mock_config();

class MockProvider final : public providers::Provider {
public:
  void set_response(std::string response);
  void set_error(std::string error_message);

  [[nodiscard]] common::Result<std::string>
  chat(const std::string &message, const std::string &model, double temperature) override;

  [[nodiscard]] common::Result<std::string>
  chat_with_system(const std::optional<std::string> &system_prompt, const std::string &message,
                   const std::string &model, double temperature) override;

  [[nodiscard]] common::Status warmup() override;
  [[nodiscard]] std::string name() const override { return "mock"; }

private:
  std::optional<std::string> response_;
  std::optional<std::string> error_;
};

class TempWorkspace {
public:
  TempWorkspace();
  ~TempWorkspace();

  TempWorkspace(const TempWorkspace &) = delete;
  TempWorkspace &operator=(const TempWorkspace &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const { return path_; }
  void create_file(const std::string &name, const std::string &content) const;

private:
  std::filesystem::path path_;
};

config::Config temp_config(const TempWorkspace &workspace);

} // namespace ghostclaw::testing
