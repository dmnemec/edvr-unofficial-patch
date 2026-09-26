# Flat Installer Issues, EDHM Mod Conflicts, and Cockpit Side-Menu Haze

## Status

State: Open (investigation active, initial hypotheses formulated).

Open Hypotheses:
- H-A (Tone/Exposure Bleed): EDHM modifies UI pixel shaders (such as
  hashes `AACF`/`CAD1`), producing out-of-range luminance or non-premultiplied
  alpha that post-tonemap, bloom, or composite passes treat as HDR specular
  energy, inducing a halo/haze.
- H-B (Projection / Motion Vector Recipe Mismatch): Side-menu quads angled at
  45-60 deg require precise jitter cancellation and camera-relative motion
  vector tagging in `flat_projection_recipes.h`. Altered shader hashes evade
  recipe recognition, falling back to unjittered screen-space draws that TAA
  smears during head rotation and ship turns.
- H-C (Depth/Stencil / UAV Clobber): EDHM custom HUD drawing modifies OM
  depth/stencil state, blend states, or bound UAV slots without preserving and
  restoring them, corrupting down-pipeline UI composite passes.

Ruled out:
- None yet (awaits flight telemetry and discriminating shader hash census).

Next steps:
- Deploy census logging for side-panel draw calls to identify exact vertex/pixel
  shader hashes under stock vs EDHM.
- Monitor `flat_projection_runtime` reset counters to detect temporal history
  reset storms in flat mode.
- Audit `tools/elite_oculus.py` and `src/installer/probe.cpp` PE version checks
  against recent Odyssey revisions and legacy Horizons executables.
- Perform flight tests isolating EDHM off vs on with both flat TAA and VR.

## Arc Context

This arc investigates three interlocked failure modes encountered in flat mode
and chained mod installations:

1. **Installer and Mode Detection Failures**: Version probing heuristics in
   `tools/elite_oculus.py` and `src/installer/probe.cpp` failing to distinguish
   recent Odyssey live revisions from legacy Horizons 3.8/4.0 executables.
2. **Flat Mode Temporal Reset Storms**: Unrecognized shader pairs in flat
   rendering causing projection recipe lookup failures in
   `flat_projection_recipes.h`, resulting in per-frame temporal history resets.
3. **Cockpit Side-Menu Haze and Smear**: Far-left (Navigation/Transactions) and
   far-right (System/Status) holo-menus angled at 45-60 degrees developing a
   glowing haze or temporal smear when EDHM (Elite Dangerous HUD Mod) is chained
   under temporal anti-aliasing (TAA/DLSS).

## Flat Installer & Detection Issues

### Executable Version Probing

The installer and launcher tools rely on PE binary inspection to target the
correct Elite Dangerous runtime:
- `src/installer/probe.cpp` checks PE export directories, product version
  strings, and internal timestamp stamps.
- `tools/elite_oculus.py` verifies executable identity before launching Oculus
  or flat harness tests.

Recent updates to Odyssey introduced minor version revisions where internal PE
string layouts shifted, causing probe routines to classify live Odyssey binaries
as unknown or erroneously trigger Horizons legacy fallback paths. If the wrong
profile is selected:
- Shader hash dictionaries mismatch the active game build.
- VR runtime bindings default to legacy OpenVR stubs instead of the native
  OpenXR translation layer.
- File installation places proxies into incorrect game subdirectories.

### Reset Storms in Flat Mode

In flat projection mode, EDVR tracks scene draw geometry via
`src/d3d11/flat_projection_recipes.h` and
`src/d3d11/flat_projection_runtime.cpp`:
- Every scene draw call is matched against known vertex shader (VS) and pixel
  shader (PS) hash recipes to determine projection matrices, jitter offsets,
  and history buffer association.
- When an unrecognized shader pair executes—especially during lighting passes,
  cockpit glass reflections, or custom mod draws—the runtime cannot establish
  camera continuity.
- To prevent reprojection artifacts, the runtime falls back to discarding the
  temporal history buffer.
- When unrecognized draws occur continuously, a "reset storm" ensues: history is
  wiped every frame, completely breaking TAA convergence and inducing intense
  flicker, strobing, and aliasing crawl across the scene.

### D3D11 Mod Chaining Architecture

EDVR acts as a primary `d3d11.dll` proxy in the game root directory. When users
run graphics mods like EDHM (Elite Dangerous HUD Mod) or ReShade:
- Chaining is configured via `edvr.ini` (`chain.d3d11 = EDHM_x64.dll` or
  `dxgi.dll`).
- EDVR intercepts `D3D11CreateDeviceAndSwapChain`, wraps the immediate context
  and swapchain, and forwards calls to the secondary mod DLL.
- Hook conflicts arise if the chained mod hooks identical virtual method
  table (VMT) slots, intercepts state changes (blend states, depth/stencil
  views), or intercepts render target transitions without propagating them
  cleanly back to EDVR.

## EDHM & Cockpit Side-Menu Haze Issue

### Symptoms

When EDHM is active alongside EDVR's temporal pass (TAA or DLSS):
- Central flight HUD elements (crosshairs, speed/throttle indicators, radar)
  remain sharp and properly positioned.
