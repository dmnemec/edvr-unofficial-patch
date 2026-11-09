# Black Hole Accretion Disk (`visual.black_hole_disk`) — Agent Handoff

## 1. Current State & Branch Summary

- **Branch:** `feat/black-hole-accretion-disk`
- **Feature Config Key:** `visual.black_hole_disk = off | simple | realistic` in `edvr.ini` and `edvr-flat.ini`
- **Investigation & Design Docs in Repo:**
  - `docs/black-hole-accretion-disk-2026-11-04.md` (main arc journal with `## Status` block, census tables, shader disassemblies, and Flight 1–13 logs/root causes)
  - `docs/black_hole_disk-design.md` / `docs/black_hole_disk_design.md` (architectural & mathematical design reviewed with Opus)
  - `docs/black_hole_disk_spec.md` (complete implementation specification)
- **Implementation Files:**
  - `src/d3d11/black_hole_disk.h`
  - `src/d3d11/black_hole_disk.cpp`
  - `src/shaders/black_hole_disk_simple.hlsl`
  - `src/d3d11/black_hole_disk_shader.h` (generated from `src/shaders/black_hole_disk_simple.hlsl` via `fxc.exe` with `/Vn kBlackHoleDiskSimpleVS` and `/Vn kBlackHoleDiskSimplePS`)
  - `src/d3d11/vscreen.cpp` (draw dispatch hooks in both VR and Flat profiles, plus buffer invalidation hooks)
  - `src/d3d11/device_hook.cpp` (`blackHoleDiskBufferInvalidated` on `kDevCreateBuffer`)
  - `src/d3d11/journal_watch.h` / `src/d3d11/journal_watch.cpp` (`journalSystemAddress()` and `journalBlackHoleStellarMass()`)

---

## 2. How the Pipeline Works End-to-End

1. **Black-Hole-System Detection & Anchor Capture (`blackHoleDiskAfterDraw` in `src/d3d11/black_hole_disk.cpp`):**
   - **Order 3 (`kBhSkyDomeVsHash = 0x8471F654B0CBAA89`, `kBhSkyDomePsHash = 0xB00B8B6FB5CDB153`):**
     - Frontier binds this sky dome shader pair *only* in star systems containing a black hole (replacing the normal sky dome `0xF8FA801F2CB1E27C`).
     - Offscreen cubemap probe draws (`vp.Width <= 512.0f`) are filtered out at the top of Order 3 and Order 4/5 so they never pollute `g_eyeRtvs[0..1]`.
     - Reads the 400-byte (`25 float4`) sky dome constant buffer `CB2` via the SEH-guarded targeted watch table (`shadowFor(psCb2)` / `shadowFor(vsCb2)`):
       - `cb2[11..14]`: Sky-space `InvViewProj` matrix (`g_skyInvViewProj[eyeSlot]`).
       - `cb2[18].xyz`: Exact 3D sky-space direction vector to the black hole (`g_latchedBhWorldDir`), which updates live at every distance from 54,000+ Ls in supercruise down to 25 km in normal space.
       - `cb2[18].w`, `cb2[21].x`, `cb2[24].xy`: Frontier's gravitational lensing parameters (`cb2[21].x` is the inner strong-lensing pinch radius `r21` inside ~3,000 Ls; `cb2[18].w * cb2[24].x` is the outer weak-lensing scale in supercruise).
   - **Orders 4 & 5 (`0x297B6F1F7F104F23` & `0xB278189EFA1D574D`, `ps = 0x06332CA168B6DA63`):**
     - Caches the 208-byte `VsCb0` camera basis (`cb0[10]` Right, `cb0[11]` Up, `cb0[9]` Fwd) and per-eye asymmetric projection (`cb0[4].x = P00`, `cb0[5].y = P11`, `cb0[4].z = P02`, `cb0[5].z = P12`).
   - **Trigger Point:**
     - The full-screen accretion disk draw is issued once per eye per frame (`g_eyeDrawnThisFrame[eyeSlot]`) immediately after **Order 5** (`0xB278189EFA1D574D / 0x06332CA168B6DA63`, or fallback Order 7a/7b). Order 5 runs *after* all 13,873 background stars are drawn into the HDR target (so the black hole shadow `outAlpha = 1.0` extinguishes stars behind the event horizon) and *before* cockpit canopy glass, HUD, and orbit lines (which depth-reject or composite over the disk via Reversed-Z read-only `GREATER_EQUAL` at `z = 0.0`).

2. **SEH-Guarded Constant Buffer Watch Table (`src/d3d11/black_hole_disk.cpp`):**
   - Follows the exact pattern of `src/d3d11/celestial_motion.cpp`:
     - `blackHoleDiskCbMapped`, `blackHoleDiskCbUnmapped`, and `blackHoleDiskCbUpdated` only call `findWatch(resource)` (zero `GetType`/`GetDesc`/`addWatch` in `hookedMap`).
     - `blackHoleDiskBufferInvalidated` clears `w->valid = false; w->mapped = nullptr; w->byteWidth = 0;` whenever `CreateBuffer`, `ExecuteCommandList`, `CopyResource`, or `CopySubresourceRegion` reuses or mutates a buffer pointer, preventing stale `byteWidth` reads when Frontier recycles `ID3D11Buffer*` addresses on supercruise drop.
     - All `std::memcpy` calls in `Unmap` and `UpdateSubresource` are wrapped in `guardedBudget(g_budget, ...)`.

