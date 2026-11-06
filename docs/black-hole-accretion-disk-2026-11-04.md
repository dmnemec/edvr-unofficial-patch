# Black hole accretion disks (`visual.black_hole_disk`, 2026-11-04)

## Status

- **State: FIRST FLIGHT CAPTURED (HIP 34707 B, 7 CENSUSES); ISOLATING LENS vs.
  COCKPIT.**
- **Goal:** Add visually striking, scientifically grounded black hole accretion
  disks (lensed upper/lower arches, central event-horizon shadow, photon ring,
  and relativistic Doppler beaming) to both the **VR** and **Flat** profiles.
- **Proposed setting:** `visual.black_hole_disk = off | simple | realistic`
  (`off` the default, stock, runs nothing), live, exposed on the `[visual]`
  section of `edvr.ini` / `edvr-flat.ini`, the installer, the VR Visual menu
  page, and the Flat F8 Visual tab:
  - `off`: stock game (bare gravitational lens, no accretion disk). One relaxed
    atomic load on the draw path; nothing created.
  - `simple`: a predetermined EHT / Gargantua appearance for every black hole,
    evaluated with a fast analytic shader per frame for near-zero GPU overhead.
  - `realistic`: system-entry pre-computation (`StartJump` / `FSDJump` / `Scan`
    plus engine draw constants) that derives mass, spin, companion-star feeding
    versus isolated RIAF state, blackbody temperature, Doppler shift and a
    pre-baked 2D lookup texture (LUT) on system entry so the per-frame shader
    samples accurate relativistic optics without per-pixel geodesic integration.
- **Ruled out:**
  - `ruled out: a black hole reaching the star-glow pass through the solar draw
    list, because two black-hole captures (edvr_gfx_20261022_112019:5 and
    114920:1 targeting a black hole at 2.64 Mm) decode no star but the
    companion O star, and hold no shader pair absent from every other census of
    the three logs: it renders through shared passes
    (docs/star-glow-2026-10-22.md).`
  - `ruled out: supercruise gravitational lensing sampling the post-opaque eye
    HDR snapshot copy (DCC S 0000022904D04CE0 -> 0000022904D04FA0), because in
    C3-C7 (supercruise at HIP 34707 B) zero draws or dispatches bind the
    snapshot clone; only the nearby NPC ship's thruster heat-refraction draw
    (vh=BBAD1CA808E1E292 ph=E54F2A902E5631F6) samples it in normal space C1-C2.`
  - `ruled out: vh=7F894EB5B6BA82A1 ph=095030F27D2C362A (n=3072) and
    vh=9BFC7FD232328391 ph=CB7AF179DF4E6A60 (n=1800) being frustum-culled
    forward-only black hole meshes, because both fire 1/eye/frame across both
    facing (C1, C4, C5) and 180-degree-away (C2, C3, C6, C7) captures.`
- **Open hypotheses:**
  - **H1 (full-sphere or post-FX lens):** Because every non-indexed sky/HDR
    draw
    in supercruise (`8471F654`/`B00B8B6F`, `9BFC7FD2`/`CB7AF179`,
    `C7FA0C0F`/`8AB5A6B2`, `359BF8FF`/`92FF8499`, `2D78DC3F`/`D0A16B9E`, and
    compute `5998146D`/`EB0245DE`) runs 1/eye/frame regardless of camera heading,
    the black hole's lens is either evaluated inside an all-sky/fullscreen pass
    using a directional vector in its constant buffer, or is one of the
    frustum-culled Group A draws (`76ED1E4F`/`7ECF7C83`, `88DCF116`/`494506A6`)
    that dropped to zero in C7.
  - **H2 (constant-buffer direction/pose):** Dumping the constant buffers (`c=`
    and `cb=`) and shader bytecode for those exact candidates (or running
    `census_skip` in any non-black-hole system) will immediately separate
    cockpit/ship draws from the black hole lens and reveal the body-direction
    vector.
- **Next step:** No black-hole flight needed to prune the list — we can check
  which of these hashes appear in any ordinary non-black-hole scene or dump
  their constant buffers / shader disassembly on the next session.

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
