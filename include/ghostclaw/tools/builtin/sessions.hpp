#pragma once

#include "ghostclaw/sessions/store.hpp"
#include "ghostclaw/tools/tool.hpp"

#include <memory>

namespace ghostclaw::tools {

class SessionsListTool final : public ITool {
public:
  explicit SessionsListTool(std::shared_ptr<sessions::SessionStore> store = nullptr);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  std::shared_ptr<sessions::SessionStore> store_;
};

class SessionsHistoryTool final : public ITool {
public:
  explicit SessionsHistoryTool(std::shared_ptr<sessions::SessionStore> store = nullptr);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  std::shared_ptr<sessions::SessionStore> store_;
};

class SessionsSendTool final : public ITool {
public:
  explicit SessionsSendTool(std::shared_ptr<sessions::SessionStore> store = nullptr);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  std::shared_ptr<sessions::SessionStore> store_;
};

class SessionsSpawnTool final : public ITool {
public:
  explicit SessionsSpawnTool(std::shared_ptr<sessions::SessionStore> store = nullptr);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  std::shared_ptr<sessions::SessionStore> store_;
};

class SubagentsTool final : public ITool {
public:
  explicit SubagentsTool(std::shared_ptr<sessions::SessionStore> store = nullptr);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] std::string_view description() const override;
  [[nodiscard]] std::string parameters_schema() const override;
  [[nodiscard]] common::Result<ToolResult> execute(const ToolArgs &args,
                                                   const ToolContext &ctx) override;

  [[nodiscard]] bool is_safe() const override;
  [[nodiscard]] std::string_view group() const override;

private:
  std::shared_ptr<sessions::SessionStore> store_;
};

} // namespace ghostclaw::tools
