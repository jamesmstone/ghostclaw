#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ghostclaw::integrations {

struct Integration {
  std::string name;
  std::string category;
  std::string description;
};

class IntegrationRegistry {
public:
  [[nodiscard]] const std::vector<Integration> &all() const;
  [[nodiscard]] std::vector<Integration> by_category(const std::string &category) const;
  [[nodiscard]] std::optional<Integration> find(const std::string &name) const;
};

} // namespace ghostclaw::integrations
