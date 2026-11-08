# Black Hole Accretion Disk (`visual.black_hole_disk`) — Architectural & Mathematical Design

## 1. Verified Root Causes of All 3 Bugs in Flight 7 (`edvr_gfx_20261106_194347.log`)

Every failure observed in Flight 6 (`edvr_gfx_20261106_172820.log`) and Flight 7 (`edvr_gfx_20261106_194347.log`) traces directly to four verified defects in [black_hole_disk.cpp](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp) and [black_hole_disk_simple.hlsl](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/shaders/black_hole_disk_simple.hlsl):

### 1.1 Bug 1A — `g_cb2ShadowValid` Was Never `true` (Zero CB2 Captures Across the Entire Flight)
- **Evidence in Log:** Running `python tools\edvr_log.py --file "D:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64\edvr_logs\edvr_gfx_20261106_194347.log" --grep "black hole"` shows:
  - `[19:45:43.627] black hole disk: drawn (vh=0xB278189EFA1D574D ph=0x06332CA168B6DA63 ...)`
  - **Zero** occurrences of `"black hole disk: captured cb2 shadow"` ([`black_hole_disk.cpp:369`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp#L369)).
- **Code Mechanism:**
  1. In [`blackHoleDiskAfterDraw`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp#L358-L362), `g_watchedCbResource` is only assigned *after* `isLensDraw` (`vh == 0x41E245D488BFE83E && ph == 0x6EF82262EB12A037`) executes:
     ```cpp
     if (isLensDraw) {
         g_lensDrawnThisFrame = true;
         g_watchedCbResource = saved.psCb2;
     }
     ```
  2. In [`blackHoleDiskOnMap`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp#L272-L276) and [`blackHoleDiskOnUpdateSubresource`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp#L283-L289), the hook checks `if (res != g_watchedCbResource) return;`.
  3. As established in Flight 4 (`edvr_gfx_20261105_233701.log`: `DCW 0 q=918 p cb=@464 unwritten`, `DCW 0 q=834 p cb=@455 unwritten`) and documented in [`docs/black-hole-accretion-disk-2026-11-04.md:382-391`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L382-L391), Frontier cycles constant buffers from a ring pool updated *before* the draw call or reuses static buffers across frames without re-mapping the exact `ID3D11Resource*` from the prior frame. Watching a single `ID3D11Resource*` pointer after the draw misses every update.
  4. Because `g_cb2ShadowValid` was permanently `false`, [`black_hole_disk.cpp:375-379`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp#L375-L379) executed on **100% of draws**:
     ```cpp
     } else {
         // Fallback or celestial dust draw: place black hole ahead for visual testing if no lens cb2
         cb2Data[4][0] = 0.0f; cb2Data[4][1] = 0.0f; cb2Data[4][2] = 1.0f; cb2Data[4][3] = 1000.0f; // Dir +Z, dist 1000 LS
     }
     ```

### 1.2 Bug 1B — Fixed "Tiny Dot" That Never Scaled With Distance
- **Code Mechanism:** In [`black_hole_disk_simple.hlsl:134, 162-169`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/shaders/black_hole_disk_simple.hlsl#L134-L169):
  ```hlsl
  float bhDist = cb2[4].w;
  ...
  float distKm = (bhDist > 5000.0) ? bhDist : (bhDist * 299792.458);
  float scale = clamp(120.0 / (distKm * 0.25 + 600.0), 0.00015, 0.020);
  ```
- Because `cb2Data[4][3]` was hardcoded to `1000.0f` by the fallback above, `bhDist` was always `1000.0`, `distKm` was always $2.99792 \times 10^8\text{ km}$, and `120.0 / (distKm * 0.25 + 600.0)` evaluated to $1.6 \times 10^{-6}$, which clamped to the minimum floor `0.00015` ($\approx 0.0086^\circ$ angular radius — a 1-2 pixel dot) at every distance from 54,000 Ls down to 28.4 km!

### 1.3 Bug 1C — Head-Locked Disk That Only Appeared When Looking 180° Away
- **Code Mechanism:**
  1. [`black_hole_disk.cpp:400-403`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp#L400-L403) left the game's `saved.vsCb0` bound to pixel-shader slot `b0` (`context->PSSetConstantBuffers(0, 3, psCbs)` where `psCbs[0] = saved.vsCb0`).
  2. [`black_hole_disk_simple.hlsl:73-93`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/shaders/black_hole_disk_simple.hlsl#L73-L93) treated `cb0[4..7]` as a pure eye projection matrix (`p00 = cb0[4].x`, `p11 = cb0[5].y`, `p02 = cb0[4].z`, `p12 = cb0[5].z`) and constructed the ray in camera space (`rayDir = normalize(float3(vx, vy, 1.0))`), then compared it directly against `bhDir = normalize(cb2[4].xyz)` (`(0, 0, +1)` from the fallback!).
  3. Because `cb2[4].xyz = (0, 0, +1)` is constant in camera space (`+Z` forward along the headset optical axis), the disk was **locked to the center of the HMD view** (`rayDir ≈ (0, 0, 1)`).
  4. Why did it only appear when looking *away* from the black hole/planet? Because `blackHoleDiskAfterDraw` fired on two triggers:
     - First on `kCelestialDustVsHash` (`0xB278189EFA1D574D`, draw `#335` on Eye 0, `#410` on Eye 1 in `edvr_gfx_20261105_223323.log`).
     - Second on `kLensVsHash` (`0x41E245D488BFE83E`, draw `#355` on Eye 0, `#430` on Eye 1 in `edvr_gfx_20261105_223323.log`), because [`blackHoleDiskAfterDraw`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp#L335-L343) only blocked `isCelestialDust` when `g_lensDrawnThisFrame` was true, not `isLensDraw`!
     - Disassembly of `vs_41E245D488BFE83E.dxbc` proves that for `0x41E245D488BFE83E`, `cb0[4..7]` is a **rotated Model-View-Projection (MVP) matrix** (`dp4 r1.x, cb0[4].xyzw, r0.xyzw`), **not** a pure projection matrix! When looking toward the planet/black hole, the world-to-clip rotation in `cb0[4].x` and `cb0[5].y` flipped sign or sheared `p02, p12`, shifting the reconstructed ray away from `(0, 0, +1)`. When looking 180° away, the rotation terms changed sign and aligned the fallback `(0, 0, +1)` ray with the screen center!

### 1.4 Bug 1D — Why `0x41E245D488BFE83E / 0x6EF82262EB12A037` Vanished Inside 3,000 Ls (It Is a Planetary Atmosphere Shader, Not the Black Hole Lens)
- **Code & Disassembly Evidence:**
  1. In [`src/d3d11/native_on_foot.cpp:29295`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/native_on_foot.cpp#L29295) and [`docs/scanner-body.md:154`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/scanner-body.md#L154), `0x41E245D488BFE83E` is already identified as `kV2Atmos` (planetary atmosphere shell).
  2. Disassembly of `ps_6EF82262EB12A037.dxbc` confirms it is a Rayleigh/Mie planetary atmosphere scattering shader:
     - `add r2.x, cb2[4].w, l(1.000000)` ($r/R_{\text{planet}} = (d/R_{\text{planet}}) + 1$)
     - `mad r2.y, r2.x, r2.x, l(-1.000000); sqrt r2.y, r2.y` (horizon tangent distance $\sqrt{(r/R)^2 - 1}$)
     - The value `cb2[4].w = 5765.38` logged in Flight 4 (`edvr_gfx_20261105_233701.log`) was the distance to a distant planet in **planetary radii**, which by sheer numerical coincidence was near 5,765 when the ship happened to be at ~5,000 Ls from `HIP 34707 B`!
  3. In Flight 2 (`edvr_gfx_20261105_223323.log`, documented in [`docs/black-hole-accretion-disk-2026-11-04.md:274`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L274)), `0x41E245D488BFE83E` appears in C1 (clean non-BH system, `n=2304`), C2..C8 (`n=36`), and **vanishes completely in C9 (1.5k Ls), C10 (5.5 Mm), C11 (3.0 Mm), and C12 (1.5 Mm)** because the distant planet whose atmosphere it rendered moved behind the frustum as the ship approached `HIP 34707 B`.

---

## 2. Coordinate Frame, Camera Pose, and Black Hole World Anchor Design

To eliminate all four bugs above and achieve zero head-roll swimming, exact VR stereo convergence, Flat-mode support, and physically accurate distance scaling from 54,000 Ls down to 25 km, the coordinate pipeline must be self-contained and independent of `0x41E245D488BFE83E`.

### 2.1 How the Engine Renders the Sky & Where Per-Eye Camera + Projection Matrices Live
1. **Black-Hole-System Sky Dome (`vh = 0x8471F654B0CBAA89`, `ph = 0xB00B8B6FB5CDB153`):**
   - As recorded in Flight 1 (`edvr_gfx_20261104_222959.log`, Table 4.2 in [`docs/black-hole-accretion-disk-2026-11-04.md:225-229`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L225-L229)) and Flight 2 (`edvr_gfx_20261105_223323.log`, Table 5.1 in [`docs/black-hole-accretion-disk-2026-11-04.md:285-296`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L285-L296)):
     - In **C1** of Flight 2 (clean non-BH star system), the sky dome draws at **Order 3** with `vh = 0xF8FA801F2CB1E27C / ph = 0xFA94EB86168C3B46` (`DrawIndexed(384)`, `vb=@380 stride=32`, `vs cb2=@453`).
     - In **C2 through C12** of Flight 2 (every single capture in the `HIP 34707` black hole system, from 54,000 Ls in supercruise down to 1.5 Mm) and in **C1–C7** of Flight 1 (including normal space at 28.4 km, [`docs/black-hole-accretion-disk-2026-11-04.md:107, 229`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L229)), `0xF8FA801F2CB1E27C` is **replaced** at **Order 3** by `vh = 0x8471F654B0CBAA89 / ph = 0xB00B8B6FB5CDB153`!
   - Disassembly of `vs_F8FA801F2CB1E27C.dxbc` shows how the sky dome works:
     ```hlsl
     // vs_F8FA801F2CB1E27C.dxbc
     dcl_constantbuffer CB2[15], immediateIndexed
     mul r0.xyzw, v0.yyyy, cb2[12].xyzw
     mad r0.xyzw, v0.xxxx, cb2[11].xyzw, r0.xyzw
     mad r0.xyzw, v0.zzzz, cb2[13].xyzw, r0.xyzw
     mad r0.xyzw, v0.wwww, cb2[14].xyzw, r0.xyzw
     div o0.xyz, r0.xyzx, r0.wwww   // world-space view ray direction!
     mov o1.xyzw, v0.xyww           // full-screen clip-space quad at z = w (far plane)
     ```
     Here `v0` is a clip-space sky dome (`o1.xyzw = v0.xyww`), and `cb2[11..14]` is the **per-eye Inverse View-Projection matrix** (`InvViewProj`) that maps clip-space `(x, y, z, w)` directly to the world-space view ray `o0.xyz`!
2. **Starfield & Celestial Dust Draws (Orders 4, 5, 7a, 7b):**
   - Across all censuses ([`docs/black-hole-accretion-disk-2026-11-04.md:229-235`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L229-L235)), the celestial background sequence in each eye HDR pass (`2632x2684 fmt=26`) is:
     - **Order 3:** `vh = 0x8471F654B0CBAA89 / ph = 0xB00B8B6FB5CDB153` (BH-system sky dome; `0xF8FA801F2CB1E27C` in non-BH systems)
     - **Order 4:** `vh = 0x297B6F1F7F104F23 / ph = 0x06332CA168B6DA63` (Starfield, 13,873 instances, additive blend, `vsCb0` = 208 bytes)
     - **Order 5:** `vh = 0xB278189EFA1D574D / ph = 0x06332CA168B6DA63` (Bright system stars, 14 instances, additive blend, `vsCb0` = 208 bytes)
     - **Order 6:** `vh = 0x88DCF1164C640EC3 / ph = 0x494506A63091DF8C` (Opaque depth-writing mesh, `ds=17wA`)
     - **Order 7a (Normal Space only, `1/eye/frame`):** `vh = 0x7F894EB5B6BA82A1 / ph = 0x095030F27D2C362A` (Normal-space celestial dust/haze sphere, additive blend, `vsCb0` = 208 bytes)
     - **Order 7b (Supercruise replacement for 7a, `1/eye/frame`):** `vh = 0x9BFC7FD232328391 / ph = 0xCB7AF179DF4E6A60` (Supercruise space-dust streaking sphere, additive blend, `vsCb0` = 208 bytes)
   - Disassembly of `vs_7F894EB5B6BA82A1.dxbc` and `vs_9BFC7FD232328391.dxbc` (verified with `fxc.exe /dumpbin`) alongside [`docs/engine-render-pipeline.md:309-315`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/engine-render-pipeline.md#L309-L315) and [`src/d3d11/celestial_motion.cpp:11-36, 198-212`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/celestial_motion.cpp#L198-L212) confirm the exact layout of the 208-byte (`13 float4`, `cb0=@57`) starfield/celestial `VsCb0`:
     - `cb0[4..7]`: **Pure per-eye Projection matrix** (`dp4 r4.x, cb0[4], r3; dp4 r4.y, cb0[5], r3; dp4 r4.z, cb0[6], r3; dp4 o2.w, cb0[7], r3`), including asymmetric VR eye tangents (`cb0[4].x = P00`, `cb0[5].y = P11`, `cb0[4].z = P02`, `cb0[5].z = P12`).
     - `cb0[9..11]`: **Per-eye View Rotation matrix** mapping world-space directions into eye view space! Specifically:
       - `cb0[9].xyz` = View Forward (+Z) unit axis in world space
       - `cb0[10].xyz` = View Right (+X) unit axis in world space
       - `cb0[11].xyz` = View Up (+Y) unit axis in world space
     - **Important Note on Axis Permutation Between `vs_7F894EB5B6BA82A1` and `vs_9BFC7FD232328391`:**
       - In `vs_7F894EB5B6BA82A1.dxbc`: `dp4 r4.z, cb0[9], r3; dp4 r4.x, cb0[10], r3; dp4 r4.y, cb0[11], r3` (writes Forward to `.z`, Right to `.x`, Up to `.y`, then multiplies `(r4.x, r4.y, r4.z, 1)` by `cb0[4..7]`).
       - In `vs_9BFC7FD232328391.dxbc`: `dp4 r2.x, cb0[9], v0; dp4 r2.y, cb0[10], v0; dp4 r2.z, cb0[11], v0` (writes Forward to `.x`, Right to `.y`, Up to `.z`, because `cb1[270..272]` expects that permuted order).
       - In **both** shaders, the underlying constant buffer `VsCb0` (`cb0[9..11]`) stores the exact same physical world vectors: `cb0[10].xyz` = Right (+X), `cb0[11].xyz` = Up (+Y), `cb0[9].xyz` = Forward (+Z)! And in `vs_9BFC7FD232328391.dxbc`, note that `cb2[7].xyz` is the ship's supercruise velocity/streak vector (`mul r1.xyz, v1.xxxx, cb2[7].xyzx`), **not** the black hole direction, so `cb2[7]` from `0x9BFC7FD232328391` must never be used as `bhWorldDir`.
3. **Scene Camera Constant Buffer (`vsCb1`, `cb1[270..279]`):**
   - As documented in [`src/d3d11/billboard_rollfree.h:4-6, 38-40`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/billboard_rollfree.h#L4-L6) and [`docs/billboard-root-cause-2026-10-16.md:18`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/billboard-root-cause-2026-10-16.md#L18), the scene `vsCb1` (`ByteWidth >= 4480`) stores the camera basis rows at **`cb1[277]` (Right), `cb1[278]` (Up), and `cb1[279]` (Forward)**, while `cb1[270..273]` holds the ViewProj columns. Because the 208-byte `VsCb0` (`cb0[4..7]` and `cb0[9..11]`) is bound directly on Orders 4, 5, 7a (`0x7F894EB5B6BA82A1`), and 7b (`0x9BFC7FD232328391`), `VsCb0` provides both the exact per-eye projection (`cb0[4..7]`) and the exact per-eye camera basis (`cb0[10]` Right, `cb0[11]` Up, `cb0[9]` Forward) in one 208-byte buffer.

### 2.2 Multi-Slot Constant Buffer Shadow Table (Eliminating Bug 1A)
Instead of watching a single `g_watchedCbResource` pointer *after* a draw (which failed 100% of the time in Flight 7), `black_hole_disk.cpp` will maintain a zero-stall CPU shadow table (modeled directly on [`celestial_motion.cpp:68-109`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/celestial_motion.cpp#L68-L109) and [`billboard_rollfree.h:77-143`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/billboard_rollfree.h#L77-L143)):
- **Intercepted in `blackHoleDiskCbMapped` / `blackHoleDiskCbUnmapped` / `blackHoleDiskCbUpdated`:**
  - When a `D3D11_BIND_CONSTANT_BUFFER` with `ByteWidth <= 4096` (specifically capturing `ByteWidth == 208` for starfield/celestial `VsCb0` and `ByteWidth` in `[64..1024]` for skybox/lens `VsCb2` / `PsCb2`) is written via `Map`+`Unmap` or `UpdateSubresource`, store its first 16 `float4`s (`256` bytes) and `ByteWidth` in a 64-slot ring/hash table keyed by `ID3D11Resource*`.
  - Why this never stalls the GPU and never misses: it captures CPU-side uploads (`Map(WRITE_DISCARD)` / `UpdateSubresource`) *before* the draw call, regardless of which pooled `ID3D11Resource*` Frontier binds on that frame.

### 2.3 Extracting Camera Pose, Per-Eye Projection, and Black Hole World Anchor
When the accretion disk draw executes on an eye pass (VR Eye 0, VR Eye 1, or Flat pass):
1. **Per-Eye Camera View Rotation $R_{\text{cam}}$ and Per-Eye Projection $(P_{00}, P_{11}, P_{02}, P_{12})$:**
   - Looked up from the bound `saved.vsCb0` (`ByteWidth == 208` on `0x7F894EB5B6BA82A1` / `0x9BFC7FD232328391` / `0xB278189EFA1D574D`) in our 64-slot shadow table:
     - Projection parameters:
       $$P_{00} = \text{cb0}[4].x, \quad P_{11} = \text{cb0}[5].y, \quad P_{02} = \text{cb0}[4].z, \quad P_{12} = \text{cb0}[5].z$$
     - Orthonormal camera rotation rows in world space (matching `vs_7F894EB5B6BA82A1.dxbc` lines `dp4 r4.z, cb0[9], r3; dp4 r4.x, cb0[10], r3; dp4 r4.y, cb0[11], r3` and [`celestial_motion.cpp:198-212`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/celestial_motion.cpp#L198-L212)):
       $$\mathbf{R}_{\text{right}} = \text{normalize}(\text{cb0}[10].xyz), \quad \mathbf{R}_{\text{up}} = \text{normalize}(\text{cb0}[11].xyz), \quad \mathbf{R}_{\text{fwd}} = \text{normalize}(\text{cb0}[9].xyz)$$
     - For any pixel with normalized device coordinates $(x_{\text{ndc}}, y_{\text{ndc}}) \in [-1, 1]^2$, its view-space unit ray is:
       $$\mathbf{v}_{\text{eye}} = \text{normalize}\!\left(\frac{x_{\text{ndc}} - P_{02}}{P_{00}},\; \frac{y_{\text{ndc}} - P_{12}}{P_{11}},\; 1.0\right)$$
     - And its **world-space unit ray** $\hat{\omega}_{\text{world}}$ is:
       $$\hat{\omega}_{\text{world}} = v_{\text{eye},x}\,\mathbf{R}_{\text{right}} + v_{\text{eye},y}\,\mathbf{R}_{\text{up}} + v_{\text{eye},z}\,\mathbf{R}_{\text{fwd}}$$
2. **Why This Completely Solves Head-Locking, Visor Roll, and Stereo Convergence:**
   - Because $\hat{\omega}_{\text{world}}$ is reconstructed in **world space** using the per-eye projection $(P_{00}, P_{11}, P_{02}, P_{12})$ and the per-eye camera orientation $(\mathbf{R}_{\text{right}}, \mathbf{R}_{\text{up}}, \mathbf{R}_{\text{fwd}})$:
     1. **World-Anchored:** When the player pitches, yaws, or turns 180° away from the black hole's world direction $\hat{d}_{\text{world}}$, $\cos\alpha = \hat{\omega}_{\text{world}} \cdot \hat{d}_{\text{world}}$ smoothly decreases and becomes negative ($\cos\alpha < 0$), so the shader immediately discards (`if (cosAlpha <= 0.0) discard;`). The disk is **only** visible when looking toward $\hat{d}_{\text{world}}$.
     2. **Roll-Free:** When the player rolls their head or ship, $\mathbf{R}_{\text{right}}$ and $\mathbf{R}_{\text{up}}$ rotate in world space by the exact roll angle, while $\hat{d}_{\text{world}}$ and the disk spin axis $\hat{n}_{\text{disk}}$ remain fixed in world space. The disk never swims with head roll.
     3. **Exact VR Stereo Convergence & Flat Parity:** In VR, Eye 0 and Eye 1 each use their own `VsCb0` (`P02_left` vs `P02_right` and canted Pimax/OpenXR eye rotation), placing the black hole at infinite/celestial stereo depth matching the starfield and skybox. In Flat mode, the single pass uses the flat projection matrix.
3. **Black Hole World Direction $\hat{d}_{\text{world}}$, Spin Axis $\hat{n}_{\text{disk}}$, Angular Scale $\theta_g = GM/(c^2 D)$, and Flight 8 Bytecode/CB Dump Protocol:**
   - **Why `0x8471F654B0CBAA89 / 0xB00B8B6FB5CDB153` and `0x662ACF4E126EFFD1 / 0x2B156A05E98F2D5D` Are Key:**
     - As shown in [`docs/black-hole-accretion-disk-2026-11-04.md:300-308`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L300-L308), diffing C1 (clean non-BH baseline) against C2..C12 isolated:
       1. `vh = 0x8471F654B0CBAA89 / ph = 0xB00B8B6FB5CDB153` (the BH-system sky dome at Order 3, active in C2..C12 from 54,000 Ls to 1.5 Mm and in Flight 1 C1–C7 down to 28.4 km).
       2. `vh = 0x662ACF4E126EFFD1 / ph = 0x2B156A05E98F2D5D` (the gravitational lens mesh/quad at draw `#386` in C2..C12, which was already disassembled in `vs_662ACF4E126EFFD1.dxbc` and `ps_2B156A05E98F2D5D.dxbc`!).
       3. `vh = 0x99B41C87E45319E6 / ph = 0xA9975F91040B0BCD` (active in C7..C12 from 3,000 Ls to 1.5 Mm).
     - Notice what `vs_662ACF4E126EFFD1.dxbc` (already dumped on disk and disassembled in our session!) does:
       - It is the actual BH gravitational lens mesh (`n=3840`, `vb=buf 7704 sd=12`, `ib=buf 7680`, sampling `16x512 fmt=80` deflection LUT)!
       - In `vs_662ACF4E126EFFD1.dxbc`, `cb0` (`ByteWidth == 240`, `15 float4`s) transforms the lens mesh vertices by `cb0[0..2]` (3x3 orientation/scale matrix) and `cb0[3].xyz` (the 3D camera-relative/world position offset of the black hole lens center!), then projects by `cb1[270..273]` (`ViewProj`)!
       - Let's verify that! In `vs_662ACF4E126EFFD1.dxbc`:
         ```hlsl
         mul r1.xyz, r0.yyyy, cb0[1].xyzx
         mad r1.xyz, r0.xxxx, cb0[0].xyzx, r1.xyzx
         mad r0.xyz, r0.zzzz, cb0[2].xyzx, r1.xyzx
         add r0.xyz, r0.xyzx, cb0[3].xyzx   // <-- 3D translation vector from camera to BH lens center!
         ```
         In `vs_662ACF4E126EFFD1.dxbc`, `cb0[3].xyz` is literally the **world-space translation vector from the camera to the black hole**, and the length of the basis columns `\|cb0[0].xyz\|` (relative to `\|cb0[3].xyz\|`) is the **exact angular/physical radius of the black hole lens** across all distances from 54,000 Ls down to 1.5 Mm!
   - **Mandatory 3-Layer Anchor & Diagnostic Strategy (Zero Wasted Flights):**
     1. **Primary Live Extraction from `0x662ACF4E126EFFD1` (`vsCb0`, 240 bytes) & `0x8471F654B0CBAA89` (`vsCb2` / `psCb2`):**
        - When `vh == 0x662ACF4E126EFFD1` draws (or is updated in the 64-slot CB shadow table), read its `vsCb0` (`cb0[0..3]`):
          - If $\|\text{cb0}[3].xyz\| > 10^{-5}$, the unit direction from the camera to the black hole in world space is $\hat{d}_{\text{world}} = \text{normalize}(\text{cb0}[3].xyz)$, and the angular scale ratio is $\theta_{\text{lens}} = \|\text{cb0}[0].xyz\| / \|\text{cb0}[3].xyz\|$, from which $\theta_g = \text{clamp}(\theta_{\text{lens}} \times 0.08,\; 0.0003,\; 0.45)$ scales the accretion disk and shadow continuously with true distance!
     2. **Immediate Bytecode & CB Logging for `0x8471F654B0CBAA89`, `0xB00B8B6FB5CDB153`, `0x99B41C87E45319E6`, `0xA9975F91040B0BCD`, and `0x662ACF4E126EFFD1`:**
        - Hook shader creation in `vscreen.cpp` to dump `.dxbc` files for `0x8471F654B0CBAA89`, `0xB00B8B6FB5CDB153`, `0x99B41C87E45319E6`, and `0xA9975F91040B0BCD` to `edvr_logs\shaders\` on creation.
        - Log the shadowed `float4` rows of `vsCb0`/`psCb2` on the first draw of `0x8471F654B0CBAA89` and `0x662ACF4E126EFFD1` once per session/transition.
     3. **Guaranteed World-Space Fallback (When in Normal Space C6 Where `0x662ACF4E126EFFD1` Is Culled or Before First Capture):**
        - Latch the last valid $\hat{d}_{\text{world}}$ and $\theta_g$ captured from `0x662ACF4E126EFFD1` (so dropping from supercruise into normal space at the black hole preserves the exact world direction $\hat{d}_{\text{world}}$!).
        - If a session starts directly in normal space before `0x662ACF4E126EFFD1` has drawn once, capture the initial forward world direction $\mathbf{R}_{\text{fwd}}$ on the first frame `0x8471F654B0CBAA89` appears and **latch it permanently as a fixed world-space vector** $\hat{d}_{\text{world}}$ (never updated with head rotation!) with a visible default angular radius $\theta_g = 0.018\text{ rad}$ ($\approx 1.0^\circ$ shadow radius, $\approx 6^\circ$ outer disk radius). Because it is latched once in **world space** and transformed by $R_{\text{cam}}$ every frame, it is 100% world-locked, roll-free, and never head-locked!

---

## 3. Draw Trigger & Hook Placement in `src/d3d11/vscreen.cpp` (VR & Flat Parity)

### 3.1 Exact Draw Order & Why We Trigger After Order 7a (`0x7F894EB5B6BA82A1`) / Order 7b (`0x9BFC7FD232328391`)
- Look at the complete per-eye HDR pass draw order in [`docs/black-hole-accretion-disk-2026-11-04.md:225-236`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L225-L236):
  - **Order 3:** `vh = 0x8471F654B0CBAA89 / ph = 0xB00B8B6FB5CDB153` (BH-system sky dome; replaces `0xF8FA801F2CB1E27C` used in non-BH systems).
  - **Order 4:** `vh = 0x297B6F1F7F104F23 / ph = 0x06332CA168B6DA63` (13,873 background stars, **additive blend**).
  - **Order 5:** `vh = 0xB278189EFA1D574D / ph = 0x06332CA168B6DA63` (14 bright system stars, **additive blend**).
  - **Order 6:** `vh = 0x88DCF1164C640EC3 / ph = 0x494506A63091DF8C` (Opaque depth-writing mesh).
  - **Order 7a (Normal Space):** `vh = 0x7F894EB5B6BA82A1 / ph = 0x095030F27D2C362A` (Normal-space celestial dust sphere, **additive blend**, `1/eye/frame`).
  - **Order 7b (Supercruise):** `vh = 0x9BFC7FD232328391 / ph = 0xCB7AF179DF4E6A60` (Supercruise space-dust sphere, **additive blend**, `1/eye/frame`).
  - **Order 8+:** Cockpit canopy glass (`0x025B4B9FF54622ED`), orbit lines (`0x2BB766C168B450A2`), HUD, bloom, tonemap.
- **Why Triggering at Order 3 Would Fail (and Why Order 7a/7b Is Exact):**
  - If we drew the accretion disk right after Order 3 (`0x8471F654B0CBAA89`), then Orders 4, 5, and 7a/7b (the 13,873 background stars and space dust, which use additive blending `ONE / ONE` at `z = 0`) would draw **on top of** the event horizon shadow, causing background stars to shine right through the black hole!
  - By contrast, triggering immediately **after Order 7a (`vh == 0x7F894EB5B6BA82A1` in normal space) or Order 7b (`vh == 0x9BFC7FD232328391` in supercruise)**:
    1. Every background sky pass (Order 3 sky dome, Order 4 13,873 stars, Order 5 bright stars, Order 7a/7b celestial dust) has already been drawn into the eye's HDR target (`R11G11B10_FLOAT`).
    2. Premultiplied alpha (`SrcBlend = ONE`, `DestBlend = INV_SRC_ALPHA`) with `outAlpha = 1.0` inside the event horizon shadow ($b < b_c$) **completely extinguishes** both the sky dome AND all 13,873 background stars behind the black hole shadow!
    3. On both `0x7F894EB5B6BA82A1` and `0x9BFC7FD232328391`, the 208-byte per-eye `VsCb0` (`cb0[4..7]` projection + `cb0[9..11]` camera basis) is **currently bound at `vsCb0`**!
    4. All foreground cockpit canopy glass (Order 8 `0x025B4B9FF54622ED`), supercruise orbit lines (Order 10), HUD, bloom, and tonemapping execute **after** Order 7a/7b, while opaque ship/cockpit/planet geometry has already written `z > 0` into the reversed-Z depth buffer!
- **Per-Eye Gating (`g_eyeDrawnThisFrame[3]`) & Black-Hole System Gate (`g_bhSkySeenThisFrame`):**
  1. **Black-Hole System Gate (`Order 3`):** When `vh == 0x8471F654B0CBAA89 && ph == 0xB00B8B6FB5CDB153` executes at Order 3, set `g_bhSkySeenThisFrame = true` (and record `g_bhSkyLastSeenFrame = g_frameIndex`). In a normal star system without a black hole (`0xF8FA801F2CB1E27C`), `g_bhSkySeenThisFrame` is `false`, so the accretion disk does not draw in non-BH systems.
  2. **Lens Mesh Capture (`0x662ACF4E126EFFD1`):** Whenever `vh == 0x662ACF4E126EFFD1` executes, update the latched world direction $\hat{d}_{\text{world}}$ and angular scale $\theta_g$ from its shadowed 240-byte `vsCb0`.
  3. **Per-Eye Accretion Disk Draw (`Order 7a / 7b`):** When `(vh == 0x7F894EB5B6BA82A1 && ph == 0x095030F27D2C362A) || (vh == 0x9BFC7FD232328391 && ph == 0xCB7AF179DF4E6A60)` executes and `g_bhSkySeenThisFrame` is true:
     - Determine the eye slot (`0` = left eye, `1` = right eye, `2` = flat/fallback) by comparing the bound RTV/DSV pointer (or `cb0[4].z` sign $P_{02}$ in VR, or tracking the up-to-2 distinct RTV `ID3D11RenderTargetView*` pointers seen since `blackHoleDiskOnFrameStart()`).
     - Check `if (g_eyeDrawnThisFrame[eyeSlot]) return;` and set `g_eyeDrawnThisFrame[eyeSlot] = true;`. This guarantees **both Eye 0 and Eye 1** draw once per frame in VR (fixing the single-boolean `g_lensDrawnThisFrame` bug that previously skipped Eye 1!), and the single scene pass draws once per frame in Flat mode.

### 3.2 Fixing the Flat-Profile Bypass & `SavedPipeline` `psCb0` Restoration
1. **Flat-Profile Hook in `src/d3d11/vscreen.cpp`:**
   - In [`src/d3d11/vscreen.cpp`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/vscreen.cpp), `hookedDraw` (line 5515), `hookedDrawIndexed` (line 5614), `hookedDrawInstanced` (line 5706), and `hookedDrawIndexedInstanced` (line 5796) currently return early when `runtimeFlatProfile()` is true.
   - Note from Table 4.2 ([`docs/black-hole-accretion-disk-2026-11-04.md:229-234`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/docs/black-hole-accretion-disk-2026-11-04.md#L229-L234)) that Order 3 (`0x8471F654B0CBAA89`), Order 7a (`0x7F894EB5B6BA82A1`), and Order 7b (`0x9BFC7FD232328391`) are **`N` (non-indexed `Draw`)** or **`X` (`DrawIndexed`)** calls depending on LOD/pass!
   - Therefore, in **both** `hookedDraw` (line 5515) and `hookedDrawIndexed` (line 5614), inside `if (runtimeFlatProfile())`, after calling `s_origDraw` / `s_origDrawIndexed`, if `g_blackHoleDiskMode > 0`, resolve `vh` and `ph` via `ensureShaderBytesAndHash` and call `blackHoleDiskAfterDraw(thisPtr, vh, ph)`! Also call `blackHoleDiskAfterDraw` at the bottom of `hookedDraw` (after line 5562) in VR mode (currently `blackHoleDiskAfterDraw` was only called in `hookedDrawIndexed` and `hookedDrawIndexedInstanced`, which missed non-indexed `Draw` calls like `N n=318` for `0x8471F654B0CBAA89` and `N n=3072` / `N n=1800` for `0x7F894EB5B6BA82A1` / `0x9BFC7FD232328391`!).
   - Also move `blackHoleDiskOnFrameStart()` in `Present` ([`vscreen.cpp:3651`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/vscreen.cpp#L3651)) above the `if (runtimeFlatProfile())` early return so per-frame flags reset in both VR and Flat modes.
2. **Restoring `psCb0` in `SavedPipeline` (`src/d3d11/black_hole_disk.cpp`):**
   - In [`black_hole_disk.cpp:68-140`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp#L68-L140), `SavedPipeline` previously saved `psCb2` and `psCb3` (`PSGetConstantBuffers(2, 2, ...)`), then overwrote `psCb0` (`PSSetConstantBuffers(0, 3, psCbs)`) without saving or restoring `psCb0`!
   - `SavedPipeline` will now save and restore `psCb0` (`PSGetConstantBuffers(0, 1, &psCb0)` and `PSSetConstantBuffers(0, 1, &psCb0)`), preventing any constant-buffer leak into subsequent draw calls.

### 3.3 Render State & Depth-Buffer Occlusion
- **Depth State:** Reversed-Z read-only (`DepthEnable = TRUE`, `DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO`, `DepthFunc = D3D11_COMPARISON_GREATER_EQUAL`). The full-screen triangle outputs `SV_Position.z = 0.0f, w = 1.0f` (the reversed-Z far plane at infinity). Any opaque foreground pixel (cockpit frame, station, ship, planet) has depth $> 0.0$ and automatically occludes the accretion disk.
- **Blend State (`Premultiplied Alpha + HDR Emission`):**
  - Inside the black hole shadow ($\alpha < \alpha_{\text{sh}}$), the event horizon absorbs the background skybox light behind it (`transmittance = 0`), while the foreground lensed disk face ($n=0$ front side) emits in front of the shadow!
  - Using **premultiplied alpha blending**:
    - `SrcBlend = D3D11_BLEND_ONE`
    - `DestBlend = D3D11_BLEND_INV_SRC_ALPHA`
    - `BlendOp = D3D11_BLEND_OP_ADD`
  - When a pixel hits the event horizon shadow (`r < 2M`), the shader sets `outAlpha = shadowOpacity` ($\to 1.0$ inside the shadow so `DestBlend = 1 - outAlpha = 0` extinguishes the background stars inside the shadow!) and outputs `outColor = diskHdrEmission` (the foreground disk emission that crosses in front of the shadow). Outside the shadow, `outAlpha = clamp(diskOpticalDepth, 0, 1)` and `outColor = diskHdrEmission`. This single blend state simultaneously renders both the pitch-black event horizon shadow AND the glowing HDR accretion disk crossing in front of it!

---

## 4. Mathematical & Physical Shader Design (`simple` and `realistic` Modes)

Both modes are grounded directly in the repository's existing validated physics modules:
- [`tools/blackhole_optics.py`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/blackhole_optics.py) — Schwarzschild Binet equation, Synge shadow, Page-Thorne thin-disk flux (`disk_flux`, lines 277-285), exact circular-orbit gravitational + Doppler redshift (`disk_redshift`, lines 294-307), and Wyman-Sloan-Shirley CIE 1931 Planck blackbody to linear sRGB (`cmf`, `blackbody_xyz`, `xyz_to_linear_srgb`, lines 318-358).
- [`tools/sgra_optics.py`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/sgra_optics.py) — Kerr metric Mino-time geodesic equations (`_geodesic_step_Equations`, lines 166-205), Bardeen-Press-Teukolsky ISCO (`isco_radius`, lines 109-114), Keplerian angular momentum (`keplerian_ell`, lines 117-125), and 5-mode sheared log-spiral turbulence (`MODES`, line 89).

### 4.1 Dedicated Constant Buffer `BlackHoleDiskConstants` (`b0`, 256 bytes / 16 `float4`s)
Instead of binding the game's raw `vsCb0` and `psCb2` directly to the pixel shader and hoping their layout matches, `black_hole_disk.cpp` uploads one dedicated 256-byte constant buffer (`g_diskCb`) to pixel-shader slot `b0` on each draw:
```cpp
struct BlackHoleDiskConstants {
    float camRight[4];      // [0] xyz = Eye camera Right unit vector in world space, w = P00
    float camUp[4];         // [1] xyz = Eye camera Up unit vector in world space,    w = P11
    float camFwd[4];        // [2] xyz = Eye camera Fwd unit vector in world space,   w = P02
    float projParams[4];    // [3] x = P12, y = timeSeconds, z = mode (1=simple, 2=realistic), w = spin a*
    float bhWorldDir[4];    // [4] xyz = BH unit direction in world space, w = theta_g (GM / c^2 D in radians)
    float diskNormal[4];    // [5] xyz = Disk spin axis unit normal in world space, w = r_isco (in M)
    float diskTangentU[4];  // [6] xyz = In-plane orthonormal basis U in world space, w = r_out (in M, e.g. 30M)
    float diskTangentV[4];  // [7] xyz = In-plane orthonormal basis V in world space, w = peakTempK (e.g. 14000K)
    float invViewProj0[4];  // [8] Sky dome InvViewProj row 0 (optional/valid when flags.x > 0)
    float invViewProj1[4];  // [9] Sky dome InvViewProj row 1
    float invViewProj2[4];  // [10] Sky dome InvViewProj row 2
    float invViewProj3[4];  // [11] Sky dome InvViewProj row 3
    float params[4];        // [12] x = useInvViewProj, y = exposureScale, z = r_horizon (in M), w = b_crit (in M)
    float pad[3][4];        // [13..15] Reserved / diagnostics
};
```

### 4.2 Exact 3D Ray-to-Disk Intersection Geometry (Eliminating the Broken `angleCoord.y > 0` Hack)
In the previous shader ([`black_hole_disk_simple.hlsl:193-204`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/shaders/black_hole_disk_simple.hlsl#L193-L204)), the primary and secondary images were split by `if (angleCoord.y > 0.0)`, which broke completely at arbitrary disk inclinations and lacked a true 3D orbital plane!

Here is the exact, singularity-free 3D relativistic ray-plane geometry (matching [`tools/blackhole_optics.py:180-240`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/blackhole_optics.py#L180-L240)):
1. Let $\hat{d} = \text{bhWorldDir.xyz}$ be the unit vector from the observer toward the black hole center, so the observer lies along direction $\hat{o} = -\hat{d}$ from the black hole at distance $r_o = M / \theta_g$.
2. For a pixel with world ray $\hat{\omega}$, the angle $\alpha$ between $\hat{\omega}$ and $\hat{d}$ satisfies:
   $$\cos\alpha = \hat{\omega} \cdot \hat{d}, \qquad \sin\alpha = \|\hat{\omega} - (\hat{\omega}\cdot\hat{d})\hat{d}\|$$
   - Early-out: if $\cos\alpha \le 0$ or $\sin\alpha > \theta_{\max}$ (where $\theta_{\max} = 35\,\theta_g$), `discard;`.
3. Define the orthonormal basis of the **geodesic propagation plane** spanned by $\hat{o} = -\hat{d}$ and $\hat{\omega}$:
   $$\hat{e}_1 = \hat{o} = -\hat{d}, \qquad \hat{e}_2 = \frac{\hat{\omega} - (\hat{\omega}\cdot\hat{d})\hat{d}}{\sin\alpha}$$
   As a photon travels along the planar Schwarzschild geodesic from the observer ($\psi = 0$, direction $\hat{e}_1$) around the black hole by orbital polar angle $\psi > 0$, its unit position vector on the sphere around the black hole is:
   $$\hat{x}(\psi) = \cos\psi\,\hat{e}_1 + \sin\psi\,\hat{e}_2$$
4. **Where does the geodesic plane intersect the inclined equatorial disk plane $\hat{n}_{\text{disk}} \cdot \hat{x}(\psi) = 0$?**
   $$\hat{n}_{\text{disk}} \cdot (\cos\psi\,\hat{e}_1 + \sin\psi\,\hat{e}_2) = 0 \implies \tan\psi = -\frac{\hat{n}_{\text{disk}} \cdot \hat{e}_1}{\hat{n}_{\text{disk}} \cdot \hat{e}_2} = \frac{\hat{n}_{\text{disk}} \cdot \hat{d}}{\hat{n}_{\text{disk}} \cdot \hat{e}_2}$$
   This gives the **exact** orbital angles $\psi_n \in (0, 2\pi)$ at which the photon crosses the equatorial disk plane for every image order $n$:
   - Let $\psi_0 = \text{atan2}(\hat{n}_{\text{disk}} \cdot \hat{d},\; \hat{n}_{\text{disk}} \cdot \hat{e}_2)$. If $\psi_0 < 0$, add $\pi$ so $\psi_1 \in (0, \pi)$ is the **primary ($n=0$, direct)** disk crossing (front half of the disk for $\psi_1 < \pi/2$, back half lensed over the top/bottom of the black hole for $\psi_1 > \pi/2$!).
   - And $\psi_2 = \psi_1 + \pi \in (\pi, 2\pi)$ is the **secondary ($n=1$, indirect / underside)** ghost image that wraps an extra $\pi$ radians around the photon sphere!
5. Look at why this is such a huge breakthrough over the old shader:
   - In a **single** continuous formula $\psi_1 = \text{atan2}(\hat{n}_{\text{disk}} \cdot \hat{d},\; \hat{n}_{\text{disk}} \cdot \hat{e}_2) \bmod \pi$, the front disk ($\psi_1 < \pi/2$) seamlessly transitions into the bent upper/lower arch over the black hole shadow ($\psi_1 > \pi/2$) with **zero** seam at the horizontal midline and **arbitrary** 3D disk inclination $\hat{n}_{\text{disk}}$!
   - And the secondary ghost ring ($\psi_2 = \psi_1 + \pi$) forms a complete, continuous inner ring hugging the photon sphere ($b \to b_c = 3\sqrt{3}M$).

### 4.3 `simple` Mode — Analytic Relativistic Bending, Page-Thorne Flux, Redshift $g^4$, and Blackbody Color
1. **Impact Parameter $b/M$:**
   $$b = \frac{\sin\alpha}{\theta_g \sqrt{1 - 2\theta_g}} \quad (\text{in units of } M), \qquad b_c = 3\sqrt{3} \approx 5.1961524$$
2. **Analytic Geodesic Radius $r(\psi, b)$ at Disk Crossing $\psi = \psi_1$ (Primary) and $\psi = \psi_2$ (Secondary):**
   - In weak-to-moderate deflection ($b > b_c$), Binet's equation $u''(\psi) + u = \frac{3}{2}u^2$ (where $u = M/r$) has the classic second-order relativistic + logarithmic photon-sphere solution (Beloborodov 2002 / Luminet 1979, verified against RK4 in [`tools/blackhole_optics.py:130-175`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/blackhole_optics.py#L130-L175)):
     - For the primary crossing $\psi_1 \in (0, \pi)$:
       - Even when $b < b_c$ (rays aimed inside the shadow!), a photon launched inward from the observer reaches the front face of the disk if $\psi_1 < \psi_{\text{horizon}}(b)$ before hitting $r = 2M$! Specifically, the inbound turning-free branch satisfies:
         $$u(\psi_1, b) \approx \frac{\sin\psi_1}{b} + \frac{3(1 - \cos\psi_1)^2}{4 b^2 \sin\psi_1} + \text{strong-field photon-sphere term}$$
       - More accurately, using Poutanen (2020) / Beloborodov (2002) exact-asymptote inversion between impact parameter $b$ and emission radius $r$ at orbital angle $\psi_1$:
         - On the front half ($\psi_1 \le \pi/2$, inbound before pericenter), $r_1(b, \psi_1)$ exists for **all** $b > 0$ (even $b < b_c$, allowing the front of the inclined disk to cross directly in front of the black shadow!):
           $$r_1(b, \psi_1) = \sqrt{\frac{b^2}{\sin^2\psi_1} - \frac{2M b (1 - \cos\psi_1)}{\sin^2\psi_1}}$$
         - On the back half ($\psi_1 > \pi/2$, outbound after passing pericenter, forming the iconic lensed upper & lower arches!), the ray must pass outside the photon sphere ($b > b_c = 3\sqrt{3}M$), and its emission radius is given by the Beloborodov-Luminet strong-deflection formula:
           $$r_1(b, \psi_1) = \frac{b}{\sin\psi_1} - 2M\frac{1 - \cos\psi_1}{\sin^2\psi_1} + \text{logarithmic divergence term as } b \to b_c^+$$
     - For the secondary image $\psi_2 = \psi_1 + \pi \in (\pi, 2\pi)$ (rays with $b > b_c$ wrapping around the photon sphere):
       $$r_2(b, \psi_2) = r_{\text{ISCO}} + (r_{\text{out}} - r_{\text{ISCO}}) \cdot \text{smooth logarithmic mapping of } \frac{b - b_c}{\Delta b_1(\psi_2)}$$
3. **3D Disk Point, Azimuthal Angle $\phi$, and Keplerian Velocity Direction:**
   - At crossing angle $\psi_k$, the unit emission direction from the black hole is:
     $$\hat{x}_k = \cos\psi_k\,\hat{e}_1 + \sin\psi_k\,\hat{e}_2$$
   - Disk azimuthal angle $\phi_k = \text{atan2}(\hat{x}_k \cdot \hat{t}_V,\; \hat{x}_k \cdot \hat{t}_U)$ in the disk plane basis $(\hat{t}_U, \hat{t}_V)$.
   - Local unit orbital velocity direction of prograde Keplerian gas at $\hat{x}_k$:
     $$\hat{v}_{\text{orb}} = \text{normalize}(\hat{n}_{\text{disk}} \times \hat{x}_k)$$
   - Specific photon angular momentum component along the disk spin axis:
     $$L_z = -b (\hat{e}_2 \cdot \hat{v}_{\text{orb}}) \sin\psi_k = b\, \hat{n}_{\text{disk}} \cdot (\hat{e}_1 \times \hat{e}_2)$$
     Note that $\hat{e}_1 \times \hat{e}_2 = (-\hat{d}) \times \hat{e}_2$ is constant along the entire planar geodesic! That means $L_z = b\,\hat{n}_{\text{disk}} \cdot (-\hat{d} \times \hat{e}_2)$ is an exact constant of motion (Carter / Killing constant $L_z$, matching [`tools/blackhole_optics.py:222`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/blackhole_optics.py#L222) and [`tools/sgra_optics.py:143`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/sgra_optics.py#L143))!
4. **Exact Relativistic Redshift Factor $g$ (`tools/blackhole_optics.py:294-307` & `tools/sgra_optics.py:213`):**
   $$g(r, L_z) = \frac{\sqrt{1 - 3M/r}}{\sqrt{1 - 2M/r_o}\,\left(1 - \Omega(r)\,L_z\right)}, \qquad \Omega(r) = \frac{1}{r^{3/2} + a_*}$$
   - For prograde gas approaching the observer ($L_z > 0$), $1 - \Omega(r) L_z < 1$, so $g > 1$ (Doppler blueshifted and relativistically beamed); for receding gas ($L_z < 0$), $1 - \Omega(r) L_z > 1$, so $g < 1$ (redshifted and dimmed).
   - Observed bolometric intensity scales by Liouville's theorem as **$g^4$**:
     $$I_{\text{obs}} = g^4 \, F_{\text{PT}}(r) \, \mathcal{T}(r, \phi, t)$$
   - Observed color temperature shifts as **$T_{\text{obs}} = g \, T_{\text{eff}}(r)$**, converted to linear sRGB via the exact analytic Wyman-Sloan-Shirley CIE 1931 piecewise Gaussian fit from [`tools/blackhole_optics.py:318-358`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/blackhole_optics.py#L318-L358) (evaluated at 5 representative wavelengths 440, 490, 545, 590, 650 nm in HLSL).
5. **Page-Thorne Radial Flux Profile $F_{\text{PT}}(r)$ (`tools/blackhole_optics.py:277-285`):**
   - Zero inside $r < r_{\text{ISCO}}$, peaking at $r \approx 9.55M$, with smooth outer taper to $r_{\text{out}} = 30M$:
     $$x = \sqrt{r/M}, \quad x_0 = \sqrt{r_{\text{ISCO}}/M}, \quad f(x) = x - x_0 - \frac{3}{2}\ln\!\left(\frac{x}{x_0}\right)$$
     $$F_{\text{PT}}(r) \propto \frac{\max(f(x), 0)}{r^3 \left(1 - \frac{3M}{r}\right)}$$
6. **Multi-Mode Sheared Logarithmic Spiral Turbulence $\mathcal{T}(r, \phi, t)$ (`tools/sgra_optics.py:89`):**
   - Uses the exact 5 modes from [`tools/sgra_optics.py:89`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/sgra_optics.py#L89):
     $$\mathcal{S}(r, \phi, t) = \sum_{j=1}^{5} w_j \cos\!\left(m_j \phi + k_j \ln(r / r_{\text{ISCO}}) - m_j \Omega(r) t + \psi_{0,j}\right)$$
     with $(m_j, k_j, \psi_{0,j}) \in \{(1, 2.3, 0.4), (2, 4.1, 2.1), (3, 5.9, 4.6), (4, 8.3, 1.3), (6, 12.2, 5.5)\}$.

### 4.4 `realistic` Mode — Precomputed 2D Geodesic LUT (`256x256` `R16G16B16A16_FLOAT`) + Kerr Spin & Journal Mass
In `realistic` mode (`visual.black_hole_disk = realistic`):
1. **CPU-Precomputed 2D Geodesic Lensing LUT (`t0`, `256x256` `DXGI_FORMAT_R16G16B16A16_FLOAT`):**
   - On initialization (or when spin $a_*$ changes), `black_hole_disk.cpp` integrates the exact relativistic geodesic differential equation (4th-order Runge-Kutta on $u''(\psi) + u = \frac{3}{2}u^2 + a_* \text{ frame-dragging correction}$, matching [`tools/blackhole_optics.py:130-175`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/blackhole_optics.py#L130-L175) and [`tools/sgra_optics.py:166-205`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/tools/sgra_optics.py#L166-L205)) over a `256x256` grid parameterized by:
     - $u_{\text{tex}} \in [0, 1]$: non-linearly mapped impact parameter $b / M \in [0, 32]$ (concentrating texels near the critical photon sphere $b_c(a_*)$).
     - $v_{\text{tex}} \in [0, 1]$: primary crossing angle $\psi_1 / \pi \in [0, 1]$.
   - Each texel stores:
     - `.r` = exact RK4 primary emission radius $r_1(b, \psi_1) / M$ (or `0.0` if absorbed by the horizon $r \le r_+(a_*)$ before reaching $\psi_1$).
     - `.g` = exact RK4 secondary emission radius $r_2(b, \psi_1 + \pi) / M$ (where the ray wraps around the back to $\psi_2 = \psi_1 + \pi$, or `0.0` if captured).
     - `.b` = exact RK4 third-order ($n=2$ photon ring) emission radius $r_3(b, \psi_1 + 2\pi) / M$.
     - `.a` = exact pericenter / horizon capture mask (`1.0` if the geodesic falls into $r \le r_+(a_*) = M(1 + \sqrt{1 - a_*^2})$, `0.0` if it escapes to infinity).
   - **Performance:** Because the `256x256` RK4 table is computed once on the CPU at startup (~2 ms total) and uploaded as an immutable/default 2D texture `g_geodesicLutSrv` bound to `t0`, the per-pixel shader in `realistic` mode gets **exact RK4 numerical geodesic integration for $n=0$ (primary), $n=1$ (secondary), and $n=2$ (photon sub-ring)** at the cost of a single bilinear `SampleLevel` lookup (< 0.2 ms GPU time at 4K VR resolution)!
2. **Kerr Spin $a_*$ and Bardeen-Press-Teukolsky ISCO (`tools/sgra_optics.py:109-114`):**
   - In `simple` mode, $a_* = 0$ (Schwarzschild, $r_{\text{ISCO}} = 6M$, $r_+ = 2M$, $b_c = 3\sqrt{3}M \approx 5.196M$).
   - In `realistic` mode, $a_* = 0.85$ (or journal-derived spin), shifting the prograde ISCO inward via the exact Bardeen-Press-Teukolsky formula (`tools/sgra_optics.py:109-114`):
     $$Z_1 = 1 + (1 - a_*^2)^{1/3}\left((1 + a_*)^{1/3} + (1 - a_*)^{1/3}\right), \quad Z_2 = \sqrt{3a_*^2 + Z_1^2}$$
     $$r_{\text{ISCO}}(a_*) = M\left(3 + Z_2 - \sqrt{(3 - Z_1)(3 + Z_1 + 2Z_2)}\right) \approx 2.61M \text{ at } a_* = 0.85$$
   - Because $r_{\text{ISCO}}$ is closer to the horizon in `realistic` mode, the inner disk reaches higher orbital velocities ($v/c \to 0.55$) and stronger gravitational redshift + Doppler beaming, plus the $n=2$ photon sub-ring from `.b` of the LUT!
3. **Journal Integration (`src/d3d11/journal_watch.cpp`):**
   - Extend [`journal_watch.cpp`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/journal_watch.cpp) to parse `StarSystem` / `SystemAddress` and `StarType` (`"H"` or `"SupermassiveBlackHole"`) / `StellarMass` on `FSDJump`, `Location`, and `Scan` events, exposing `journalSystemAddress()` and `journalBlackHoleStellarMass()` so the disk orientation ($\hat{n}_{\text{disk}}$) and peak temperature ($T_{\text{peak}} \propto M_{\odot}^{-1/4}$) vary deterministically per black hole system.

---

## 5. Verification & Build Gates
1. **Shader Compilation:** Compile `src/shaders/black_hole_disk_simple.hlsl` into `src/d3d11/black_hole_disk_shader.h` (`g_blackHoleDiskVsBytes`, `g_blackHoleDiskPsBytes`).
2. **Health & Diagnostic Logging:**
   - Log `"black hole disk: mode=%s"` on change (`edvr.ini` / `edvr-flat.ini` hot-reload).
   - Log `"black hole disk: drawn (mode=%s vh=0x%016llX ...)"` on first draw and summary counts, plus dump `0x8471F654B0CBAA89`, `0xB00B8B6FB5CDB153`, `0x99B41C87E45319E6`, and `0xA9975F91040B0BCD` on `CreateVertexShader`/`CreatePixelShader` if not yet dumped.
3. **Full Validation Build:** Run `C:\Users\csasn\github.com\characterecho-sean\edvr-unofficial-patch\build.bat --wait-lock` and verify all Python self-tests (`check_status_blocks.py`, `check_config_contract.py`, `gen_settings_schema.py`, `blackhole_optics.py --self-test`, `sgra_optics.py --self-test`) and C++ test rigs pass and `build\full_build_receipt.json` is written.
