#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include "../../src/d3d11/draw_verdict.h"
#include "../../src/d3d11/draw_dispatch.h"
#include "../../src/common/frame_context.h"
#include "../../src/common/plugin_registry.h"
#include "../../src/common/install_receipt.h"

using namespace edvr;

namespace {

int g_checks = 0;

void expect(bool condition, const char* msg) {
    ++g_checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

// Test claimant functions
DrawVerdict claimTestA(const FrameContext* ctx, const PluginDrawState* state) {
    (void)ctx; (void)state;
    return DrawVerdict::kGlareClamp;
}

DrawVerdict claimTestB(const FrameContext* ctx, const PluginDrawState* state) {
    (void)ctx; (void)state;
    return DrawVerdict::kRemlok;
}

DrawVerdict claimTestWildcard(const FrameContext* ctx, const PluginDrawState* state) {
    (void)ctx;
    if (state->count == 42) {
        return DrawVerdict::kPanel;
    }
    return DrawVerdict::kNone;
}

DrawVerdict claimNightVisionCanary(const FrameContext* ctx, const PluginDrawState* state) {
    (void)ctx;
    if (state->kind == 'X' && state->count == 240 && state->instances == 1 &&
        state->vsHash == 0xFCF7BD2896751D96ull && state->psHash == 0xF786D34B5E118D5Eull) {
        return DrawVerdict::kNightVision;
    }
    return DrawVerdict::kNone;
}

}  // namespace

int main(int argc, char** argv) {
    bool selfTest = false;
    bool dryRun = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-test") == 0) selfTest = true;
        if (std::strcmp(argv[i], "--dry-run") == 0) dryRun = true;
    }

    if (dryRun && !selfTest) {
        std::printf("verdict_replay_test: dry-run ok\n");
        return 0;
    }

    resetDispatchTable();
    expect(getDispatchCount() == 0, "initial dispatch table is empty");

    // 1. Register claims with arbitrary precedences
    DispatchEntry e1;
    e1.pluginId = "test-plugin-a";
    e1.vsHash = 0x1111222233334444ull;
    e1.slot = ClaimSlot::Vs;
    e1.claimFunc = &claimTestA;
    e1.precedence = 100;
    registerDrawClaim(e1);

    DispatchEntry e2;
    e2.pluginId = "test-plugin-b";
    e2.psHash = 0x5555666677778888ull;
    e2.slot = ClaimSlot::Ps;
    e2.claimFunc = &claimTestB;
    e2.precedence = 50;  // Higher priority (earlier)
    registerDrawClaim(e2);

    DispatchEntry e3;
    e3.pluginId = "cockpit-visuals";
    e3.vsHash = 0xFCF7BD2896751D96ull;
    e3.psHash = 0xF786D34B5E118D5Eull;
    e3.slot = ClaimSlot::Both;
    e3.claimFunc = &claimNightVisionCanary;
    e3.precedence = 20;
    registerDrawClaim(e3);

    DispatchEntry e4;
    e4.pluginId = "wildcard-plugin";
    e4.slot = ClaimSlot::Wildcard;
    e4.claimFunc = &claimTestWildcard;
    e4.precedence = 200;
    registerDrawClaim(e4);

    expect(getDispatchCount() == 4, "registered 4 claims");

    // Sort table by precedence
    sortDispatchTable();

    // 2. Test fallback baseline: no receipt loaded -> all plugins active
    PluginRegistry::get().initialize(nullptr);
    expect(PluginRegistry::get().isFallbackBaseline() == true, "fallback baseline active when receipt is null");
    expect(PluginRegistry::get().isPluginInstalled("cockpit-visuals") == true, "cockpit-visuals installed in fallback");

    FrameContext ctx{};
    ctx.eyeWidth = 2048;
    ctx.eyeHeight = 2048;
    ctx.stereoActive = true;

    // 3. Test Night Vision canary match
    PluginDrawState nvState{};
    nvState.kind = 'X';
    nvState.count = 240;
    nvState.instances = 1;
    nvState.vsHash = 0xFCF7BD2896751D96ull;
    nvState.psHash = 0xF786D34B5E118D5Eull;

    DrawVerdict vNv = dispatchDraw(&ctx, &nvState);
    expect(vNv == DrawVerdict::kNightVision, "night vision canary claimed successfully");

    // 4. Test PS-only match
    PluginDrawState psState{};
    psState.kind = 'D';
    psState.count = 100;
    psState.instances = 1;
    psState.vsHash = 0x9999999999999999ull;
    psState.psHash = 0x5555666677778888ull;

    DrawVerdict vPs = dispatchDraw(&ctx, &psState);
    expect(vPs == DrawVerdict::kRemlok, "PS-only claimant matched");

    // 5. Test VS-only match
    PluginDrawState vsState{};
    vsState.kind = 'D';
    vsState.count = 50;
    vsState.instances = 1;
    vsState.vsHash = 0x1111222233334444ull;
    vsState.psHash = 0xAAAAAAAAAAAAAAAAull;

    DrawVerdict vVs = dispatchDraw(&ctx, &vsState);
    expect(vVs == DrawVerdict::kGlareClamp, "VS-only claimant matched");

    // 6. Test Wildcard shape claim
    PluginDrawState wildcardState{};
    wildcardState.kind = 'D';
    wildcardState.count = 42;
    wildcardState.instances = 1;
    wildcardState.vsHash = 0x1234ull;
    wildcardState.psHash = 0x5678ull;

    DrawVerdict vWildcard = dispatchDraw(&ctx, &wildcardState);
    expect(vWildcard == DrawVerdict::kPanel, "wildcard shape claimant matched");

    // 7. Non-matching draw returns kNone
    PluginDrawState noneState{};
    noneState.kind = 'D';
    noneState.count = 999;
    noneState.instances = 1;
    noneState.vsHash = 0x0001ull;
    noneState.psHash = 0x0002ull;

    DrawVerdict vNone = dispatchDraw(&ctx, &noneState);
    expect(vNone == DrawVerdict::kNone, "unmatched draw returns kNone");

    std::printf("PASS: verdict_replay_test (%d checks)\n", g_checks);
    return 0;
}
