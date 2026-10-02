#include "plugin_registry.h"

#include <cstring>
#include "log.h"

namespace edvr {

PluginRegistry& PluginRegistry::get() {
    static PluginRegistry instance;
    return instance;
}

void PluginRegistry::registerPlugin(const PluginDefinition& def) {
    if (!def.id) return;
    for (size_t i = 0; i < pluginCount_; ++i) {
        if (plugins_[i].id && std::strcmp(plugins_[i].id, def.id) == 0) {
            plugins_[i] = def;
            return;
        }
    }
    if (pluginCount_ < kMaxPlugins) {
        plugins_[pluginCount_++] = def;
    }
}

bool PluginRegistry::initialize(const wchar_t* receiptPath) {
    if (receiptPath && loadInstallReceipt(receiptPath, &receipt_)) {
        fallbackBaseline_ = false;
        Log::get().note("plugins: loaded install receipt (%ls) with %zu active plugin(s) [edition=%s, profile=%s]",
                        receiptPath, receipt_.plugins.size(), receipt_.edition.c_str(), receipt_.profile.c_str());
        return true;
    }

    fallbackBaseline_ = true;
    Log::get().note("plugins: install receipt not found or invalid; falling back to monolithic baseline (all plugins active)");
    return false;
}

bool PluginRegistry::isPluginInstalled(const char* pluginId) const {
    if (!pluginId) return false;
    if (fallbackBaseline_) {
        // Monolithic baseline fallback: every compiled first-party plugin is considered installed
        return true;
    }
    return receipt_.hasPlugin(pluginId);
}

void PluginRegistry::configure(const Config& cfg) {
    for (size_t i = 0; i < pluginCount_; ++i) {
        if (isPluginInstalled(plugins_[i].id) && plugins_[i].onConfigure) {
            plugins_[i].onConfigure(cfg);
        }
    }
}

void PluginRegistry::shutdown() {
    for (size_t i = 0; i < pluginCount_; ++i) {
        if (isPluginInstalled(plugins_[i].id) && plugins_[i].onShutdown) {
            plugins_[i].onShutdown();
        }
    }
}

bool PluginRegistry::wantsDrawGate() const {
    for (size_t i = 0; i < pluginCount_; ++i) {
        if (isPluginInstalled(plugins_[i].id) && plugins_[i].wantsDrawGate) {
            if (plugins_[i].wantsDrawGate()) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace edvr