3. **3D Relativistic Accretion Disk Shader (`src/shaders/black_hole_disk_simple.hlsl`):**
   - **Fixed 3D World-Space Spin Axis:** `diskNormal`, `diskTangentU`, and `diskTangentV` are computed deterministically from `journalSystemAddress()` (`sysAddr`) in `black_hole_disk.cpp`, completely independent of `d_bh`.
   - **Live 3D Orbiting Perspective & Smooth $C^\infty$ Equator Crossing (Flight 13 fix):**
     - `PSMain` derives `diskUp` and `diskRight` directly from fixed `n_disk` (no `hemiSign` flip) and regularizes the grazing inclination with `cosIncReg = sqrt(cosIncSigned * cosIncSigned + 0.08 * 0.08)` and `byOriented = by * (cosIncSigned / cosIncReg)`, eliminating the 1-frame orientation pop when flying across the equatorial plane.
   - **Unified Beloborodov (2002) / Luminet (1979) Geodesic Plane Bending:**
     - Sheet 1 ($n=0$) uses the smooth closed-form light-bending equation $r_1(b, \psi_1)$ across both the front equatorial bridge ($\cos\psi_1 > 0$, crossing in front of the event-horizon shadow) and the gravitationally bent rear arch ($\cos\psi_1 < 0$, wrapping over the back of the shadow).
     - Sheet 2 ($n=1$) renders the secondary lensed arch wrapping around the opposite rim of the shadow.
     - Sheet 3 (`realistic` mode $n=2$ Kerr Geodesic LUT) samples `v_lut = acos(cosPsi1) / pi` (Flight 13 fix, replacing the buggy `atan2(by, bx)` screen-angle lookup that caused a second tilted ghost disk in Flight 12).
   - **Mode Spectra (`simple` vs. `realistic`):**
     - `simple`: Warm cinematic Gargantua golden-amber-white spectrum ($T_{\text{eff}} \sim 5,500\text{ K}$).
     - `realistic`: Astrophysical stellar-mass Kerr black hole ($a_* = 0.85$, $r_{\text{ISCO}} = 3.8M$, $T_{\text{peak}} \ge 14,000\text{ K}$ scaled by `journalBlackHoleStellarMass()`) with O/B-type Planckian blue-white/cyan core, warm ivory midline, Doppler-redshifted crimson-magenta outer rim, and Bardeen (1973) prograde Kerr shadow shift.

---

## 3. What Was Just Built & Installed (Ready for Flight 15 Verification)

The current working tree on `feat/black-hole-accretion-disk` has passed the full validation build (`build.bat --wait-lock`) and is installed on the Steam target (`D:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64`).

### Changes in Flight 15 Build (Post-Flight 14 Feedback)
1. **Unbroken Shadow & Exact Scale in `realistic` Mode:** Set $r_{\text{ISCO}} = 5.5M$ and $b_{\text{crit}} = 5.196M$ for both modes, ensuring `realistic` mode matches `simple`'s imposing scale and pristine unbroken shadow center while preserving its O/B-type Planckian cyan/violet spectrum and Kerr $n=2$ sub-ring.
2. **Dynamic Supercruise Distance Scaling:** $\theta_g = \text{clamp}(0.0008f + (bw \cdot r24x) \cdot 0.0032f, 0.0008f, 0.065f)$, smoothly expanding the disk from a compact, glowing star-like jewel at 54,000+ Ls into a screen-filling giant as you approach.
3. **Radiant Star-Like Optical Bloom & Glare:** Boosted peak HDR radiance to $\sim 15.0$ and added an optical glare scattering envelope around the inner disk, driving Elite's native bloom pass to create atmospheric glare like bright stars.
4. **Smooth Limb Blending:** Asymptotic decay of gravitational bending at the limbs ($b \gg b_{\text{crit}}$) with wide limb blending, eliminating pinched warping where the arch meets the wings.

---

## 4. Next Steps for the Next Agent

1. **After Flight 15:**
   - Verify the flight log matches the build:
     ```powershell
     python tools\edvr_log.py --target steam --expect-build HEAD --version
     python tools\edvr_log.py --target steam --grep "black hole"
     ```
   - Check user feedback on:
     - `realistic` mode shadow: verify it is 100% pure black and unbroken like `simple` mode, with matched imposing scale.
     - Supercruise approach: verify the black hole starts as a brilliant star-like jewel at 54,000+ Ls and grows continuously as you fly closer.
     - Light bloom: verify the star-like optical bloom envelope around the disk and photon ring.
     - Smooth limbs: verify the seamless transition where the upper arch meets the outer wings.
2. **When Editing the Shader (`src/shaders/black_hole_disk_simple.hlsl`):**
   - Always recompile `src/d3d11/black_hole_disk_shader.h` using `fxc.exe` with `/Vn kBlackHoleDiskSimpleVS` (entry `VSMain`, `/T vs_5_0 /O3`) and `/Vn kBlackHoleDiskSimplePS` (entry `PSMain`, `/T ps_5_0 /O3`) wrapped in `namespace edvr { ... }`, then run the full `build.bat --wait-lock` by absolute path.

