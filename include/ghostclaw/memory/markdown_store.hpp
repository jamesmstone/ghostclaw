#pragma once

#include "ghostclaw/memory/memory.hpp"

namespace ghostclaw::memory {

class MarkdownMemory final : public IMemory {
public:
  explicit MarkdownMemory(std::filesystem::path workspace);

  [[nodiscard]] std::string_view name() const override;
  [[nodiscard]] common::Status store(const std::string &key, const std::string &content,
                                     MemoryCategory category) override;
  [[nodiscard]] common::Result<std::vector<MemoryEntry>>
  recall(const std::string &query, std::size_t limit) override;
  [[nodiscard]] common::Result<std::optional<MemoryEntry>> get(const std::string &key) override;
  [[nodiscard]] common::Result<std::vector<MemoryEntry>>
  list(std::optional<MemoryCategory> category) override;
  [[nodiscard]] common::Result<bool> forget(const std::string &key) override;
  [[nodiscard]] common::Result<std::size_t> count() override;
  [[nodiscard]] common::Status reindex() override;
  [[nodiscard]] bool health_check() override;
  [[nodiscard]] MemoryStats stats() override;

private:
  [[nodiscard]] std::filesystem::path path_for_category(MemoryCategory category) const;
  [[nodiscard]] common::Status append_entry(const std::filesystem::path &path,
                                            const MemoryEntry &entry) const;
  [[nodiscard]] common::Result<std::vector<MemoryEntry>> load_all() const;

  std::filesystem::path workspace_;
};

} // namespace ghostclaw::memory
