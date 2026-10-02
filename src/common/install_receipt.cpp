#include "install_receipt.h"

#include <windows.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace edvr {

bool InstallReceipt::hasPlugin(const char* pluginId) const {
    if (!pluginId) return false;
    for (const auto& p : plugins) {
        if (p.id == pluginId) return true;
    }
    return false;
}

namespace {

std::string extractStringValue(const std::string& json, const std::string& key, size_t startPos = 0) {
    const std::string needle = "\"" + key + "\"";
    const size_t k = json.find(needle, startPos);
    if (k == std::string::npos) return "";

    const size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return "";

    const size_t open = json.find('"', colon);
    if (open == std::string::npos) return "";

    std::string val;
    for (size_t i = open + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            val += json[++i];
            continue;
        }
        if (json[i] == '"') break;
        val += json[i];
    }
    return val;
}

}  // namespace

bool loadInstallReceipt(const wchar_t* path, InstallReceipt* outReceipt) {
    if (!path || !outReceipt) return false;
    outReceipt->plugins.clear();
    outReceipt->loaded = false;

    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > (4u << 20)) { // 4MB safety limit
        CloseHandle(hFile);
        return false;
    }

    std::string content;
    content.resize(size);
    DWORD readBytes = 0;
    if (!ReadFile(hFile, &content[0], size, &readBytes, nullptr) || readBytes != size) {
        CloseHandle(hFile);
        return false;
    }
    CloseHandle(hFile);

    outReceipt->edition = extractStringValue(content, "edition");
    outReceipt->profile = extractStringValue(content, "profile");

    // Parse installed_plugins array
    const size_t arrayKey = content.find("\"installed_plugins\"");
    if (arrayKey != std::string::npos) {
        const size_t openBracket = content.find('[', arrayKey);
        const size_t closeBracket = (openBracket != std::string::npos) ? content.find(']', openBracket) : std::string::npos;
        if (openBracket != std::string::npos && closeBracket != std::string::npos) {
            size_t cursor = openBracket + 1;
            while (cursor < closeBracket) {
                const size_t openObj = content.find('{', cursor);
                if (openObj == std::string::npos || openObj >= closeBracket) break;
                const size_t closeObj = content.find('}', openObj);
                if (closeObj == std::string::npos || closeObj > closeBracket) break;

                const std::string objStr = content.substr(openObj, closeObj - openObj + 1);
                const std::string id = extractStringValue(objStr, "id");
                const std::string ver = extractStringValue(objStr, "version");

                if (!id.empty()) {
                    outReceipt->plugins.push_back({id, ver});
                }
                cursor = closeObj + 1;
            }
        }
    }

    outReceipt->loaded = true;
    return true;
}

}  // namespace edvr
