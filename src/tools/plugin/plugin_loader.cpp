#include "ghostclaw/tools/plugin/plugin_loader.hpp"

#include <dlfcn.h>

namespace ghostclaw::tools::plugin {

PluginLoader::PluginLoader(std::filesystem::path plugin_dir) : plugin_dir_(std::move(plugin_dir)) {}

PluginLoader::~PluginLoader() {
  for (auto &plugin : loaded_) {
    if (plugin.handle != nullptr) {
      dlclose(plugin.handle);
      plugin.handle = nullptr;
    }
  }
}

common::Result<std::vector<LoadedPlugin>> PluginLoader::load_all() {
  loaded_.clear();

  if (!std::filesystem::exists(plugin_dir_)) {
    return common::Result<std::vector<LoadedPlugin>>::success({});
  }

  for (const auto &entry : std::filesystem::directory_iterator(plugin_dir_)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const auto ext = entry.path().extension().string();
    if (ext != ".so" && ext != ".dylib" && ext != ".dll") {
      continue;
    }

    void *handle = dlopen(entry.path().c_str(), RTLD_NOW);
    if (handle == nullptr) {
      continue;
    }

    LoadedPlugin plugin;
    plugin.path = entry.path();
    plugin.name = entry.path().stem().string();
    plugin.handle = handle;
    loaded_.push_back(std::move(plugin));
  }

  return common::Result<std::vector<LoadedPlugin>>::success(loaded_);
}

} // namespace ghostclaw::tools::plugin