- Angled side-menus—specifically the far-left Navigation/Transactions panel
  and far-right System/Internal Status panel (angled at 45-60 degrees off
  head-center)—develop a distinct fuzzy, glowing haze.
- During head movements in VR or head-look/camera panning in flat mode, this
  haze smears into a trailing ghost that takes several frames to resolve.

### Candidate Hypotheses

#### Hypothesis A: Tone/Exposure Bleed (HDR Bloom Misinterpretation)

EDHM overrides stock UI pixel shaders (notably shaders identified in previous
draw investigations around hashes `AACF` and `CAD1`) to recolor HUD elements:
- Stock shaders output UI color with specific alpha and luminance bounds that
  the downstream tone-mapping and bloom passes anticipate.
- EDHM's replacement shaders may output colors in non-standard ranges (e.g.
  unclamped HDR values or non-premultiplied alpha).
- When the game's post-processing runs (tone-mapping, eye adaptation, bloom,
  or depth-of-field composite), the heightened luminance values on the angled
  menus exceed the bloom threshold, causing the post-process composite to bleed
  bright haze over adjacent cockpit geometry.

#### Hypothesis B: Projection / Motion Vector Recipe Mismatch

The side panels are rendered as planar quads rotated in 3D cockpit space:
- In `src/d3d11/flat_projection_recipes.h`, specific shader signatures are
  registered to identify cockpit UI quads and apply proper jitter cancellation
  or compute camera-relative motion vectors.
- If EDHM swaps out the pixel shader or hooks the vertex shader, the draw call
  hash pair `(VS_hash, PS_hash)` no longer matches any entry in
  `flat_projection_recipes.h`.
- Consequently, EDVR treats these draw calls as standard unclassified 3D
  geometry or unjittered 2D screen overlay:
  - Sub-pixel jitter applied to the camera is not cancelled for these draws.
  - Motion vectors for the angled geometry are omitted or set to zero.
  - The temporal resolver (TAA/DLSS) attempts to accumulate the jittered,
    moving samples across frames without valid motion vectors, resulting in
    heavy temporal blur and smearing.

#### Hypothesis C: Depth/Stencil / UAV Clobber

EDHM utilizes custom rendering passes to inject user-configured themes:
- Custom HUD drawing routines bind custom render targets, UAVs, or depth-stencil
  states to mask and recolor elements.
- If EDHM fails to snapshot and restore the active D3D11 Output Merger (OM)
  state (`OMGetDepthStencilState`, `OMGetRenderTargetsAndUnorderedAccessViews`),
  the subsequent composite passes run with corrupted stencil masks or invalid
  depth testing rules.
- Without proper stencil masking, the UI composite bleeds into the cockpit glass
  transparency or background depth buffer, yielding a diffuse, hazy boundary.

## Discriminating Tests & Verification Protocol

To distinguish between H-A, H-B, and H-C without wasting test flights:

### 1. Shader Hash Census Audit
- **Objective**: Determine whether EDHM alters shader hashes registered in
  `flat_projection_recipes.h`.
- **Method**: Add census logging on side-panel draw calls using
  `src/d3d11/object_probe.cpp` or draw census tooling:
  - Record `VS_hash`, `PS_hash`, topology, and draw call index for the
    Navigation panel with EDHM disabled (stock).
  - Record the same metrics with EDHM enabled.
- **Discriminator**: If hashes differ and match the unclassified draw list,
  **H-B** is directly confirmed as a contributor.

### 2. Luma & Pre-Bloom Buffer Inspection
- **Objective**: Verify if side-menu pixel luminance exceeds bloom threshold.
- **Method**: Use `src/d3d11/luma_probe.cpp` to sample max/mean luminance of the
  render target immediately after the UI draw pass and before the bloom pass.
- **Discriminator**: If EDHM produces peak luminance > 1.0 (or values significantly
  higher than stock) in UI regions, **H-A** is substantiated.

### 3. OM State Restoration Verification
- **Objective**: Detect state leaks across chained mod boundaries.
- **Method**: In EDVR's proxy layer, wrap the chained DLL call with state query
  assertions:
  ```cpp
  ID3D11DepthStencilState* pre_ds = nullptr;
  UINT pre_ref = 0;
  ctx->OMGetDepthStencilState(&pre_ds, &pre_ref);
  // Forward to chained mod (EDHM)
  // ...
  ID3D11DepthStencilState* post_ds = nullptr;
  UINT post_ref = 0;
  ctx->OMGetDepthStencilState(&post_ds, &post_ref);
  // Compare pre vs post
  ```
- **Discriminator**: If `pre_ds != post_ds` or blend state / UAV bindings are
  dirty upon return from EDHM, **H-C** is validated.

### 4. Flight Protocol
- Flight 1: Baseline clean install (no EDHM), flat mode TAA, cockpit seated.
  Verify zero side-panel smear and confirm recipe match in log.
- Flight 2: EDHM chained, stock theme, luma probe and census active. Check
  hash divergence and OM state integrity.
- Flight 3: EDHM chained with candidate fix applied (recipe aliasing or OM
  state guard).
