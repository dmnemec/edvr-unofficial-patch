#pragma once

#include <string>
#include <vector>

namespace edvr {

struct InstalledPlugin {
    std::string id;
    std::string version;
};

struct InstallReceipt {
    std::string edition;
    std::string profile;
    std::vector<InstalledPlugin> plugins;
    bool loaded = false;

    bool hasPlugin(const char* pluginId) const;
};

// Loads and parses edvr_install_receipt.json from the given directory or file path.
// Returns false if the file is missing or invalid, signaling the caller to apply
// the legacy monolithic fallback.
bool loadInstallReceipt(const wchar_t* path, InstallReceipt* outReceipt);

}  // namespace edvr
