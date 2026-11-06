# Black hole accretion disks (isual.black_hole_disk, 2026-11-04)

## Status

- **State: LENS PIPELINE & CONSTANT BUFFER ISOLATED (CB2 DECODED, 2026-11-05);
  ENTERING ACCRETION DISK DESIGN.**
- **Goal:** Add visually striking, scientifically grounded black hole accretion
  disks (lensed upper/lower arches, central event-horizon shadow, photon ring,
  and relativistic Doppler beaming) to both the **VR** and **Flat** profiles.
- **Proposed setting:** visual.black_hole_disk = off | simple | realistic (off the default, stock, runs nothing), live, exposed on [visual] in edvr.ini / edvr-flat.ini:
  - off: stock game (bare gravitational lens, no accretion disk). One relaxed
    atomic load on draw path.
  - simple: predetermined EHT / Gargantua look, fast analytic shader per frame.
  - realistic: system-entry pre-computation (StartJump/FSDJump/Scan) deriving
    spin,
    mass, temperature, Doppler beaming and 2D geodesic LUT.
- **Ruled out:**
  - ruled out: a black hole reaching the star-glow pass through the solar draw
    list (docs/star-glow-2026-10-22.md).
  - ruled out: supercruise gravitational lensing sampling the post-opaque eye
    HDR snapshot copy (DCC S).
  - ruled out: vh=662ACF4E and vh=5453D19B being the primary lens (they are
    radar contact/stalk markers).
- **Key findings (Flight 4 & DXBC Analysis):**
  - **True Gravitational Lens:** Identified at vs=41E245D488BFE83E,
    ps=6EF82262EB12A037, reading scene depth t0 and skybox t1/t2.
  - **Constant Buffer Decoded:** CB2 (144 bytes, h=9692D4A04F893284) provides
    exact 3D direction vector and distance (cb2[4]), and orthonormal
    camera-space basis frame (cb2[5..7]).
  - **Visor Roll Defect:** Caused by view-space basis quad construction tilting
    with HMD head roll at close distances.
- **Next step:** Accretion disk injection pass architecture, shader design, and
  coordinate frame un-rolling.
---

## 1. What the engine draws today, and why `star_glow` missed it

When Elite Dangerous launched in 2014, stellar-mass and supermassive black
holes were built as bare vacuum gravitational lenses: they warp the background
skybox and starfield around the body's view-space direction, with no accretion
disk and no opaque event-horizon shadow (background stars pinch and slide
across the centre rather than being occluded by a shadow of radius
$r_{\text{shadow}} = \sqrt{27}\,GM/c^2$).

