// mfd_test.cpp -- Unit test suite for EDVR Cockpit MFD framework
// Validates Option A (Declarative JSON MFD) implementation, Option B extensibility slots,
// gaze-directed focus tracking, UI input routing, and HUD rasterization.

#include "../../src/mfd/mfd_types.h"
#include "../../src/mfd/mfd_view_model.h"
#include "../../src/mfd/mfd_provider.h"
#include "../../src/mfd/mfd_font.h"
#include "../../src/mfd/mfd_renderer.h"
#include "../../src/mfd/mfd_gaze_tracker.h"
#include "../../src/mfd/mfd_input_router.h"
#include "../../src/mfd/mfd_compositor.h"
#include "../../src/mfd/mfd_manager.h"

#include <cassert>
#include <iostream>
#include <string>
#include <cmath>

using namespace edvr::mfd;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

// 1. Test geometric vector and quaternion math
int test_math() {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    Vec3 c = a + b;
    TEST_CHECK(std::abs(c.x - 5.0f) < 1e-5f, "Vec3 addition X");
    TEST_CHECK(std::abs(c.y - 7.0f) < 1e-5f, "Vec3 addition Y");
    TEST_CHECK(std::abs(c.z - 9.0f) < 1e-5f, "Vec3 addition Z");

    Vec3 norm = Vec3(0.0f, 3.0f, 4.0f).normalized();
    TEST_CHECK(std::abs(norm.length() - 1.0f) < 1e-5f, "Vec3 normalization length");
    TEST_CHECK(std::abs(norm.y - 0.6f) < 1e-5f, "Vec3 normalized Y");
    TEST_CHECK(std::abs(norm.z - 0.8f) < 1e-5f, "Vec3 normalized Z");

    // Quaternion identity rotation
    Quat q = Quat::fromEulerDegrees(0.0f, 0.0f, 0.0f);
    Vec3 fwd(0.0f, 0.0f, -1.0f);
    Vec3 rotated = q.rotate(fwd);
    TEST_CHECK(std::abs(rotated.x - fwd.x) < 1e-4f, "Identity rotation X");
    TEST_CHECK(std::abs(rotated.y - fwd.y) < 1e-4f, "Identity rotation Y");
    TEST_CHECK(std::abs(rotated.z - fwd.z) < 1e-4f, "Identity rotation Z");

    // Yaw 90 degrees left around +Y (right-handed): fwd (0, 0, -1) rotates to (-1, 0, 0)
    Quat qYaw90 = Quat::fromEulerDegrees(0.0f, 90.0f, 0.0f);
    Vec3 rotatedYaw = qYaw90.rotate(fwd);
    TEST_CHECK(std::abs(rotatedYaw.x - (-1.0f)) < 1e-4f, "Yaw 90 rotation X");
    TEST_CHECK(std::abs(rotatedYaw.z) < 1e-4f, "Yaw 90 rotation Z");

    return 0;
}

