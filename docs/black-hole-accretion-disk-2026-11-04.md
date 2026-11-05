# Black hole accretion disks (`visual.black_hole_disk`, 2026-11-04)

## Status

- **State: DESIGN AND CENSUS PLAN, NOT BUILT.**
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
- **Open hypotheses:**
  - **H1 (frustum-culled lens draw):** The black hole's gravitational lens is
    drawn by a frustum-culled sphere or billboard draw into the scene target or
    post-bloom distortion chain using a shared `(VS, PS)` pair. Comparing a draw
    census taken facing the black hole against a draw census taken facing 180
    degrees away in the same spot will isolate the exact draw call.
  - **H2 (constant-buffer pose):** Like stars (`0EE43D81`/`4D516EF0`, VS `b0`
    rows 9-11 `.w` for camera-relative centre and `b2[0].w` for radius) and
    planets (`72BDD292`, `celestial_motion.h`), the black hole's lens draw
    carries its camera-relative centre and scale in `VS b0` / `VS b2`.
- **Next flight:** One black-hole discovery capture (can piggyback on the MFD
  test flight): two back-to-back draw censuses near a black hole — Census A
  looking directly at the black hole, Census B looking 180 degrees away at
  empty sky.

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
