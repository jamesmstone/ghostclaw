#pragma once

#include "ghostclaw/common/result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ghostclaw::tools::plugin {

struct LoadedPlugin {
  std::filesystem::path path;
  std::string name;
  void *handle = nullptr;
};

class PluginLoader {
public:
  explicit PluginLoader(std::filesystem::path plugin_dir);
  ~PluginLoader();

  [[nodiscard]] common::Result<std::vector<LoadedPlugin>> load_all();

private:
  std::filesystem::path plugin_dir_;
  std::vector<LoadedPlugin> loaded_;
};

} // namespace ghostclaw::tools::plugin