// 2. Test Option A: Declarative JSON MFD Provider
int test_declarative_provider() {
    DeclarativeMfdProvider provider("spansh_test", "SPANSH ROUTER");
    TEST_CHECK(provider.type() == MfdProviderType::kDeclarativeJson, "Provider type is DeclarativeJson (Option A)");
    TEST_CHECK(std::string(provider.id()) == "spansh_test", "Provider ID matches");

    std::string testJson = R"({
        "title": "SPANSH NEUTRON ROUTER",
        "subtitle": "CURRENT: SOL",
        "statusBadge": "SYNCED",
        "footerHint": "[Q/E] TABS  [NAV] SELECT",
        "tabs": [
            {
                "title": "ROUTE",
                "type": "list",
                "items": [
                    {"id": "wp1", "label": "Jackson's Lighthouse", "value": "12.4 LY", "sublabel": "Neutron Star", "badge": "CURRENT"},
                    {"id": "wp2", "label": "Prua Phoe NC-D d12-14", "value": "148.1 LY", "sublabel": "Supercharge Ready", "badge": "NEXT"},
                    {"id": "wp3", "label": "Dryooe Prou CS-B d1-2", "value": "294.0 LY", "sublabel": "Class M Star", "badge": "REFUEL"}
                ]
            },
            {
                "title": "METRICS",
                "type": "keyvalue",
                "items": [
                    {"key": "Total Jumps", "value": "14"},
                    {"key": "Distance Left", "value": "2,410.8 LY"},
                    {"key": "Fuel Reserve", "value": "88%"}
                ]
            }
        ]
    })";

    bool parsed = provider.loadFromJson(testJson);
    TEST_CHECK(parsed, "Declarative JSON parsed successfully");

    const auto& vm = provider.viewModel();
    TEST_CHECK(vm.title == "SPANSH NEUTRON ROUTER", "View model title");
    TEST_CHECK(vm.subtitle == "CURRENT: SOL", "View model subtitle");
    TEST_CHECK(vm.statusBadge == "SYNCED", "View model status badge");
    TEST_CHECK(vm.tabs.size() == 2, "View model tab count");
    TEST_CHECK(vm.tabs[0].title == "ROUTE", "Tab 0 title");
    TEST_CHECK(vm.tabs[0].items.size() == 3, "Tab 0 item count");
    TEST_CHECK(vm.tabs[0].items[1].label == "Prua Phoe NC-D d12-14", "Tab 0 item 1 label");
    TEST_CHECK(vm.tabs[1].title == "METRICS", "Tab 1 title");
    TEST_CHECK(vm.tabs[1].keyValues.size() == 3, "Tab 1 key-value count");

    // Test Navigation Inputs when unfocused (should NOT consume):
    provider.onFocusChanged(false);
    TEST_CHECK(!provider.onInput(MfdInputAction::kNextTab), "Unfocused input is ignored");
    TEST_CHECK(provider.viewModel().activeTabIndex == 0, "Tab remains unchanged when unfocused");

    // Test Navigation Inputs when focused (SHOULD consume):
    provider.onFocusChanged(true);
    TEST_CHECK(provider.onInput(MfdInputAction::kNextTab), "NextTab consumed when focused");
    TEST_CHECK(provider.viewModel().activeTabIndex == 1, "Tab cycled to 1");

    TEST_CHECK(provider.onInput(MfdInputAction::kPrevTab), "PrevTab consumed when focused");
    TEST_CHECK(provider.viewModel().activeTabIndex == 0, "Tab cycled back to 0");

    // Test list item selection
    TEST_CHECK(provider.viewModel().tabs[0].selectedIndex == 0, "Initial selection at index 0");
    TEST_CHECK(provider.onInput(MfdInputAction::kDown), "Down arrow moves selection");
    TEST_CHECK(provider.viewModel().tabs[0].selectedIndex == 1, "Selection moved to index 1");

    // Test Action Trigger on Select
    std::string selectedId;
    std::string selectedLabel;
    provider.setActionCallback([&](const std::string& id, const std::string& label) {
        selectedId = id;
        selectedLabel = label;
    });

    TEST_CHECK(provider.onInput(MfdInputAction::kSelect), "Select action executed");
    TEST_CHECK(selectedId == "wp2", "Selected item ID forwarded to callback");
    TEST_CHECK(selectedLabel == "Prua Phoe NC-D d12-14", "Selected item label forwarded to callback");

    return 0;
}

// 3. Test Option B: Scripted Code MFD Provider Architecture Slot
int test_scripted_provider_slot() {
    ScriptedMfdProvider scripted("custom_lua_slot", "scripts/cockpit_calc.lua");
    TEST_CHECK(scripted.type() == MfdProviderType::kScriptedCode, "Provider type is ScriptedCode (Option B)");
    TEST_CHECK(std::string(scripted.id()) == "custom_lua_slot", "Scripted provider ID matches");
    TEST_CHECK(scripted.scriptPath() == "scripts/cockpit_calc.lua", "Script path preserved");
    TEST_CHECK(std::string(scripted.title()) == "SCRIPTED MFD", "Scripted provider default title");

    // Verify polymorphic handling via IMfdProvider base pointer:
    IMfdProvider* basePtr = &scripted;
    TEST_CHECK(basePtr->type() == MfdProviderType::kScriptedCode, "Polymorphic type check for Option B");

    return 0;
}

