# EDVR Phase 1 Plugin Architecture Implementation Blueprint

## 1. Exact File Paths for Core & State
- **Core Registry & Base Lifecycle**: `src/common/plugin_registry.h` / `.cpp`
- **Draw Dispatch Table**: `src/d3d11/draw_dispatch.h` / `.cpp`
- **FrameContext**: `src/common/frame_context.h`
- **Install Receipt Handler**: `src/common/install_receipt.h` / `.cpp`
- **Legacy Thunks (Migration)**: `src/d3d11/legacy_verdicts.h` / `.cpp`
- **D3D11 Frame Hooks & Services**: `src/d3d11/frame_services.h` / `.cpp`
- **GPU Census**: `src/d3d11/gpu_census.h` / `.cpp` (D3D11 metrics) and `src/common/census.h` / `.cpp` (platform-agnostic aggregation)
- **First-Party Plugin (Night Vision)**: `src/plugins/cockpit-visuals/night_vision.cpp` and `plugin.manifest.json`
- **Test Rig**: `tools/verdict_replay_test/verdict_replay_test.cpp` and `tools/verdict_replay_test/mutants.py` (or Python self-test companion)

## 2. Structs, C Function Pointers, and Data Layouts
To uphold the **Performance North Star** (no allocations, no virtual fan-out, no `std::function`), we use PODs and flat arrays.

### Plugin Lifecycle Definition
```cpp
// src/common/plugin_registry.h
struct PluginDefinition {
    const char* id;
    void (*onConfigure)(const Config* cfg);
    void (*onShutdown)();
    bool (*wantsDrawGate)();
    const struct DispatchEntry* claims;
    size_t claim_count;
};
```

### FrameContext & DrawState
A zero-allocation, POD struct passed to hooks, decoupling cross-plugin state:
```cpp
// src/common/frame_context.h
struct FrameContext {
    uint32_t eye_width;
    uint32_t eye_height;
    bool is_fss_active;
    bool is_flat_mode;
};

// src/d3d11/draw_dispatch.h
struct DrawState {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t srv_width;
    uint32_t srv_height;
    // Bound Constant Buffers / resource properties...
};
```

### Dual-Shader Draw Dispatch Table
Support multi-slot matching and fallback shapes:
```cpp
// src/d3d11/draw_dispatch.h
#include "frame_context.h"

enum class BindSlot : uint8_t { Vs, Ps, Both, Wildcard };

typedef DrawVerdict (*PluginClaimFunc)(const FrameContext* ctx, const DrawState* state);

struct DispatchEntry {
    const char* plugin_id;
    uint64_t shader_hash; // FNV1a64, or 0 for Wildcard
    BindSlot slot;
    PluginClaimFunc claim_func;
    int precedence;       // Lower = earlier execution
};

// Flat, pre-sorted array initialized once during setup
extern DispatchEntry g_dispatch_table[MAX_DISPATCH_ENTRIES];
extern size_t g_dispatch_count;
```

### Binding Shadow Interest Caching
When a shader is bound, the binding shadow caches its exact sub-range in the dispatch table, making per-draw lookup O(1) and cache-friendly. For draws lacking a specific shader match, Wildcard/Fallback entries are evaluated.
```cpp
struct ShaderInterest {
    bool has_interest;
    uint16_t dispatch_start_idx;
    uint16_t dispatch_count;
};
```
During a draw, the core evaluates both the cached `[dispatch_start_idx, dispatch_start_idx + dispatch_count)` block and any registered Wildcards (e.g. for purely shape-based verdicts like `kPanel`) sequentially.

## 3. Mapping 19 DrawVerdicts to Legacy Thunks
The existing ladder in `vscreen.cpp` (`beginPanelOverride`) must be disassembled carefully:
1. Each of the 19 checks is extracted into a standalone static function (e.g., `LegacyThunks::checkSunGlare`).
2. Each function is registered into `g_dispatch_table` with its required `shader_hash` and `BindSlot` (or `Wildcard` for purely shape/CB fallback claims).
3. **Precedence:** We assign integers reflecting their exact top-to-bottom order in `vscreen.cpp` (e.g., Sun Glare = 10, Radar Contact = 20). 
4. At draw time, contested shaders and wildcards are evaluated in exact legacy order, preserving existing claim precedence without a monolithic ladder.

## 4. Extracting Night Vision (`cockpit-visuals`)
1. Remove the night vision branch from `vscreen.cpp` ladder.
2. Implement lifecycle methods (`onConfigure`, `onShutdown`, `wantsDrawGate`) in `src/plugins/cockpit-visuals/night_vision.cpp`. The core queries `wantsDrawGate()` to ensure night vision is not starved.
3. Retain `kNightVision` in `forwardVerdictBegin` and `forwardVerdictEnd`, delegating immediately to the plugin's `nightVisionBegin`/`nightVisionEnd` implementations.
4. Implement `DrawVerdict ClaimNightVision(const FrameContext* ctx, const DrawState* state)` and define it in a `PluginDefinition` exposed to the registry.
5. In `plugin_registry.cpp`, the core registers Night Vision's definitions and appends its entries to `g_dispatch_table` if installed.

## 5. Decoupling Static Props & Scheduler Stack Probe
Currently bound to `temporalPassConfigure` (`temporal_pass.cpp` 6138 and 6143), meaning they die if `temporal-aa` is uninstalled.
- **Fix:** Move them to a dedicated `CoreServices::onFrameConfigure()` and `CoreServices::onFrameTick()` in `src/d3d11/frame_services.cpp`.
- The core will independently read the config key `fix.static_prop_updates` and manage the scheduler probe, completely decoupling D3D11-dependent hooks from the AA pipeline.

## 6. Decoupling Census Families into Core Registry
The core must own the census so logs remain intact and performant. To avoid string lookups (~17k calls/frame):
- **Fix:** Use a compact enum or integer ID (`uint8_t family_id`), matching `GpuCensusSection` / `flat_cpu.h`.
- Maintain D3D11 GPU metric collection in `src/d3d11/gpu_census.*`, and place platform-agnostic metric aggregation/reporting in `src/common/census.*`.
- Expose `edvr::census::beginFamily(uint8_t family_id)` and `edvr::census::endFamily()`.
- Plugins call these with fast enums.

## 7. Receipt Fallback & Build Wiring
### Receipt Fallback
If `edvr_install_receipt.json` is missing or invalid, the `plugin_registry.cpp` defaults to a **legacy monolithic baseline fallback**. It automatically registers all features as "installed" to ensure parity with the pre-plugin behavior, rather than silently failing to a broken state.

### Byte-Identical Verdict Replay Test Rig
- **Path**: `tools/verdict_replay_test/verdict_replay_test.cpp` and `tools/verdict_replay_test/mutants.py`.
- **Mechanism**: The C++ rig statically links `draw_dispatch.cpp` and `legacy_verdicts.cpp`. It runs embedded deterministic fixtures against `g_dispatch_table` to assert a byte-for-byte match with expected `DrawVerdict`s.
- **Build Wire-Up**: In `build.bat`, after C++ compilation, we wire up the rig with support for `--dry-run` and `--self-test`:
  `python tools\verdict_replay_test\mutants.py --self-test`
  If it fails, the build aborts, preventing broken precedence from reaching a test flight.
