#include "cockpit_visuals_plugin.h"

#include "../../common/plugin_registry.h"
#include "../../d3d11/draw_dispatch.h"
#include "../../d3d11/night_vision.h"

namespace edvr {

namespace {

DrawVerdict claimNightVision(const FrameContext* ctx, const PluginDrawState* state) {
    (void)ctx;
    if (nightVisionShape(state->kind, state->count, state->instances) &&
        nightVisionMatches(state->kind, state->count, state->instances)) {
        return DrawVerdict::kNightVision;
    }
    return DrawVerdict::kNone;
}

void onConfigure(const Config& cfg) {
    nightVisionConfigure(const_cast<Config&>(cfg));
}

void onShutdown() {
    nightVisionShutdown();
}

bool wantsDrawGate() {
    return nightVisionWantsDraws();
}

const DispatchEntry kNightVisionClaim = {
    "cockpit-visuals",
    0xFCF7BD2896751D96ull,  // VS hash
    0xF786D34B5E118D5Eull,  // PS hash
    ClaimSlot::Both,
    &claimNightVision,
    50  // Precedence matching legacy vscreen.cpp position
};

const PluginDefinition kCockpitVisualsDef = {
    "cockpit-visuals",
    &onConfigure,
    &onShutdown,
    &wantsDrawGate
};

}  // namespace

void registerCockpitVisualsPlugin() {
    PluginRegistry::get().registerPlugin(kCockpitVisualsDef);
    registerDrawClaim(kNightVisionClaim);
}

}  // namespace edvr
