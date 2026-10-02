#pragma once

#include <cstddef>
#include "config.h"
#include "install_receipt.h"

namespace edvr {

struct PluginDefinition {
    const char* id = nullptr;
    void (*onConfigure)(const Config& cfg) = nullptr;
    void (*onShutdown)() = nullptr;
    bool (*wantsDrawGate)() = nullptr;
};

class PluginRegistry {
public:
    static PluginRegistry& get();

    void registerPlugin(const PluginDefinition& def);
    bool initialize(const wchar_t* receiptPath);
    void configure(const Config& cfg);
    void shutdown();

    bool isPluginInstalled(const char* pluginId) const;
    bool wantsDrawGate() const;

    const InstallReceipt& getReceipt() const { return receipt_; }
    bool isFallbackBaseline() const { return fallbackBaseline_; }

    size_t pluginCount() const { return pluginCount_; }
    const PluginDefinition* getPlugin(size_t index) const {
        return (index < pluginCount_) ? &plugins_[index] : nullptr;
    }

private:
    PluginRegistry() = default;
    static constexpr size_t kMaxPlugins = 32;
    PluginDefinition plugins_[kMaxPlugins];
    size_t pluginCount_ = 0;
    InstallReceipt receipt_;
    bool fallbackBaseline_ = false;
};

}  // namespace edvr