// 4. Test Head-Gaze Tracker and Hysteresis State Machine
int test_gaze_tracker() {
    MfdGazeTracker tracker;
    tracker.setDwellEnterTime(0.10f); // 100ms dwell threshold
    tracker.setDwellExitTime(0.15f);  // 150ms release grace period
    tracker.setConeAngleDegrees(15.0f);

    // MFD situated on cockpit lower console: x=0.25m (right), y=-0.20m (down), z=-0.50m (forward)
    MfdPose mfdPose;
    mfdPose.position = Vec3(0.25f, -0.20f, -0.50f);
    mfdPose.orientation = Quat::fromEulerDegrees(-25.0f, -25.0f, 0.0f); // Angled up and toward pilot
    mfdPose.widthM = 0.25f;
    mfdPose.heightM = 0.15f;

    Vec3 headPos(0.0f, 0.0f, 0.0f);

    // Case A: Looking straight ahead (fwd = (0, 0, -1)). Should be unfocused.
    Vec3 headFwd(0.0f, 0.0f, -1.0f);
    MfdFocusState s1 = tracker.update(headPos, headFwd, mfdPose, 0.016f);
    TEST_CHECK(s1 == MfdFocusState::kUnfocused, "Looking straight forward is Unfocused");

    // Case B: Pilot turns head directly towards MFD
    Vec3 toMfd = (mfdPose.position - headPos).normalized();
    float angleToMfd = MfdGazeTracker::computeGazeAngle(headPos, toMfd, mfdPose.position);
    TEST_CHECK(angleToMfd < 0.1f, "Direct gaze angle is near 0 deg");

    // Frame 1 looking at MFD: transitions to kAcquiring
    MfdFocusState s2 = tracker.update(headPos, toMfd, mfdPose, 0.016f);
    TEST_CHECK(s2 == MfdFocusState::kAcquiring, "Initial gaze transitions to kAcquiring");

    // Advance 50ms (not yet at 100ms threshold): should still be acquiring
    MfdFocusState s3 = tracker.update(headPos, toMfd, mfdPose, 0.050f);
    TEST_CHECK(s3 == MfdFocusState::kAcquiring, "Dwell under threshold remains kAcquiring");

    // Advance past 100ms: transitions to kFocused!
    MfdFocusState s4 = tracker.update(headPos, toMfd, mfdPose, 0.060f);
    TEST_CHECK(s4 == MfdFocusState::kFocused, "Dwell past threshold transitions to kFocused");
    TEST_CHECK(tracker.isFocused(), "isFocused() returns true");

    // Case C: Pilot glances away (fwd = (0, 0, -1)): enters kReleasing grace period
    MfdFocusState s5 = tracker.update(headPos, headFwd, mfdPose, 0.050f);
    TEST_CHECK(s5 == MfdFocusState::kReleasing, "Glancing away enters kReleasing");

    // Pilot glances back at MFD before 150ms grace period expires: recovers to kFocused!
    MfdFocusState s6 = tracker.update(headPos, toMfd, mfdPose, 0.020f);
    TEST_CHECK(s6 == MfdFocusState::kFocused, "Recovering gaze restores kFocused");

    // Pilot turns away completely for > 150ms: transitions back to kUnfocused
    tracker.update(headPos, headFwd, mfdPose, 0.050f); // enters kReleasing
    MfdFocusState s7 = tracker.update(headPos, headFwd, mfdPose, 0.200f); // exceeds grace period
    TEST_CHECK(s7 == MfdFocusState::kUnfocused, "Exceeding release grace period returns to kUnfocused");

    return 0;
}

// 5. Test Input Router and UI Navigation Suppression
int test_input_router() {
    MfdInputRouter router;
    DeclarativeMfdProvider provider("nav_test");
    provider.loadFromJson(R"({
        "tabs": [
            { "title": "TAB1", "type": "list", "items": [{"id": "1", "label": "A"}, {"id": "2", "label": "B"}] },
            { "title": "TAB2", "type": "list", "items": [{"id": "3", "label": "C"}] }
        ]
    })");

    // Scenario 1: MFD NOT focused. Pressing Down arrow (keyDownDown = true)
    // Inputs must NOT be swallowed, passing through to game flight controls.
    bool swallowed1 = router.processInput(false, &provider,
                                          false, true, false, false,
                                          false, false, false, false);
    TEST_CHECK(!swallowed1, "Input is NOT swallowed when MFD is unfocused");
    TEST_CHECK(provider.viewModel().tabs[0].selectedIndex == 0, "Provider selection unchanged when unfocused");

    // Scenario 2: MFD IS focused.
    provider.onFocusChanged(true);
    // Release key to reset edge detector:
    router.processInput(true, &provider, false, false, false, false, false, false, false, false);

    // Press Down arrow while focused:
    bool swallowed2 = router.processInput(true, &provider,
                                          false, true, false, false,
                                          false, false, false, false);
    TEST_CHECK(swallowed2, "Input IS swallowed when MFD is focused");
    TEST_CHECK(provider.viewModel().tabs[0].selectedIndex == 1, "Provider received and handled Down arrow");

    // Press NextTab (E key):
    router.processInput(true, &provider, false, false, false, false, false, false, false, false); // release
    bool swallowed3 = router.processInput(true, &provider,
                                          false, false, false, false,
                                          false, false, true, false);
    TEST_CHECK(swallowed3, "NextTab IS swallowed when focused");
    TEST_CHECK(provider.viewModel().activeTabIndex == 1, "Provider cycled to Tab 1");

    return 0;
}

