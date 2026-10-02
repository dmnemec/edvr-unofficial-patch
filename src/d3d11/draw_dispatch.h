#pragma once

#include <d3d11.h>
#include <cstdint>
#include "../common/frame_context.h"
#include "draw_verdict.h"
#include "draw_census.h"

namespace edvr {

struct PluginDrawState {
    ID3D11DeviceContext* ctx = nullptr;
    char kind = 0;
    UINT count = 0;
    UINT instances = 0;
    DrawArgs args{};
    uint64_t vsHash = 0;
    uint64_t psHash = 0;
};

enum class ClaimSlot : uint8_t { Vs, Ps, Both, Wildcard };

typedef DrawVerdict (*PluginClaimFunc)(const FrameContext* ctx, const PluginDrawState* state);

struct DispatchEntry {
    const char* pluginId = nullptr;
    uint64_t vsHash = 0;
    uint64_t psHash = 0;
    ClaimSlot slot = ClaimSlot::Vs;
    PluginClaimFunc claimFunc = nullptr;
    int precedence = 0;  // Lower number = evaluated earlier
};

void registerDrawClaim(const DispatchEntry& entry);
void sortDispatchTable();
void resetDispatchTable();
size_t getDispatchCount();

// Hot path draw dispatch: evaluates claimants in declared precedence order.
// Zero allocation, no virtual calls, no std::function.
DrawVerdict dispatchDraw(const FrameContext* ctx, const PluginDrawState* state);

}  // namespace edvr
