#include "draw_dispatch.h"

#include <algorithm>
#include "../common/plugin_registry.h"

namespace edvr {

namespace {

constexpr size_t kMaxDispatchEntries = 128;
DispatchEntry s_dispatchTable[kMaxDispatchEntries];
size_t s_dispatchCount = 0;

}  // namespace

void registerDrawClaim(const DispatchEntry& entry) {
    if (!entry.claimFunc || s_dispatchCount >= kMaxDispatchEntries) return;
    s_dispatchTable[s_dispatchCount++] = entry;
}

void sortDispatchTable() {
    std::sort(s_dispatchTable, s_dispatchTable + s_dispatchCount,
              [](const DispatchEntry& a, const DispatchEntry& b) {
                  return a.precedence < b.precedence;
              });
}

void resetDispatchTable() {
    s_dispatchCount = 0;
}

size_t getDispatchCount() {
    return s_dispatchCount;
}

DrawVerdict dispatchDraw(const FrameContext* ctx, const PluginDrawState* state) {
    if (!state) return DrawVerdict::kNone;

    const auto& registry = PluginRegistry::get();

    for (size_t i = 0; i < s_dispatchCount; ++i) {
        const auto& entry = s_dispatchTable[i];
        if (!entry.claimFunc) continue;

        // If registered under a plugin, skip if plugin is not installed
        if (entry.pluginId && !registry.isPluginInstalled(entry.pluginId)) {
            continue;
        }

        // Match shader slots
        switch (entry.slot) {
        case ClaimSlot::Vs:
            if (entry.vsHash != state->vsHash) continue;
            break;
        case ClaimSlot::Ps:
            if (entry.psHash != state->psHash) continue;
            break;
        case ClaimSlot::Both:
            if (entry.vsHash != state->vsHash || entry.psHash != state->psHash) continue;
            break;
        case ClaimSlot::Wildcard:
            // Matches any shader; secondary predicates checked inside claimFunc
            break;
        }

        const DrawVerdict verdict = entry.claimFunc(ctx, state);
        if (verdict != DrawVerdict::kNone) {
            return verdict;
        }
    }

    return DrawVerdict::kNone;
}

}  // namespace edvr