// 6. Test HUD Renderer and Pixel Rasterization
int test_renderer() {
    MfdRenderer renderer(256, 192);
    TEST_CHECK(renderer.width() == 256, "Renderer width");
    TEST_CHECK(renderer.height() == 192, "Renderer height");

    MfdViewModel model;
    model.title = "TEST HUD";
    model.subtitle = "SYSTEM OK";
    model.statusBadge = "LIVE";
    model.isFocused = true;

    MfdTab tab;
    tab.title = "NAV";
    tab.type = MfdTabType::kList;
    MfdListItem item1{"i1", "SOL", "0.0 LY", "Home", "ORIGIN"};
    MfdListItem item2{"i2", "ALPHA CENTAURI", "4.3 LY", "Triple Star", "WAYPOINT"};
    tab.items.push_back(item1);
    tab.items.push_back(item2);
    tab.selectedIndex = 0;
    model.tabs.push_back(tab);

    renderer.render(model);

    // Verify pixel buffer has been drawn to
    const uint32_t* pixels = renderer.pixelData();
    TEST_CHECK(pixels != nullptr, "Pixel buffer pointer valid");

    bool hasNonBackgroundPixels = false;
    uint32_t bgVal = palette::kBackground.toRgba();
    for (int i = 0; i < renderer.width() * renderer.height(); ++i) {
        if (pixels[i] != bgVal) {
            hasNonBackgroundPixels = true;
            break;
        }
    }
    TEST_CHECK(hasNonBackgroundPixels, "Renderer successfully drew HUD content into buffer");

    return 0;
}

// 7. Test MfdManager End-to-End Coordination
int test_manager() {
    auto& mgr = MfdManager::instance();
    bool init = mgr.initialize(320, 240);
    TEST_CHECK(init, "MfdManager initialized");

    MfdPose pose;
    pose.position = Vec3(0.0f, -0.2f, -0.6f);
    pose.orientation = Quat::fromEulerDegrees(-30.0f, 0.0f, 0.0f);

    auto provider = std::make_unique<DeclarativeMfdProvider>("main_center", "CENTRAL MFD");
    provider->loadFromJson(R"({
        "tabs": [{ "title": "MAIN", "type": "text", "items": ["SYSTEM DIAGNOSTICS: NOMINAL"] }]
    })");

    bool added = mgr.addSlot("center_console", std::move(provider), pose);
    TEST_CHECK(added, "Slot added to manager");
    TEST_CHECK(mgr.slotCount() == 1, "Slot count is 1");

    MfdSlot* slot = mgr.findSlot("center_console");
    TEST_CHECK(slot != nullptr, "Slot lookup by name succeeds");

    // Look directly at center console:
    Vec3 headPos(0.0f, 0.0f, 0.0f);
    Vec3 toCenter = (pose.position - headPos).normalized();

    // Tick enough time to acquire focus (150ms):
    for (int i = 0; i < 10; ++i) {
        mgr.update(headPos, toCenter, 0.020f);
    }

    TEST_CHECK(mgr.focusedSlot() == slot, "Center console is now the focused slot");
    TEST_CHECK(slot->provider->viewModel().isFocused, "Provider view model reflects focused state");

    // Execute render cycle
    mgr.render();
    TEST_CHECK(mgr.compositor() != nullptr, "Compositor valid");
    TEST_CHECK(mgr.compositor()->backend() == MfdCompositorBackend::kOpenXrQuadLayer, "Default compositor is OpenXR Quad Layer");

    mgr.shutdown();
    return 0;
}

int main() {
    std::cout << "[mfd_test] Running Cockpit MFD framework test suite..." << std::endl;

    if (test_math() != 0) return 1;
    std::cout << "  ok  Vec3 and Quat math" << std::endl;

    if (test_declarative_provider() != 0) return 1;
    std::cout << "  ok  Option A: Declarative JSON MFD Provider" << std::endl;

    if (test_scripted_provider_slot() != 0) return 1;
    std::cout << "  ok  Option B: Scripted Code MFD Provider Architecture Slot" << std::endl;

    if (test_gaze_tracker() != 0) return 1;
    std::cout << "  ok  Head-Gaze Tracking and Hysteresis State Machine" << std::endl;

    if (test_input_router() != 0) return 1;
    std::cout << "  ok  Input Router and Flight Control Protection" << std::endl;

    if (test_renderer() != 0) return 1;
    std::cout << "  ok  HUD Vector/Bitmap Renderer" << std::endl;

    if (test_manager() != 0) return 1;
    std::cout << "  ok  MfdManager End-to-End Orchestration" << std::endl;

    std::cout << "[mfd_test] ALL TESTS PASSED." << std::endl;
    return 0;
}