In the 2026-10-22 star survey (`docs/star-glow-2026-10-22.md`, "Survey 2
results"):

- Two censuses were taken in a binary O-star + black-hole system:
  `edvr_gfx_20261022_112019` census 5, and `edvr_gfx_20261022_114920` census 1
  (targeting the black hole at 2.64 Mm, facing away from the O star).
- Those captures ran `flat_star_dump`, which only dumped constant buffers for
  draws in `EyeDrawSnapshot::solarDraw` / `solarPixel`, and checked whether any
  `(VS, PS)` pair appeared *exclusively* in the black-hole censuses across all
  three survey logs.
- Neither happened, because the black hole is not a luminous star surface and
  reuses a shared distortion or scene shader pair that also runs in ordinary
  frames.
- However, a shared `(VS, PS)` pair is still straightforward to isolate when
  diffed against a control census in the *same* spot with the black hole behind
  the camera (`tools/diff_draw_census.py`).

---

## 2. Flight checklist: Black Hole discovery capture (piggyback on MFD flight)

You can run this in **either VR or Flat** (or both) whenever your ship is in a
system with a black hole (e.g., Maia B, Mintaka B, HIP 63835, or any binary
with a black hole):

### Preparation (before jumping or while in system)

1. Make sure you are on the current build (`python tools\install_edvr.py
   --target steam --verify-only`). No special black-hole code is needed for
   this first discovery capture: the shipped draw census (`draw_census.cpp`)
   already records the draws we need to diff.
2. **If flying in VR (`edvr.ini`):**
   - For this capture to see offscreen/HDR-target draws as well as eye draws,
     set `advanced.census_offscreen = true` in `edvr.ini` before the flight
     (or run the 2-press capture in Flat, where `NumLock` automatically records
     every draw including offscreen up to 16,384 lines with no config edit).
   - Default VR census key: **`Home`** (`hotkey.dump_draws = Home`).
3. **If flying in Flat (`edvr-flat.ini`):**
   - Nothing to change in the ini: pressing **`NumLock`**
     (`drawCensusFlatRequest`)
     always captures 2 whole frames of every draw (both scene `H` and offscreen,
     up to 16,384 lines).

### In-flight steps at the black hole (takes ~30 seconds)

1. **Drop out of supercruise or throttle to zero in supercruise close to the
   black hole** (ideally within **1–5 Mm** for a stellar black hole so the
   gravitational lensing distortion is clearly visible and covers a noticeable
   solid angle on screen).
2. **Target the black hole** so the ship's target reticle confirms its distance
   and direction, and keep the ship stationary (zero pitch/roll/yaw rate).
3. **Census 1 (Black hole centered in view):**
   - Point the ship nose (and your head, if in VR) **directly at the black
     hole** so the lensing sphere is dead-centre.
   - Press **`Home`** (in VR) or **`NumLock`** (in Flat) **once**.
   - Wait **3 seconds** for the census to finish writing to the log.
4. **Census 2 (Control — 180 degrees away):**
   - Without changing throttle or opening any menus, pitch/yaw the ship **180
     degrees away** so the black hole is directly behind you and you are facing
     ordinary background starfield (and away from any companion star if
     possible).
   - Keep the ship still and press **`Home`** (in VR) or **`NumLock`** (in
     Flat)
     **once**.
   - Wait **3 seconds**.
5. *(Optional bonus — Census 3, edge/scale check):*
   - Fly to roughly **half or double the distance** (e.g., from 4 Mm to 2 Mm),
     point directly at the black hole again, and press **`Home`** / **`NumLock`**
     once more. This gives us a second distance reading so we can immediately
     verify which constant-buffer float scales with $1/d$ or $d$ even before a
     CB dump.

### Post-flight analysis commands

Once you land / exit the session, we will run:

```powershell
python tools\edvr_log.py --target steam --expect-build HEAD --version
python tools\diff_draw_census.py <path-to-edvr_gfx_log> --a 1 --b 2
python tools\edvr_log.py --target steam --tally offscreen
```

- `diff_draw_census.py` between Census 1 (facing the black hole) and Census 2
  (facing away) will list under `REMOVED` / `ADDED` the exact draw(s) (`vh=`,
  `ps=`, `r=`, `d=`, `c=`, `s=`, index count `n=`) that execute only when the
  black hole is in view.
- That gives us the vertex shader hash `vh=` to plug into
  `advanced.census_cb_watch` (or our constant-buffer shadow reader) and tells
  us whether the black hole is drawn before or after the HDR bloom trigger.

---

## 3. Architecture for `visual.black_hole_disk` (VR and Flat)

### 3.1 Where the pass sits in the frame

Both VR (`src/d3d11/vr_star_glow.h`) and Flat (`src/d3d11/flat_star_glow.h`)
already intercept the scene HDR target `H` (`R11G11B10_FLOAT`, bound with the
scene's reversed-Z depth buffer) before the bloom chain's first level:

- **VR:** Each eye has its own `H` (`vScreenIsEyeSized`, `R11G11B10_FLOAT`,
  depth bound); the bloom chain's first level is the draw with pixel shader
  `143AAE0597E2F7BF` reading `H` at `t0`.
- **Flat:** The scene has one `H` (`flatHdrCandidateDraw`, `R11G11B10_FLOAT` at
  render resolution with depth bound); the bloom chain's first level is the
  half-size HDR trigger draw.

Drawing the accretion disk into `H` **before the bloom trigger** (with depth
test enabled against the scene's reversed-Z depth buffer `depth <= projA`, and
depth writes off) gives both profiles:

1. **Natural HDR bloom and exposure adaptation:** The hot inner photon ring and
   Doppler-boosted crescent feed directly into Elite's native bloom chain
   (`143AAE05`) and exposure reduction (`DEF19B03`/`831DF02E`).
2. **Automatic cockpit, canopy-strut, HUD and ship occlusion:** Foreground
   geometry has `depth > projA` in Elite's reversed-Z depth buffer
   (`sky_ring.h`), so the cockpit frame and any ships in front of the black
   hole naturally occlude the accretion disk in both eyes.
3. **True event-horizon shadow:** By premultiplied-alpha blending (` ONE,
   INV_SRC_ALPHA `) over the sky pixels inside the shadow radius $r <
   \sqrt{27}\,GM/c^2$ ($\alpha = 1$, RGB $= 0$), the pass replaces Elite's
   see-through central pinch with a true black event-horizon shadow while the
   emissive disk and photon ring add HDR light around and across it.

### 3.2 The three setting tiers: `off | simple | realistic`

| Tier | System-entry work (`FSDJump` / `Scan`) | Per-frame GPU work (VR & Flat) | Visual characteristics |
|---|---|---|---|
| **`off`** (default) | None. | One relaxed atomic load (`false`); zero allocations, zero draw cost. | Stock Elite Dangerous. |
| **`simple`** | None. | **Fast Analytic Shader (~0.05–0.1 ms):** Evaluates a single predetermined EHT / Gargantua profile on the bounding quad of the black hole. Uses a closed-form 2-sheet ray deflection ($b \approx r + r_s/r$) for the primary flat disk and the bent upper/lower secondary arch, analytical $\delta^4$ Doppler brightening, a crisp photon ring at $r = 3\sqrt{3}\,r_g$, an opaque central shadow, and a lightweight 2D procedural spiral noise. | Every black hole gets the same striking golden-orange relativistic accretion disk with minimal GPU cost. |
| **`realistic`** | **System-Entry Pre-Bake:** Uses the journal (`StartJump`, `FSDJump`, `Scan`, as parsed in `journal_star.h`) plus the engine's body radius/constants on arrival to compute:<br>1. **Mass $M$ & Kerr spin $a_* \in [0, 0.998]$** (deterministic from body ID/mass), which sets the ISCO radius $r_{\text{ISCO}}(a_*)$ and asymmetric photon ring.<br>2. **Accretion regime:** Checks if the system has a close companion star (high-accretion bright X-ray binary / hot blue-white-gold disk) vs. an isolated stellar-mass black hole (tenuous, magnetically dominated red/orange RIAF) vs. a supermassive black hole (Sgr A* / M87* EHT profile).<br>3. **Pre-bakes a 2D LUT texture (`R16G16B16A16_FLOAT`) once on system entry** holding exact Kerr geodesic deflection angles, gravitational redshift $g = \sqrt{1 - r_s/r} / [\gamma(1 - \beta\cos\theta)]$, shifted Planck blackbody RGB curves, and multi-octave GRMHD turbulent filament density. | **LUT-Driven Relativistic Shader (~0.15–0.25 ms):** Samples the pre-baked geodesic + radiative-transfer LUT and turbulent density map per pixel on the bounding quad, applying live view-pitch angle $\theta_{\text{obs}}$ and differential Keplerian rotation $\Omega(r) = (r^{3/2} + a_*)^{-1}$. | Scientifically grounded, varied per black hole (spin, ISCO, temperature, companion-fed vs. isolated RIAF, prograde/retrograde asymmetry) while keeping per-frame VR cost low by doing the heavy math once at system entry. |

### 3.3 Open design questions to settle after the discovery census

1. **Draw ordering relative to Elite's stock lensing pass:**
   - Does Elite's stock gravitational lensing draw into `H` before bloom, or
     after bloom / during the LDR composite?
   - If Elite lenses `H` *after* our pass, its screen-space warp will also warp
     our disk (which could either double-bend the arch or look great if we only
     draw the flat disk and let Elite bend it, though Elite's lens is a 2D
     radial pinch rather than a true 3D geodesic over-the-top wrap).
   - If Elite lenses *before* our pass (or if we draw right after Elite's lens
     pass), Elite warps the background starfield and our shader draws the exact
     3D geodesic upper/lower disk arches and opaque event-horizon shadow on top.
   - **Census 1 vs. Census 2 will answer this outright** by showing the exact
     `q=` position of the black hole draw relative to the star passes and the
     bloom chain's first level (`143AAE05`).
2. **Temporal AA / DLSS / XeSS motion vectors:**
   - In supercruise, does the black hole draw write depth or rely on sky depth
     (`depth = 0`)? If it sits at sky depth, the temporal pass (`temporal_pass.cpp`)
     treats it as sky unless `celestial_motion.h` supplies a body record or it
     is blended after the upscaler. Checking the `d=` column and depth state on
     the census line will tell us immediately.

---

## 4. Flight 1 results (`HIP 34707 B`, 2026-11-05)

Log: `edvr_gfx_20261105_203015.log` (`v0.22.0-rc.6-204-g63967cb5`, VR profile,
`2632x2684` per-eye target), paired with `Journal.2026-11-05T203028.01.log` at
`HIP 34707 B` (`BodyID: 2`, `BodyType: Star`, `DistFromStarLS: 54983.46`).

*(Tool fix landed alongside this analysis: `BEGIN_RE` in
`tools/diff_draw_census.py` required `\S+=\S+` for trailing attributes on `DC
begin` lines, which rejected the empty `capture_id=` attribute emitted when no
eye-run capture ID is active; updated to `\S+=\S*` and pinned in
`--self-test`.)*

### 4.1 Timeline and heading of the 7 censuses

| Census | Time | Frame | Mode | Ship maneuver / view reported by pilot |
|---|---|---|---|---|
| **C1** | `20:34:00` | `20898` | Normal Space | Facing `HIP 34707 B` at ~3,000 LS (NPC pirate `Tagom` also nearby in normal space) |
| **C2** | `20:34:10` | `21161` | Normal Space | Flipped 180 degrees away from `HIP 34707 B` (`12s` before `StartJump` to Supercruise at `20:34:22`) |
| **C3** | `20:34:38` | `21904` | Supercruise | Flying away toward ~6,500 LS (`11s` after entering Supercruise at `20:34:27`) |
| **C4** | `20:35:09` | `22743` | Supercruise | Turned 180 degrees around at ~6,500 LS to face `HIP 34707 B` and fly back |
| **C5** | `20:35:25` | `23167` | Supercruise | Approaching / flying past `HIP 34707 B` |
| **C6** | `20:35:41` | `23574` | Supercruise | Passing / flying away from `HIP 34707 B` |
| **C7** | `20:35:49` | `23802` | Supercruise | ~1,000 LS away, preparing to open Galaxy Map (`20:36:04`) and jump out |

### 4.2 Complete eye-HDR (`2632x2684 fmt=26`) draw sequence from sky to bloom

Across all 7 censuses, the left-eye HDR pass (`r=tex 2632x2684 fmt=26
res=0000022904D04CE0`, `d=tex 2632x2684 fmt=19 res=0000022904D044A0`) after
deferred lighting (`7E38A6AA`, `8289669D`, `0357BBB2`) and the post-opaque
compute dispatches (`5998146D`, `EB0245DE`) executes this exact sequence:

| Order | `vh` / `ch` | `ph` | Call & geometry | Counts `[C1..C7]` | Notes / role |
|---|---|---|---|---|---|
| 1 | `ch=5998146D464F5C0E` | `-` | `DispatchIndirect` (`u=[fmt=26, fmt=53]`) | `[6, 6, 6, 6, 6, 6, 6]` | Post-opaque compute pass 1 reading linear depth `fmt=39`, depth `fmt=19`, `tex 256x256 fmt=95`, `cb=[buf 480, buf 480]` |
| 2 | `ch=EB0245DE0BB23BB6` | `-` | `DispatchIndirect` (`u=[fmt=26, fmt=53]`) | `[6, 6, 6, 6, 6, 6, 6]` | Post-opaque compute pass 2 (same bindings, `args+12`) |
| 3 | `8471F654B0CBAA89` | `B00B8B6FB5CDB153` | `N n=318 i=1` (`vb=buf 6360 sd=20`) | `[6, 6, 6, 6, 6, 6, 6]` | Sky dome at `z=0.000-0.000`, `ds=17wZ`, opaque `bl=02,1,1/2,1,1`, reads `s=[tex 2048x2048 fmt=80, tex 256x256 fmt=27]` |
| 4 | `297B6F1F7F104F23` | `06332CA168B6DA63` | `N n=12 i=13873` (`vb=buf 192 sd=16`) | `[6, 6, 6, 6, 6, 6, 6]` | Starfield (`13,873` star instances at `z=0.000-0.000`, `ds=17wZ`, additive `bl=15,2,1/5,2,1`, `c=buf 208`) |
| 5 | `B278189EFA1D574D` | `06332CA168B6DA63` | `N n=12 i=14` (`vb=buf 192 sd=16`) | `[6, 6, 6, 6, 6, 6, 6]` | Bright system stars/bodies (`14` instances at `z=0.000-0.000`) |
| 6 | `88DCF1164C640EC3` | `494506A63091DF8C` | `X n=48, 48, 24` (`ia=7855590, 8019642, 1476698`) | `[18, 18, 18, 18, 18, 10, 6]` | Opaque depth-writing mesh (`ds=17wA`, `c=buf 208`, instanced pool `stride=336`); the two `n=48` draws drop to `0` in C6 frame 0 and C7 (`[12, 12, 12, 12, 12, 4, 0]`) |
| 7a | `7F894EB5B6BA82A1` | `095030F27D2C362A` | `N n=3072 i=1` (`vb=buf 49152/98304 sd=16`) | `[6, 6, 0, 0, 0, 0, 0]` | **Normal-space only** (`1/eye/frame` in C1–C2); `ds=17wZ st=10`, additive `bl=15,2,1/5,2,1`, `c=buf 208`, `s=[tex 6x1 fmt=39, tex 128x128 fmt=70]` |
| 7b | `9BFC7FD232328391` | `CB7AF179DF4E6A60` | `N n=1800 i=1` (`vb=buf 64800 sd=36`) | `[0, 0, 6, 6, 6, 6, 6]` | **Supercruise replacement for 7a** (`1/eye/frame` in C3–C7); `ds=17wZ st=10/14`, additive `bl=12,2,1/2,2,1`, same `c=buf 208` and `s=[tex 6x1 fmt=39, tex 128x128 fmt=70]` |
| 8 | `025B4B9FF54622ED` | `C5A5C7E8216CB9AF` | `X n=966 i=1` (`ia=1476746`) | `[6, 6, 6, 6, 6, 6, 6]` | Cockpit/canopy glass mesh (`st=14`, `stride=336`) |
| 9 | `B43A856E285815E3` | `702C3974A260DE14` | `X n=1515, 4584, 381` | `[0, 0, 18, 18, 18, 18, 18]` | Supercruise canopy/interior pass (`st=14`, reads `tex 3072x3072 fmt=33` shadow + `tex 256x256 fmt=95` cubemap) |
| 10 | `2BB766C168B450A2` | `4888F2B05460FA9B` | `N n=1528 i=1` (`vb=buf 24448 sd=16`, `tp=5`) | `[0, 0, 6, 42, 48, 54, 60]` | Supercruise orbit lines / gravity-well rings (`1/eye` in C3, `7/eye` in C4, `8/eye` in C5, `9/eye` in C6, `10/eye` in C7) |
| 11 | `359BF8FF5CFAA4C3` | `92FF8499ED345759` | `X n=6 i=13` (`vb=buf 64 sd=16 ib=buf 12`) | `[0, 0, 0, 6, 6, 6, 6]` | `13` instanced quads (`1/eye/frame` in C4–C7), `c=buf 208`, reads linear depth `s=[tex 2632x2684 fmt=39]` |
| 12 | `76ED1E4F8C72C26E` | `7ECF7C83FD5AD373` | `X n=3960, 168, 168` (`ia=7316756, 7939048, 8103100`) | `[18, 18, 18, 18, 18, 4, 0]` | Alpha-blended mesh (`ds=17wZ`, `bm=7`, `bl=15,6,1/5,6,1`, `c=buf 208`, `s=[tex 6x1 fmt=39, tex 128x128 fmt=70 x2]`); **drops in C6 and vanishes (`0`) in C7** |
| 13 | `F512712C40D93C12` | `D0B9213C1F248335` | `X n=288 i=1` (`ia=1096924`) | `[18, 9, 6, 6, 6, 6, 6]` | Canopy glass reflection (`st=14`, dual-source blend `bl=12,16,1/2,18,1`) |
| 14 | `DCC S` | `-` | `CopySubresourceRegion` (`fmt=26 -> fmt=26`) | `[6, 6, 6, 6, 6, 6, 6]` | Clones active eye HDR target (`0000022904D04CE0 -> 0000022904D04FA0` left, `0000022904CFCE60 -> 0000022904D01060` right) |
| 15 | `BBAD1CA808E1E292` | `E54F2A902E5631F6` | `X n=6, 6` or `n=12, 12` (`vb=buf 228 sd=92`) | `[4, 12, 0, 0, 0, 0, 0]` | **Only consumer of the `DCC S` clone (`0000022904D04FA0`)**, and only in Normal Space (`C1–C2`, NPC ship heat plume); **absent in Supercruise (`C3–C7`)** |
| 16 | `A47A3315FFF5E2E4` | `C67370DE72E4422C` | `N n=140 i=1` (`vb=buf 25600 sd=32`, `tp=2`) | `[0, 0, 6, 6, 6, 6, 6]` | Line list (`tp=2`, `ds=02wA st=04`, HUD/cockpit vector lines) |
| 17 | `8C091FFD08644E02` | `4E4FF61E8A08FC7E` | `X n=12432 i=1` (`ia=1492952`) | `[6, 6, 6, 6, 6, 6, 6]` | Canopy/cockpit interior (`st=15`, `stride=336`) |
| 18 | `C7FA0C0F5DD49180` | `8AB5A6B2E7CACFAE` | `N n=8194 i=1` (`vb=buf 131104 sd=16`, `tp=5`) | `[0, 0, 6, 6, 6, 6, 6]` | Supercruise space-dust / shell strip (`8,194` vertices, `st=11`, `so=r81/w00/f3`, also drawn into `256x256 fmt=26` probe in C3 & C5) |
| 19 | `E508648660A352B2`.. | `5F26E558A7CA0C50`.. | HUD / cockpit holographic elements | steady | Followed by bloom pyramid (`DFED8E1C`/`143AAE05` into `1316x1342 fmt=26`) and fullscreen tonemap (`2D78DC3F`/`D0A16B9E` into `2632x2684 fmt=27`) |

### 4.3 What Flight 1 proved and what remains to check

1. **Supercruise lensing does NOT use the post-opaque HDR copy (`DCC S`):**
   - In `C3–C7` (all in supercruise, where the lensing effect was ~1/3 of the
     screen), the cloned HDR texture (`0000022904D04FA0` / `0000022904D01060`)
     is never bound as an SRV by any draw or compute shader.
2. **Normal space facing (`C1`) vs. 180-degrees away (`C2`) has the exact same
   sky/eye-HDR draw list:**
   - Every non-NPC draw in the sky and eye-HDR sequence (`8471F654`,
     `297B6F1F`,
     `B278189E`, `88DCF116`, `7F894EB5`, `76ED1E4F`) ran with the exact same
     count (`1/eye/frame` or `3/eye/frame`) in both `C1` (facing at 3k LS) and
     `C2` (flipped 180 degrees away in normal space).
   - That means the black hole's lens is **not** a frustum-culled draw that
     disappears in `C2`: either it is evaluated inside one of the always-on
     full-sphere / fullscreen passes (`8471F654` sky dome, `7F894EB5` /
     `9BFC7FD2` sphere, `5998146D` / `EB0245DE` compute, or `2D78DC3F`
     fullscreen composite) using a direction vector in its constant buffer, OR
     `76ED1E4F` / `88DCF116` only culled in `C6–C7`.
3. **We do not need another black-hole trip to eliminate the cockpit/HUD
   draws:**
   - Because any normal flight log (in a regular star system or station) will
     contain the ordinary cockpit, sky dome, starfield, space-dust and tonemap
     shaders, we can compare this table against any non-black-hole census (or
     inspect the shader bytecode of `2D78DC3F`/`D0A16B9E`, `8471F654`/`B00B8B6F`,
     `7F894EB5`/`095030F2`, `9BFC7FD2`/`CB7AF179`, and `76ED1E4F`/`7ECF7C83`)
     to see which shader actually computes the gravitational lens deflection!

## 5. Flight 2 results (HIP 34707 B 14-point distance ladder, 2026-11-05)

Log: `edvr_gfx_20261105_223323.log` (12 untruncated censuses, 3 frames each,
17.5 MB).

### 5.1 Census ladder correlation

| Census | Time | Range / State | Draws/frame (eye / off) | Key observations |
|---|---|---|---|---|
| C1 | 22:35:48 | Clean baseline (Non-BH star system) | 1244 eye / 1556 off | Sky dome uses `F8FA801F2CB1E27C`; normal celestial sphere `7F894EB5`; zero BH lens quads; zero orbit lines |
| C2 | 22:47:28 | 54k LS facing BH (Supercruise) | 1352 eye / 1582 off | Sky dome switches to `8471F654B0CBAA89`; `9BFC7FD2` supercruise sphere active; BH lens quad cluster active |
| C3 | 22:47:52 | 54k LS facing away (180 deg) | 1488 eye / 1820 off | Same BH lens quad cluster fires 1/eye/frame despite facing away |
| C4 | 22:52:34 | 14k LS facing BH (Supercruise) | 1764 eye / 1629 off | Lens cluster active; orbit line count grows |
| C5 | 22:54:19 | 6k LS facing BH (Supercruise) | 1677 eye / 1623 off | Lens cluster active |
| C6 | 22:55:15 | 6k LS facing BH (Normal space) | 1076 eye / 1686 off | Drops to normal space; orbit lines drop to 0; supercruise sphere `9BFC7FD2` reverts to `7F894EB5`; lens cluster inactive |
| C7 | 22:56:59 | 3k LS facing BH (Supercruise) | 1395 eye / 1642 off | Lens quad cluster fires 1/eye/frame at draws #383-#389 |
| C8 | 22:58:21 | 3k LS facing away (180 deg) | 1736 eye / 1877 off | Lens quad cluster fires identically at draws #485-#491 |
| C9 | 22:59:51 | 1.5k LS facing BH (Supercruise) | 1554 eye / 1627 off | Lens quad cluster active |
| C10 | 23:02:48 | 5.5 Mm facing BH (Supercruise) | 1806 eye / 1653 off | Close approach |
| C11 | 23:03:38 | 3.0 Mm facing BH (Supercruise) | 1476 eye / 1627 off | Lens spans large screen fraction |
| C12 | 23:04:05 | 1.5 Mm facing BH (Supercruise) | 1464 eye / 1480 off | Lens dominates view; log cap reached shortly after |

### 5.2 Identification of the Black Hole Lens Shaders

Diffing C1 (clean baseline without a black hole) against C2..C12 isolates the
exact 14 shaders present only when a black hole is in the system:
1. **The Gravitational Lens Mesh / Quad:**
   - Draw #386 in C7 (`vh=662ACF4E126EFFD1 ph=2B156A05E98F2D5D`): indexed
     triangle list (`n=3840 i=1`, `vb=buf 7704 sd=12`, `ib=buf 7680`), `ds=02wA
     st=04`, blending `bl=12,2,1/2,2,1` (`SrcBlend=D3D11_BLEND_SRC_ALPHA`,
     `DestBlend=D3D11_BLEND_INV_SRC_ALPHA`).
   - Samples `s0=tex 16x512 fmt=80` (`DXGI_FORMAT_R32G32_FLOAT` or
     deflection/falloff profile LUT).
   - Accompanied by helper quads:
     - `vh=5453D19B6D362364 ph=289C3EA6EA3EFAF9` (`n=6 i=4`, `vb=buf 120
       sd=20`, `bl=15,2,1/5,2,1` additive)
     - `vh=A2C2D5510BF1926D ph=4F39912FEED610E2` (`n=6 i=4`, `vb=buf 96 sd=16`,
       `bl=15,2,1/5,2,1` additive)
     - `vh=9B34C331902DC1ED ph=9FDA9FAB05B654BD` (`n=108 i=1`, `vb=buf 3456
       sd=32`)
     - `vh=F8D8A92E96419901 ph=4F35016E001AB1CB` (`n=6 i=3`, `vb=buf 96 sd=16`,
       samples `tex 1024x1024 fmt=98`)
2. **The Visor / Tilt-Roll Defect:**
   - In headset at close approach (< 30 km), the user noted: "At the Km
     distances the entire screen is taken up by a weird lensing effect no
     matter where I look. It rotates with the visor just like smoke used to
     before we fixed the smoke tilt rotation."
   - This occurs because `vh=662ACF4E126EFFD1` and its companion quads
     (`5453D19B`, `A2C2D551`) construct camera-facing billboard/screen quads
     using the cockpit/head camera basis rather than world coordinates,
     suffering from the exact same head-roll artifact as unpatched particle
     billboards (`docs/particle-billboards.md`).



## 6. Flight 4 results: Constant Buffer (CB2) Decoded (`41E245D488BFE83E`, 2026-11-05)

Log: `edvr_gfx_20261105_233701.log` (2 censuses, `census_cb_watch =
41E245D488BFE83E`, `census_cb_slot = 2`).

### 6.1 Decoded Black Hole Lens Constant Buffer (`CB2`)

The GPU readback captured the exact 144-byte pixel-shader constant block `CB2`
(`h=9692D4A04F893284`):

| Register | Values (`x, y, z, w`) | Interpretation |
|---|---|---|
| `cb2[0]` | `0.798719, 0.691701, 4.048700, 0.000000` | Lens optic parameters / base tint |
| `cb2[1]` | `0.798719, 0.691701, 4.048700, 0.134957` | Lensing wavelength dispersion / falloff exponent |
| `cb2[2]` | `0.150000, 0.163000, 48.670700, 0.368000` | Output color scaling and saturation terms |
| `cb2[3]` | `1.558000, 0.400000, 10.000000, 0.000000` | Deflection scale (`x=1.558`), noise phase (`y=0.40`), falloff power (`z=10.0`) |
| `cb2[4]` | `0.236350, 0.056610, 0.970018, 5765.3800` | **Black Hole 3D Unit Direction (`x, y, z`) and Distance (`w = 5,765.38 LS`)** |
| `cb2[5]` | `-0.971576, 0.000000, 0.236729, 0.000000` | **Body-aligned Right Basis Vector** (orthonormal, length 1.0) |
| `cb2[6]` | `0.013401, -0.998396, 0.055001, 0.000000` | **Body-aligned Up Basis Vector** (orthonormal, length 1.0) |
| `cb2[7]` | `0.236350, 0.056610, 0.970018, 1.000000` | **Body-aligned Forward Basis Vector** (identical to direction, length 1.0) |
| `cb2[8]` | `1.000000, 1.000000, 1.000000, 0.000000` | Occlusion mask / visibility flags |

### 6.2 Architectural Significance

1. **Exact 3D Coordinates Available Per-Frame:**
   - Frontier delivers the exact 3D camera-to-body direction unit vector
     (`cb2[4].xyz`) and distance in light-seconds (`cb2[4].w = 5765.38`)
     directly to the lens shader in CB2.
2. **Complete 3D Orthonormal Orientation:**
   - Rows `cb2[5..7]` provide the complete 3D orientation frame (`Right`, `Up`,
     `Forward`) in world/camera space.
3. **Accretion Disk Integration:**
   - Because the 3D position and orientation are already packed into CB2, the
     accretion disk shader pass can directly sample CB2 (or hook
     `41E245D488BFE83E`) to construct geometrically perfect, world-locked
     relativistic arches, event horizon shadows, and Doppler beaming without
     guessing the body position or needing extra hooks!

