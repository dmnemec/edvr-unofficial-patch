# Black hole accretion disks (`visual.black_hole_disk`, 2026-11-04)

## Status

- **State: FLYING (`visual.black_hole_disk = off | simple | realistic`, Flight 16 Build).**
- **Goal:** Add visually striking, scientifically grounded black hole accretion
  disks (lensed upper/lower arches, central event-horizon shadow, photon ring,
  and relativistic Doppler beaming) to both the **VR** and **Flat** profiles.
- **Ruled out:**
  - `ruled out: 0x41E245D488BFE83E / 0x6EF82262EB12A037 is the black hole lens shader, because disassembly proves it is kV2Atmos planetary atmosphere.`
  - `ruled out: watching a single post-draw g_watchedCbResource pointer on Map, because Frontier cycles pooled constant buffers before the draw.`
  - `ruled out: drawing immediately after Order 3 sky dome, because Orders 4 and 5 draw 13,873 additive background stars over the shadow.`
  - `ruled out: interpolating world rays across the full-screen triangle in VSMain, because linear interpolation distorts directions off unit sphere.`
  - `ruled out: unnormalized PageThorneFlux, because at r=10M it evaluates to 1.6e-5 (10,000x dimmer than photon ring), discarding the disk in Flight 9.`
  - `ruled out: normalizing skyPos.xyz without dividing by homogeneous skyPos.w, which caused the blurry radial streak in Flight 9.`
  - `ruled out: calling GetType/GetDesc and addWatch on every mapped buffer in hookedMap, because buffer pointer recycling caused stale byteWidth reads past 256-byte guard pages (d3d11.dll+0x5BC701 AV in Flight 10).`
  - `ruled out: unforeshortened upper/lower arches spanning r_isco..r_out in all sky directions, which produced a 20M-tall puffy lemon halo in Flight 10 instead of a thin relativistic Einstein arch hugging the shadow.`
  - `ruled out: computing diskNormal relative to d_bh every frame and hardcoding cosInc = 0.20 in PSMain, because it locked the viewing inclination and perspective of the disk as the ship orbited the black hole in Flight 11.`
  - `ruled out: smooth featureless DiskEmissionProfile out to r_out = 30.0 with separate flat Sheet 1 + oval Sheet 2, because it filled the outer region with a featureless yellow haze and compressed the realistic-mode arch into 2 pixels on the photon ring in Flight 11.`
  - `ruled out: indexing GeodesicLut v_lut with raw screen angle atan2(by, bx) instead of 3D geodesic bending angle acos(cosPsi1)/pi, because it drew a second unforeshortened ghost disk at a mismatched angle in realistic mode in Flight 12.`
  - `ruled out: discontinuous hemiSign = (cosIncSigned >= 0) ? 1 : -1 flip on n_eff, because crossing the equatorial plane inverted diskRight/by in 1 frame and caused an orientation pop in Flight 12.`
  - `ruled out: evaluating Beloborodov r_direct across the foreground line of sight (cosPsi1 >= 0) all the way to by = 0, because it produced r >= r_isco inside the shadow and stepped to 0 at by = 0, creating a flat horizontal shelf cutting into the shadow in Flight 13.`
  - `ruled out: clamping g_latchedThetaG to 0.036f (2.0 deg), because Frontier's inner pinch radius at the Exclusion Zone (25 km) is r21 = 0.347 rad (20 deg), making the disk 10x smaller than Frontier's gravitational lens in Flight 13.`
  - `ruled out: setting r_isco = 3.8M < 4.65M = b_crit in realistic mode, because emission inside the shadow radius caused the same inner-circle overlap in Flight 14.`
  - `ruled out: evaluating bw * r24x as dynamic distance in supercruise, because bw is constant ~23.7 on the sky dome, locking theta_g at 0.065 rad until r21 popped it to 0.0009 rad at 0.24 Ls in Flight 15.`
  - `ruled out: limbFalloff = exp(-max(bShadow - b_crit, 0.0) / 3.5), because at the top of the arch b_y >> b_crit decayed r_rear into r_front, ballooning the arch into a tall vertical capsule in Flight 15.`
  - `ruled out: broad hdrGlare diffuse envelope, because it caused a foggy washout across the disk and dimmed the starfield in Flight 15.`
- **Implemented Architecture (Flight 16 Build):**
  - **Monotonic Dynamic Distance Scaling:** $\theta_g = \text{clamp}(\max(r21 \times 0.185f, 0.0015f), 0.0015f, 0.075f)$, smoothly growing from a brilliant $2^\circ$ jewel at 54k Ls to a $90^\circ$ canopy-filling disk at 25 km with zero pop.
  - **Pure Black Central Shadow in Both Modes:** $r_{\text{ISCO}} = 5.5M$ and $b_{\text{crit}} = 5.196M$ in both `simple` and `realistic` modes; foreground intersection $r_{\text{front}} = \sqrt{b_x^2 + (b_y / \cosIncReg)^2}$ guarantees 100% pure black shadow.
  - **Tight Relativistic Upper Arch:** Removed `limbFalloff`; pure Beloborodov $r_{\text{rear}}$ hugs the shadow across the top; smooth Hermite blend `frontBridgeWeight = smoothstep(-0.18, 0.18, cosPsi1)` eliminates limb warping.
  - **Clean Optical Bloom without Fog:** Removed `hdrGlare` fog and starfield opacity offset; razor-thin photon spike ($2.25$) and peak radiance ($1.45$) drive Elite's native bloom filter cleanly.
- **Next Flight:** Flight 16 validation of smooth monotonic approach scaling, tight relativistic arch without vertical blowout, pure black shadow in both modes, and clean starfield clarity.
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



---

## 7. Implementation Architecture: Simple Tier (isual.black_hole_disk = simple, 2026-11-06)

### 7.1 Pipeline Hook & Basis Unrolling (src/d3d11/black_hole_disk.cpp)

1. **Draw Interception:**
   - Intercepts the true gravitational lens draw ( s=0x41E245D488BFE83E,
     ps=0x6EF82262EB12A037) right after Frontier issues it in  screen.cpp.
2. **Visor Roll Decoupling:**
   - Latches Frontier's 144-byte CB2 constant block from the draw.
   - De-rolls the camera-facing basis using world up 0 1 0$ via
ollfree::basis (following
[src/d3d11/billboard_rollfree.h](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/billboard_rollfree.h)),
completely resolving the close-distance visor tilt bug observed in Flight 2.
3. **Accretion Disk Dispatch:**
   - Binds the active eye HDR target (R11G11B10_FLOAT) and read-only scene
     depth (D3D11_DEPTH_WRITE_MASK_ZERO, D3D11_COMPARISON_GREATER_EQUAL for
     reversed-Z canopy occlusion).
   - Draws a full-screen triangle executing kBlackHoleDiskSimpleVS and
     kBlackHoleDiskSimplePS with premultiplied alpha blend (ONE /
     INV_SRC_ALPHA).
   - Safely saves and restores original D3D11 pipeline state.

### 7.2 Analytic Shader Model (src/shaders/black_hole_disk_simple.hlsl)

- **Event Horizon Shadow:** Extends to {\text{shadow}} = \sqrt{27}\,r_g \approx
  5.196\,r_g$.
- **Photon Ring:** Sharp Gaussian peak at  \approx 5.2\,r_g$.
- **Lensed Arches:** Analytic geodesic mapping yielding primary equatorial
  disk, secondary upper arch (rays curving over the top of the event horizon),
  and tertiary lower arch (rays curving beneath).
- **Relativistic Doppler Beaming:** Approaching gas on the left is boosted with
  blue shift ( \propto g^{3.8}$); receding gas on the right is dimmed and
  red-shifted.
- **Canopy Occlusion:** Scene depth $ is sampled to discard any pixels occluded
  by cockpit frames or ship geometry.

## Flight 6 Fix & Verification (2026-11-06)

- **Issues Addressed from Flight 5:**
  1. *Stereo decoupling / 'Two cat eyes staring at me' head-locked artifact:* Replaced naive 2D screen UV ray with true view-space ray reconstructed from the game's actual per-eye projection matrix in cb0[4..7] (x_v = (x_ndc - cb0[4].z) / cb0[4].x, y_v = (y_ndc - cb0[5].z) / cb0[5].y). Bound sCb0 at vertex shader slot 0. Eliminates VR optical center mismatch and restores stereo convergence and world anchoring across both eyes.
  2. *Close-range angular scaling (tiny eyeball size at 27 km):* Converted distance to kilometers ({\text{km}} = D_{\text{LS}} \times 299792.458$). At 27 km, physical gravitational radius  \approx 30\text{ km}$ scales the accretion disk and event horizon shadow to engulf the cockpit canopy (~64 degree angular field of view).
  3. *Persistence across Supercruise:* Replaced fragile sub-pixel falloff with a dedicated luminous beacon / core diffraction flare floor at long distances (out to 5,000+ Ls).
- **Validation:** uild.bat --wait-lock passed all 32 Python gates and test rigs cleanly. Installed to Steam target and verified.

## Flight 7 Fix & Verification (2026-11-06)

- **Issues Addressed from Flight 6 Feedback:**
  1. *Alignment Defect (Disk in extreme upper outer corners of eyes):*
     - Root cause: 
ollfree::basis in lack_hole_disk.cpp treated Frontier's body orientation rows as camera rows, flipping cb2[5] (Right) and cb2[6] (Up) into inverted signs (-X, -Y). Furthermore, unprojected ray dot products diverged rapidly away from the optical axis.
     - Fix: Removed 
ollfree::basis call from lack_hole_disk.cpp. In black_hole_disk_simple.hlsl, dynamically generated the canonical view-space camera-up orthonormal tangent frame from bhDir = normalize(cb2[4].xyz), and projected rays onto the tangent plane via offsetRay = (ray / cosTheta) - bhDir. This guarantees angleCoord == (0, 0) exactly at the black hole center across both VR eyes.
  2. *Distance & Disappearance Defect (Vanish at 3,000 Ls & tiny up close):*
     - Root cause: Overscaled clamp factor blew out the disk at 24 Mm while contracting excessively up close. Additionally, a typo in SavedPipeline::restore (PSGetShaderResources instead of PSSetShaderResources) corrupted downstream pipeline bindings when Frontier culled the distant billboard.
     - Fix: Corrected SavedPipeline::restore to PSSetShaderResources. Updated angular scaling to scale = clamp(120.0 / (distKm * 0.25 + 600.0), 0.00015, 0.020), guaranteeing the disk spans ~30 degrees across the cockpit canopy at 27 km - 24 Mm, while maintaining a luminous 0.25-degree beacon floor throughout Supercruise.
- **Validation:** Full validation build (uild.bat --wait-lock) passed all 32 Python gates and test rigs cleanly. Both DLLs installed and verified on Steam target.

## Flight 7 Results & Verified Root-Cause Analysis (2026-11-06)

Log: `edvr_gfx_20261106_194347.log` (build `6AEE6D36`).

### 8.1 User Observations in Flight 7
1. **180-Degree Inversion / Head-Locking:** The accretion disk appeared only when looking *away* from the black hole, and was absent when looking directly at the black hole center.
2. **Fixed Tiny Dot Size:** Up close (27 km to 24 Mm), the accretion disk appeared as a tiny dot and stayed the exact same dot size as the ship supercruised away.
3. **Vanishing at 3,000 Ls:** The dot disappeared completely once inside/past ~3,000 Ls in Supercruise.

### 8.2 Verified Root Causes (Code, Log, & Disassembly Evidence)
1. **Bug 1A — `g_cb2ShadowValid` Was Permanently `false` (Zero CB2 Captures):**
   - `python tools\edvr_log.py --file "...\edvr_gfx_20261106_194347.log" --grep "black hole"` showed zero occurrences of `"black hole disk: captured cb2 shadow"`.
   - `g_watchedCbResource` was only assigned *after* `isLensDraw` executed, and watched a single `ID3D11Resource*` pointer. Because Frontier cycles constant buffers from a ring pool updated *before* the draw call (`DCW 0 q=918 p cb=@464 unwritten`), the single-pointer watcher missed 100% of uploads and executed the hardcoded fallback `cb2Data[4] = (0, 0, +1, 1000.0)` on every frame.
2. **Bug 1B — Fixed Dot Floor from Hardcoded `bhDist = 1000.0`:**
   - Because `cb2Data[4].w` was permanently `1000.0` Ls ($2.99792 \times 10^8\text{ km}$), `scale = clamp(120.0 / (distKm * 0.25 + 600.0), 0.00015, 0.020)` evaluated to $1.6 \times 10^{-6}$ and pinned at the minimum clamp floor `0.00015` ($\approx 0.0086^\circ$ radius, 1-2 pixels) at every distance.
3. **Bug 1C — Camera-Space Fallback + Rotated MVP Matrix on `0x41E245D488BFE83E`:**
   - The fallback direction `(0, 0, +1)` was evaluated directly in camera space (head-locked along the HMD optical axis).
   - Disassembly of `vs_41E245D488BFE83E.dxbc` proved `cb0[4..7]` on that draw is a rotated Model-View-Projection (MVP) matrix (`dp4 r1.x, cb0[4], r0`), not a pure projection matrix. When looking toward the planet/black hole, the world-to-clip rotation in `cb0[4].x` and `cb0[5].y` flipped sign; when looking 180 degrees away, the sign inverted and aligned the fallback `(0, 0, +1)` ray with the screen center.
4. **Bug 1D — `0x41E245D488BFE83E / 0x6EF82262EB12A037` Is Planetary Atmosphere (`kV2Atmos`), Not the Black Hole Lens:**
   - In `src/d3d11/native_on_foot.cpp:29295` and `docs/scanner-body.md:154`, `0x41E245D488BFE83E` is identified as `kV2Atmos`. Disassembly of `ps_6EF82262EB12A037.dxbc` confirmed Rayleigh/Mie scattering with horizon tangent distance $\sqrt{(r/R)^2 - 1}$ (`cb2[4].w` is distance in planetary radii). It vanished in C9..C12 (< 3,000 Ls) because the distant planet moved outside the frustum.

---

## 9. Agreed Architectural & Mathematical Design and Technical Specification (Flight 8, 2026-11-06)

### 9.1 System Architecture & Render Pipeline Order
Across all eye-HDR censuses (Table 4.2 & Table 5.1), the celestial pass executes in this exact order:
- **Order 3 (`vh=0x8471F654B0CBAA89`, `ph=0xB00B8B6FB5CDB153`):** Black-hole-system sky dome (replaces normal sky dome `0xF8FA801F2CB1E27C` in C2..C12 from 54,000 Ls down to 28.4 km). Sets `g_bhSkySeenThisFrame = true`.
- **Orders 4 & 5 (`vh=0x297B6F1F7F104F23`, `vh=0xB278189EFA1D574D`, `ph=0x06332CA168B6DA63`):** 13,873 background stars and 14 bright system stars (additive blend). Binds the 208-byte per-eye `VsCb0` containing the pure per-eye projection matrix `cb0[4..7]` ($P_{00}, P_{11}, P_{02}, P_{12}$) and orthonormal camera basis rows in world space: `cb0[10].xyz` = Right (+X), `cb0[11].xyz` = Up (+Y), `cb0[9].xyz` = Forward (+Z).
- **Order 6 (`vh=0x662ACF4E126EFFD1`, `ph=0x2B156A05E98F2D5D`):** Gravitational lens mesh (`n=3840`). Disassembly of `vs_662ACF4E126EFFD1.dxbc` shows its 240-byte `VsCb0` transforms the mesh by `cb0[0..2]` (scale/orientation) and `cb0[3].xyz` (3D camera-to-BH translation vector), providing live world direction $\hat{d}_{\text{world}} = \text{normalize}(\text{cb0}[3].xyz)$ and angular scale ratio $\|\text{cb0}[0].xyz\| / \|\text{cb0}[3].xyz\|$, which are latched in world space across normal-space transitions.
- **Order 7a (`vh=0x7F894EB5B6BA82A1`, `ph=0x095030F27D2C362A` in Normal Space) / Order 7b (`vh=0x9BFC7FD232328391`, `ph=0xCB7AF179DF4E6A60` in Supercruise):** Celestial dust sphere (`1/eye/frame`, additive blend, 208-byte `VsCb0` bound).
- **EDVR Accretion Disk Pass (`blackHoleDiskAfterDraw` immediately after Order 7a/7b):**
  - Triggers once per eye in VR (`g_eyeDrawnThisFrame[0..1]`) and once per frame in Flat (`g_eyeDrawnThisFrame[2]`) when `g_bhSkySeenThisFrame` is active.
  - Draws a full-screen triangle at the reversed-Z far plane (`z = 0.0, w = 1.0`) with read-only depth test (`D3D11_COMPARISON_GREATER_EQUAL`) and premultiplied alpha (`SrcBlend = ONE`, `DestBlend = INV_SRC_ALPHA`).
  - Because it runs *after* Orders 3, 4, 5, and 7a/7b, `outAlpha = 1.0` inside the event horizon shadow ($b < b_c$) completely extinguishes both the sky dome and all 13,873 background stars behind the black hole, while Order 8+ (cockpit canopy glass `0x025B4B9FF54622ED`, orbit lines, HUD, bloom, tonemap) and opaque depth ($z > 0$) properly occlude foreground geometry.

### 9.2 Dedicated 256-Byte Constant Buffer & Shadow Table (`src/d3d11/black_hole_disk.h`, `black_hole_disk.cpp`)
- **64-Slot CB Shadow Table (`g_cbShadowTable[64]`):** Intercepts CPU-side `Map`/`Unmap`/`UpdateSubresource` writes (`ByteWidth <= 4096`) and caches the first 256 bytes (`16 float4`s) keyed by `ID3D11Resource*`, with `blackHoleDiskCbUnmapped` called in `hookedUnmap` *before* `realUnmap`.
- **`BlackHoleDiskConstants` (`sizeof == 256`, bound at PS `b0`, with `psCb0` saved and restored in `SavedPipeline`):**
  - `camRight` (`xyz` = World Right, `w` = $P_{00}$), `camUp` (`xyz` = World Up, `w` = $P_{11}$), `camFwd` (`xyz` = World Fwd, `w` = $P_{02}$).
  - `projParams` (`x` = $P_{12}$, `y` = `timeSeconds`, `z` = `mode` 1 or 2, `w` = Kerr spin $a_*$).
  - `bhWorldDir` (`xyz` = $\hat{d}_{\text{world}}$, `w` = $\theta_g = GM/(c^2 D)$).
  - `diskNormal` (`xyz` = $\hat{n}_{\text{disk}}$, `w` = $r_{\text{ISCO}}/M$), `diskTangentU` (`xyz` = $\hat{t}_U$, `w` = $r_{\text{out}}/M$), `diskTangentV` (`xyz` = $\hat{t}_V$, `w` = $T_{\text{peak}}$).
  - `params` (`x` = `useInvViewProj`, `y` = `exposureScale`, `z` = $r_+/M$, `w` = $b_c/M$).

### 9.3 Relativistic Shader Mathematics (`src/shaders/black_hole_disk_simple.hlsl`)
1. **World-Space Ray Reconstruction:**
   $$\mathbf{v}_{\text{eye}} = \text{normalize}\!\left(\frac{x_{\text{ndc}} - P_{02}}{P_{00}},\; \frac{y_{\text{ndc}} - P_{12}}{P_{11}},\; 1.0\right), \qquad \hat{\omega} = v_{\text{eye},x}\mathbf{R}_{\text{right}} + v_{\text{eye},y}\mathbf{R}_{\text{up}} + v_{\text{eye},z}\mathbf{R}_{\text{fwd}}$$
2. **Singularity-Free 3D Geodesic Plane Intersection:**
   - Geodesic plane basis: $\hat{e}_1 = -\hat{d}_{\text{world}}$, $\hat{e}_2 = \text{normalize}(\hat{\omega} - (\hat{\omega}\cdot\hat{d}_{\text{world}})\hat{d}_{\text{world}})$.
   - Crossing angles where $\hat{x}(\psi) = \cos\psi\,\hat{e}_1 + \sin\psi\,\hat{e}_2$ meets $\hat{n}_{\text{disk}} \cdot \hat{x}(\psi) = 0$:
     $$\psi_1 = \text{atan2}(\hat{n}_{\text{disk}} \cdot \hat{d}_{\text{world}},\; \hat{n}_{\text{disk}} \cdot \hat{e}_2) \bmod \pi \in (0, \pi), \qquad \psi_2 = \psi_1 + \pi \in (\pi, 2\pi)$$
   - Impact parameter: $b = \sin\alpha / (\theta_g \sqrt{1 - 2\theta_g})$, Carter constant: $L_z = b\,\hat{n}_{\text{disk}} \cdot (-\hat{d}_{\text{world}} \times \hat{e}_2)$.
3. **Mode 1 (`simple`):** Schwarzschild ($a_* = 0$, $r_{\text{ISCO}} = 6M$, $b_c = 3\sqrt{3}M \approx 5.196M$) with analytic Poutanen-Beloborodov inversion for $r_1(b, \psi_1)$ (front face $\psi_1 \le \pi/2$ valid for all $b$ crossing in front of the shadow; lensed upper/lower arch $\psi_1 > \pi/2$ for $b > b_c$) and secondary ring $r_2(b, \psi_2)$.
4. **Mode 2 (`realistic`):** Kerr ($a_* = 0.85$, Bardeen-Press-Teukolsky $r_{\text{ISCO}} \approx 2.61M$, $r_+ \approx 1.527M$, $b_c \approx 4.65M$) sampling the CPU-precomputed `256x256` `DXGI_FORMAT_R16G16B16A16_FLOAT` RK4 geodesic LUT (`t0`) for $r_1$ (`.r`), $r_2$ (`.g`), $r_3$ (`.b` $n=2$ photon sub-ring), and capture mask (`.a`), plus journal stellar mass scaling (`journal_watch.cpp`).
5. **Radiative Transfer (`tools/blackhole_optics.py` & `tools/sgra_optics.py`):**
   - Exact circular-orbit redshift factor $g(r, L_z) = \frac{\sqrt{1 - 3M/r}}{\sqrt{1 - 2M/r_o}(1 - \Omega(r)L_z)}$ with $\Omega(r) = (r^{3/2} + a_*)^{-1}$.
   - Specific intensity $I_{\text{obs}} \propto g^4 F_{\text{PT}}(r)\mathcal{T}(r, \phi, t)$ using Page-Thorne flux $F_{\text{PT}}(r)$, 5-mode sheared log-spiral turbulence, and shifted blackbody color $T_{\text{obs}} = g T_{\text{eff}}(r)$ via Wyman-Sloan-Shirley CIE 1931 XYZ to linear sRGB.

---

## 10. Flight 8 Results & Verified Root-Cause Analysis (2026-11-06)

Log: `edvr_gfx_20261106_225405.log` (build `6AEEAB5B`, Flat mode at ~25 km in normal space, switching between `simple` and `realistic`).

### 10.1 User Observations in Flight 8
1. At 25 km in both `simple` and `realistic` modes, no accretion disk rendered on screen at all.
2. The old head-locked "mini" reverse disc/dot from Flights 6–7 was completely gone (confirming the old atmosphere shader hook `0x41E245D488BFE83E` was cleanly removed).
3. The auto-dumper wrote 4 new black hole shader files to `edvr_logs/shaders/`:
   - `vs_8471F654B0CBAA89.dxbc` and `ps_B00B8B6FB5CDB153.dxbc` (Order 3 black-hole-system sky dome)
   - `vs_99B41C87E45319E6.dxbc` and `ps_A9975F91040B0BCD.dxbc` (Order 6b close-approach mesh)

### 10.2 Verified Root Causes of Zero Pixels in Flight 8
1. **Defect A — `g_cbConstants` Bound Only to `PSSetConstantBuffers`, Not `VSSetConstantBuffers`:**
   - In `src/shaders/black_hole_disk_simple.hlsl`, `VSMain` read `camRight`, `camUp`, `camFwd`, and `projParams` from `cbuffer BlackHoleDiskCb : register(b0)` to compute `o.worldRay`.
   - However, `blackHoleDiskAfterDraw` in `src/d3d11/black_hole_disk.cpp` only bound `g_cbConstants` via `ctx->PSSetConstantBuffers(0, 1, psCbs)` and never called `ctx->VSSetConstantBuffers(0, 1, psCbs)`.
   - Consequently, `VSMain` read the game's bound `vsCb0` (`@116`, where `cb0[0..2]` are zeros/scale factors), producing a degenerate `o.worldRay = (0, 0, 0)` (NaN after `normalize`), so `PSMain` discarded 100% of pixels on `if (cosAlpha <= 0.0) discard;`!
   - **Fix:** Bound `g_cbConstants` to both `VSSetConstantBuffers(0, 1, cbs)` and `PSSetConstantBuffers(0, 1, cbs)`, and moved `omega` ray reconstruction from `VSMain` into `PSMain` (passing `o.grid = grid` in `TEXCOORD0`), which also eliminates linear vertex interpolation distortion across the wide-FOV full-screen triangle.
2. **Defect B — `g_cbShadowTable` Polluted on Frame 1 by Unfiltered `Map` Calls:**
   - `hookedMap` runs ~1,100 times per frame across dynamic vertex, instance, and constant buffers.
   - `blackHoleDiskCbUnmapped` inserted every mapped resource (`ByteWidth <= 4096`) into the 64-slot table without filtering to watched target buffers and without LRU eviction. All 64 slots filled on Frame 1 with unrelated buffers and never admitted the sky/starfield constant buffers.
   - **Fix:** Replaced with a 128-slot, 8-probe Knuth-hash targeted watch table (`WatchedCb`, modeled on `celestial_motion.cpp`) with LRU eviction by `lastDrawFrame`. Only buffers explicitly registered via `shadowFor(ID3D11Buffer*)` during Orders 3, 4, 5, 6, 7a, and 7b are watched; `blackHoleDiskCbMapped` / `blackHoleDiskCbUnmapped` / `blackHoleDiskCbUpdated` do a fast `findWatched(resource)` lookup and ignore all other resources.
3. **Breakthrough Discovery — Disassembly of `vs_8471F654B0CBAA89.dxbc` and `ps_B00B8B6FB5CDB153.dxbc` (Order 3 Sky Dome):**
   - `vs_8471F654B0CBAA89.dxbc` transforms any screen NDC position `(grid.x, grid.y, 1, 1)` directly into an unnormalized sky-space ray `o0.xyzw` via `cb2[11..14]`:
     $$\mathbf{r}_{\text{sky}} = x_{\text{ndc}}\,\text{cb2}[11] + y_{\text{ndc}}\,\text{cb2}[12] + \text{cb2}[13] + \text{cb2}[14]$$
   - `ps_B00B8B6FB5CDB153.dxbc` normalizes `v0.xyz` (`skyRay = normalize(v0.xyz)`) and subtracts **`cb2[18].xyz`**:
     ```hlsl
     dp3 r0.x, v0.xyzx, v0.xyzx
     rsq r0.x, r0.x
     mul r0.y, cb2[18].w, cb2[24].x
     mad r0.xzw, v0.xxyz, r0.xxxx, -cb2[18].xxyz
     ...
     add r1.xyw, v0.xyxz, -cb2[18].xyxz
     dp3 r0.w, r1.xywx, r1.xywx
     sqrt r0.w, r0.w
     add r1.x, -r0.w, cb2[21].x
     div_sat r1.x, r1.x, cb2[21].x
     ```
   - This proves that **`cb2[18].xyz`** in `ps_B00B8B6FB5CDB153` is the **exact unit direction vector of the black hole** in the exact same sky coordinate space as `cb2[11..14]`, **`cb2[21].x`** is the inner strong-lensing pinch angular radius, and **`cb2[18].w * cb2[24].x`** is the outer gravitational lensing angular radius!
   - Because Order 3 (`0x8471F654B0CBAA89 / 0xB00B8B6FB5CDB153`) executes on every frame on both VR eyes and in Flat mode at **all distances** (from 54,000 Ls in supercruise down to 25 km in normal space), extracting `cb2[11..14]` from `vsCb2` and `cb2[18]`, `cb2[21].x`, `cb2[24].x` from `psCb2` gives the exact per-eye sky ray transform, exact black hole direction, and live angular gravitational scale $\theta_g$ at all ranges with zero coordinate-space ambiguity.

---

## 11. Flight 9 Results & Verified Root-Cause Analysis (2026-11-08)

Log: `edvr_gfx_20261107_205450.log` (build `6AEEBA31`, VR profile at 25 km and supercruise past 3,000 Ls).

### 11.1 User Observations in Flight 9
1. **World Anchoring & Size Confirmed:** The black center (`b < b_crit`) and photon ring rendered at the exact black hole position in world space (`cb2[18]=(0.3836, -0.3191, 0.8666, 21.5225)`, `cb2[21].x=0.31480`) and at the right angular size.
2. **Missing Accretion Disk & Lensed Arches:** Only the solid black shadow and thin outer ring appeared; the equatorial disk and upper/lower lensed arches were invisible.
3. **Blurry Streak Artifact:** A blurry streak shot off one side of the black hole.
4. **Washed-Out White/Blue Color:** In `simple` mode the ring/streak was cold white, and in `realistic` mode it was blue, instead of the brilliant fiery golden-yellow seen in Flight 7's shader.
5. **Disappearance Past 3,000 Ls:** In supercruise past ~3,000 Ls, the black hole disappeared completely.

### 11.2 Verified Root Causes & Flight 10 Fixes
1. **Why the Disk & Lensed Arches Were Missing (`PageThorneFlux` 10,000× Underflow):**
   - `PageThorneFlux(r, r_isco, r_out)` evaluated the raw unnormalized textbook ratio $f(r) / [(r - 3) r^{5/2}]$, which at $r = 10M, r_{\text{ISCO}} = 6M$ is $0.036 / (7 \times 316.2) = 1.6 \times 10^{-5}$.
   - Even after multiplying by `65.0`, the disk intensity was `0.001` (`totalOpacity = 0.00005`), 10,000× dimmer than the photon ring (`12.0`), falling below the shader's `0.0005` discard threshold!
   - **Fix:** Replaced with peak-normalized `DiskEmissionProfile(r, r_isco, r_out)` peaking at `~1.0` near $1.35\,r_{\text{ISCO}}$, and traced all 3 relativistic sheets in the 3D world-locked `(diskRight, diskUp)` tangent frame:
     - **Sheet 1 (Direct Equatorial Disk):** Tilted plane ($\cos i = 0.22$) with the front equatorial bridge (`by < 0`) crossing visibly in front of the event horizon shadow (`vis1 = 1.0`), and the rear half occluded behind the shadow.
     - **Sheet 2 (Gravitationally Lensed Upper & Lower Arches):** Continuous upper arch (`r_arch`) curving over the top of `b_crit` and lower arch (`r_lower`) wrapping underneath `b_crit`, joining smoothly with the equatorial wings.
     - **Sheet 3 (`realistic` Kerr Geodesic LUT):** Adds the frame-dragged Kerr inner vortex ($a_* = 0.85, r_{\text{ISCO}} = 2.61M$) and higher-order sub-rings from `GeodesicLut`.
2. **Why There Was a Blurry Streak (`skyPos.w` Perspective Divide & `atan2` Branch Cut):**
   - In `vs_8471F654B0CBAA89.dxbc`, `o2.xyzw = v0.xyww` (`SV_POSITION`), so D3D11 hardware divides `v0.xyz` by `v0.w` (`div r0.w, l(1.0), v0.w` in `ps_B00B8B6FB5CDB153.dxbc`). On our full-screen triangle (`o.pos.w = 1.0`), `skyPos.w` was not divided out before `normalize(skyPos.xyz)`. Furthermore, the `psi1 <= pi/2` branch cut in the old analytic formula jumped discontinuously across the disk horizon.
   - **Fix:** Added explicit perspective divide `skyDir = (abs(skyPos.w) > 1e-5) ? (skyPos.xyz / skyPos.w) : skyPos.xyz` in `PSMain` and replaced the discontinuous `psi1` split with smooth `(bx, by)` tangent-plane coordinates.
3. **Why Simple Was White and Realistic Was Blue (`BlackbodyToLinearSrgb` Normalization):**
   - `BlackbodyToLinearSrgb(14000 K)` divided `xyz` by luminance `Y` and evaluated at $14,000\text{ K}$, producing cold blue-white `(0.65, 0.78, 1.45)`, while the photon ring was hardcoded to `float3(1.3, 1.4, 1.8)`.
   - **Fix:** Replaced with `AccretionTemperatureToRGB(T_norm, mode)` using the brilliant fiery golden-amber-yellow spectrum (`colRed`, `colGold`, `colYellowWhite`, `colHot`) in `simple` mode with a golden photon ring (`float3(2.2, 1.65, 0.75)`), and a fiery golden-orange outer disk transitioning to an electric blue-white Doppler-beamed Kerr vortex in `realistic` mode.
4. **Why It Disappeared Past 3,000 Ls in Supercruise:**
   - Past 3,000 Ls, the sky shader's inner pinch radius `cb2[21].x` (`r21`) drops to `0`, and `0x9BFC7FD232328391` switches pixel shaders (`0xDBF1725726018F52`, taken by `uiLayer`). Additionally, `runtimeFlatProfile()` in `vscreen.cpp` returned before calling `blackHoleDiskAfterDraw`.
   - **Fix:** Trigger the accretion disk draw immediately after Order 5 (`0xB278189EFA1D574D / 0x06332CA168B6DA63`, the bright-star pass that runs 100% of the time at all distances from 25 km to 54,000+ Ls right after the 13,873 background stars and before cockpit/HUD), added `blackHoleDiskAfterDraw` to the flat profile paths in `vscreen.cpp`, and set a minimum supercruise angular floor of `0.0012` rad on `g_latchedThetaG`.

---

## 12. Flight 10 Results, Crash Root Cause & Visual Refinement (2026-11-08)

Minidump: `20261108_110524_MiniDump.dmp` (access violation at `d3d11.dll+0x5BC701`).
Screenshot: `20261108110438_1.jpg` (250 km in normal space).

### 12.1 Verified Crash Root Cause in Flight 10
- Crash stack: `RIP = d3d11.dll+0x5BC701` (`vmovdqu ymm5, ymmword ptr [rdx+r8-20h]`) inside `std::memcpy(w->data, w->mapped, n)` in `blackHoleDiskCbUnmapped`.
  - `R8 = 0x190 = 400`, `RCX = &w->data`, `RDX = 0x0000025DFB68BF00`, `R9 = RDX + 0x190 = 0x0000025DFB68C090`.
  - The read crossed into an unmapped guard page at `0x0000025DFB68C000` (256 bytes after `RDX`).
- **Why `w->byteWidth` was larger than 256 bytes:**
  - When dropping out of supercruise at 27 km, Frontier freed a larger constant buffer (`ByteWidth >= 400`) and allocated a fresh 256-byte constant buffer at the exact same recycled pointer `0x000002619AC4BDE0`.
  - `blackHoleDisk` did not hook `kDevCreateBuffer` or invalidate `byteWidth = 0` on creation.
  - Additionally, calling `GetType`, `GetDesc`, and `addWatch` on all ~1,100 maps per frame polluted the 128-slot watch table and lacked SEH protection.
- **Fix:**
  - Adopted `celestial_motion.cpp`'s proven discipline: `blackHoleDiskCbMapped`, `blackHoleDiskCbUnmapped`, and `blackHoleDiskCbUpdated` only perform `findWatch(resource)`. Zero `GetType`/`GetDesc`/`addWatch` in `hookedMap`.
  - Added `blackHoleDiskBufferInvalidated(const void* resource)` hooked into `device_hook.cpp` (`kDevCreateBuffer`) and `vscreen.cpp` (`ExecuteCommandList`, `CopyResource`, `CopySubresourceRegion`), which clears `w->valid = false; w->mapped = nullptr; w->byteWidth = 0;` forcing `GetDesc` re-verification on the next draw.
  - Wrapped `memcpy` in `blackHoleDiskCbUnmapped` and `blackHoleDiskCbUpdated` inside `guardedBudget(g_budget, [&] { ... })`.
  - Removed `g_recentUnmaps` and deferred accretion disk drawing until `g_hasSkyBhAnchor` or `g_hasLatchedBhWorldDir` is valid, eliminating the 1-frame camera-forward flash.

### 12.2 Visual Refinements ("Weird Glow" in 250 km Screenshot)
- **Why the glow looked like a puffy boxy lemon:**
  1. `upperArchWeight` and `lowerArchWeight` evaluated `DiskEmissionProfile` out to `r_out = 30.0` over the whole region above/below the shadow without relativistic foreshortening, producing a 20M-tall solid halo.
  2. Direct disk and lensed arches were naively summed at the four corners without occlusion.
- **Fixes Applied to `black_hole_disk_simple.hlsl`:**
  1. **Relativistic Foreshortening on Upper Arch:**
     $$r_{\text{arch}}(b_x, b_y) = r_{\text{ISCO}} + \frac{b\,(b - b_{\text{crit}})}{\sqrt{b_x^2 + (\cos i \cdot b_y)^2}}$$
     Compresses the upper arch at the pole to a crisp 4.0M thickness hugging $b_{\text{crit}}$, flaring smoothly into the wide horizontal disk wings at the equator.
  2. **Tight Lower Secondary Arch:**
     $$r_{\text{lower}}(b_x, b_y) = r_{\text{ISCO}} + \frac{b - b_{\text{crit}}}{0.09 \cdot \max(-b_y / b, 0.05)}$$
     Forms a delicate, razor-thin secondary lower arch ($1.8M$ thick) along the bottom rim of the shadow.
  3. **Front-over-Back Compositing:**
     Direct front disk occludes the lensed arches behind it (`hdr1 + archHdr * (1.0 - opacity1 * 0.85)`).
  4. **Tuned Bloom Levels & Radial Falloff:**
     Steeper $u^{2.15}$ falloff in `DiskEmissionProfile` with refined peak HDR levels (1.45 on front disk, 1.25 on upper arch, 0.85 on lower arch, 2.2 on photon ring), giving high-contrast cinematic bloom without washing out the dark gap between the front disk and the upper arch.

---

## 13. Flight 11 Results & Flight 12 True 3D Geodesic Plane Fixes (2026-11-08)

Log: `edvr_gfx_20261108_115530.log` (`v0.22.0-rc.6-211-gd32a0952-dirty`, crash-free at 25 km).
User observations:
1. Crash at 27 km supercruise drop is resolved.
2. Perspective of the shader never changed as the ship flew around the black hole (always pinned to the same perspective).
3. Big yellow haze was still present around the black hole, and there was no distinct wrapping of the disk on the back side.

### 13.1 Verified Root Causes in Flight 11
1. **Why the Perspective Never Changed When Orbiting the Black Hole:**
   - In `src/d3d11/black_hole_disk.cpp:828-851`, `diskNormal` was constructed every frame from `localUp` and `side` orthogonal to `d_bh` (`g_latchedBhWorldDir`), rather than being a fixed 3D world-space spin axis vector! Because `diskNormal` rotated along with `d_bh`, the angle between the line of sight and the disk normal remained constant regardless of where the ship orbited.
   - Worse, in `src/shaders/black_hole_disk_simple.hlsl:179`, `cosInc` was hardcoded to `const float cosInc = 0.20;` instead of computing `dot(-d_bh, n_disk)`!
2. **Why the Big Yellow Haze Persisted & Backside Wrapping Was Missing:**
   - In `realistic` mode (`r_isco = 2.61, b_crit = 4.65`), `r_arch = 2.61 + 5.0 * (b - 4.65)` reached its peak emission ($1.35\,r_{\text{ISCO}} = 3.52M$) at $b = 4.83M$ — only $0.18M$ (2 pixels) outside the photon ring — while the smooth, featureless `DiskEmissionProfile` out to `r_out = 30.0` filled $b \in [5M, 30M]$ with a low-contrast ($\pm 20\%$) orange-yellow gradient.
   - Simultaneously, `Sheet 1` drew an unlensed flat ellipse across the upper hemisphere (`vis1 = 1.0` for `b > b_crit` even when `by > 0`), blending with `Sheet 2` into a featureless oval haze rather than bending a single equatorial disk plane over the top of the shadow.
   - Additionally, at 25 km (`cb2[21].x = 0.33084`), `theta_g = 0.14 * r21 = 0.04632` rad caused `30M` to span $1.39\text{ rad} \approx 80^\circ$ of the FOV, and offscreen `256x256` cubemap draws in Order 3/4/5 were not filtered before updating `g_eyeRtvs`.

### 13.2 Flight 12 Fixes
1. **Fixed 3D World-Space Spin Axis & Live 3D Orbiting Perspective (`black_hole_disk.cpp` & `black_hole_disk_simple.hlsl`):**
   - `diskNormal`, `diskTangentU`, and `diskTangentV` are now computed solely from `sysAddr` as a fixed 3D world-space orthonormal frame, completely independent of `d_bh`.
   - `PSMain` computes the live 3D viewing inclination `cosIncSigned = dot(-d_bh, n_disk)` and projects 3D geodesic hit vectors onto `(u_disk, v_disk)`, so flying around the black hole in inclination or azimuth continuously updates the disk's tilt, foreshortening, hemisphere sign, and spiral arm orientation.
2. **Unified Beloborodov (2002) / Luminet (1979) 3D Geodesic Plane Intersection (`black_hole_disk_simple.hlsl`):**
   - Replaced the separate flat Sheet 1 + oval Sheet 2 approximation with the exact $C^\infty$ closed-form relativistic light-bending intersection for a single equatorial plane:
     $$r_1(b, \psi_1) = \frac{r_{\text{lens}} + \sqrt{r_{\text{lens}}^2 + b^2\,\frac{1 + \cos\psi_1}{1 - \cos\psi_1}}}{1 + \cos\psi_1}$$
     with photon-sphere asymptotic pull to $r_{\text{ISCO}}$ as $b \to b_{\text{crit}}^+$ on the rear hemisphere ($\cos\psi_1 < 0$).
   - This single equation seamlessly draws the front equatorial bridge across the shadow ($\cos\psi_1 > 0$) AND bends the rear half of the disk up and over the northern hemisphere of the shadow ($\cos\psi_1 < 0$), while Sheet 2 ($n=1$, $\psi_2 = \pi + \psi_1$) wraps the underside of the disk around the bottom rim of the shadow.
3. **High-Contrast Concentric MRI Rings, Dark Gaps & Tight Outer Cutoff (`DiskStructureAndTurbulence`):**
   - Added multi-frequency concentric radial ring modulation with dark gaps (`ringWave1 * ringWave2 * ringWave3`) and high-contrast sheared Keplerian spiral filaments (`pow(spiralContrast, 2.15)`), tightened `r_out` to `18.0M` with an exponential outer cutoff, and scaled `g_latchedThetaG = clamp(r21 * 0.065f, 0.0010f, 0.024f)` so there is zero diffuse yellow fog.
   - Filtered `vp.Width <= 512.0f` at the start of Order 3 and Order 4/5 in `black_hole_disk.cpp` and copied `g_skyInvViewProj` from `wPs2` when `wVs2` is unmapped.

---

## 14. Flight 12 Results & Flight 13 Refinements (2026-11-08)

Log: `edvr_gfx_20261108_144645.log` (`v0.22.0-rc.6-211-gd32a0952-dirty`).
User observations:
1. Overall looking really good; `simple` mode looks fine.
2. In `realistic` mode, there was a second blown-out disk (fuzzy on one side, sharper on the other, at a different angle) that was not present in `simple`, and `realistic` was still using the golden glow (questioning whether gold is mathematically right for this stellar-mass black hole).
3. Crossing over the equatorial plane between the top and bottom hemispheres caused the black hole disk to pop into the new orientation.
4. The black hole was smaller than expected (`theta_g = 0.00100` in the log).

### 14.1 Verified Root Causes in Flight 12
1. **Why `realistic` Mode Had a Second Blown-Out Disk at a Different Angle (`v_lut` Coordinate Mismatch):**
   - In `generateGeodesicLut` (`src/d3d11/black_hole_disk.cpp:280`), row coordinate `v_tex` is parameterized by the 3D orbital deflection angle $\psi_{\text{target}} = v_{\text{tex}} \cdot \pi \in [0, \pi]$.
   - However, Sheet 3 in `src/shaders/black_hole_disk_simple.hlsl:267` sampled `v_lut = (atan2(by, bx) + pi) / (2 * pi)`. Because `atan2(by, bx) = -pi` on the left (`bx < 0`) and `0` on the right (`bx > 0`), `v_lut` swept from `0` on the left (where $r_3 = \infty$, fuzzy outer halo) to `0.5` on the right (where $r_3 \approx r_{\text{ISCO}}$, sharp inner edge), and completely ignored 3D inclination foreshortening $\cos\psi_1$!
2. **Why `realistic` Mode Should Be Hot Blue-White/Cyan-Violet Instead of Gold (`T_{\text{eff}} \propto M^{-1/4}$):**
   - By Novikov-Thorne / Shakura-Sunyaev accretion physics, peak effective disk temperature scales as $T_{\text{peak}} \propto (M / M_\odot)^{-1/4}$. While a supermassive black hole like Gargantua ($10^8 M_\odot$) peaks at $\sim 5,500\text{ K}$ (golden-yellow, kept for `simple` mode), stellar-mass black holes in Elite Dangerous ($M \sim 5\text{--}20\,M_\odot$, `peakTempK = 14000 K`) peak at $T_{\text{eff}} \ge 14,000\text{--}30,000\text{ K}$ in the O/B-type UV/blue-white Planckian regime!
   - Previously, `AccretionTemperatureToRGB` used the exact same golden-orange ramp (`colDeepOrange`, `colBrilliantGold`, `colYellowWhite`) for `T_norm < 1.25` in both `simple` and `realistic` modes.
3. **Why Crossing the Equatorial Plane Popped the Orientation (`hemiSign` Discontinuity):**
   - `hemiSign = (cosIncSigned >= 0.0) ? 1.0 : -1.0` jumped from `+1` to `-1` in a single frame at `cosIncSigned = 0` and flipped `n_eff`, which inverted `diskRight = cross(n_eff, d_bh)` ($+b_x \to -b_x$) and snapped the apparent inclination from $+6.9^\circ$ to $-6.9^\circ$.
4. **Why the Black Hole Was Too Small (`theta_g = 0.00100`):**
   - In Flight 12, `cb2[18].w = 0.3179`, `cb2[24].x = 1.0`, `cb2[21].x = 0.00000`, so `(bw * r24x) * 0.0012f = 0.00038` clamped to the minimum floor `0.00100` rad ($\approx 1.1^\circ$ full disk diameter).

### 14.2 Flight 13 Fixes
1. **3D-Aligned Kerr Geodesic LUT (`v_lut = acos(cosPsi1) / pi`) & Kerr Frame-Dragging Shadow Shift:**
   - Sheet 3 now computes `psi1_angle = acos(clamp(cosPsi1, -1.0, 1.0))` and samples `v_lut = psi1_angle / 3.1415926535`, aligning the Kerr $n=2$ photon sub-ring with the exact 3D foreshortened disk plane and applying full `DiskStructureAndTurbulence` and Doppler beaming (`g3^3`).
   - Added Bardeen (1973) prograde Kerr shadow displacement (`bxShadow = bx - 0.32 * a_spin * sinIncMag` in `realistic` mode, $r_{\text{ISCO}} = 3.8M$) and Doppler asymmetry along the photon ring.
2. **Astrophysical Stellar-Mass O/B-Type Planckian Spectrum in `realistic` Mode:**
   - `simple` mode retains the warm cinematic Gargantua golden-amber-white spectrum.
   - `realistic` mode now uses the stellar-mass ($T_{\text{peak}} \ge 14,000\text{ K}$, scaled by `journalBlackHoleStellarMass()`) Planckian relativistic spectrum: deep Doppler-redshifted magenta-crimson outer edge (`0.75, 0.18, 0.38`) $\to$ warm lavender-ivory transition (`1.35, 1.15, 1.30`) $\to$ hot B-type cyan-white (`1.45, 1.95, 2.65`) $\to$ dazzling O-type actinic blue-white core (`1.85, 2.35, 3.25`) with an electric ice-blue photon ring (`1.65, 2.10, 2.95`).
3. **$C^\infty$ Smooth Equator Crossing (Zero Pop):**
   - Removed `hemiSign` and `n_eff`. `diskUp` and `diskRight` are now derived directly from fixed `n_disk` (`diskRight = cross(diskUp, d_bh)`), so `(bx, by)` never flips chirality when crossing the equator.
   - Replaced the hard clamp `clamp(abs(cosIncSigned), 0.12, 0.96)` with the smooth $C^\infty$ regularizer `cosIncReg = sqrt(cosIncSigned * cosIncSigned + 0.08 * 0.08)` and continuous `byOriented = by * (cosIncSigned / cosIncReg)`, so flying across the equatorial plane smoothly transitions the front bridge from bottom to top through a symmetric edge-on cross with zero pop.
4. **Larger Angular Scale (`g_latchedThetaG`):**
   - Increased `g_latchedThetaG` scaling in `src/d3d11/black_hole_disk.cpp:652-658` to `clamp(r21 * 0.105f, 0.0022f, 0.036f)` and `clamp((bw * r24x) * 0.0065f, 0.0022f, 0.036f)` with `r_out = 20.0M`, more than doubling the supercruise/mid-range angular size and increasing close-range scale by ~65%.

---

## 15. Flight 13 Results & Flight 14 Architecture (2026-11-08)

Log: `edvr_gfx_20261108_220646.log` (build `6AF0E3C8`, VR profile at 27 km Exclusion Zone).
Screenshots: `20261108221040_1.jpg`, `20261108221223_1.jpg`, `20261108221240_1.jpg`, `20261108221248_1.jpg`.

### 15.1 Flight 13 Findings
1. **The Horizontal Cut / Disk Half-Way Across Shadow:**
   - In `20261108221223_1.jpg` and `20261108221248_1.jpg`, the upper half of the shadow was pure black with a clean photon ring, while the lower half was filled by the accretion disk with a flat horizontal knife-edge cut across the diameter at $b_y = 0$.
   - **Root Cause:** Sheet 1 calculated $r_{\text{direct}}$ via Beloborodov's formula $(r_{\text{lens}} + \sqrt{r_{\text{lens}}^2 + b^2 \text{ratio}_1}) / (1 + \cos\psi_1)$. When $b_y \to 0^-$, $\cos\psi_1 \to 0$ and $\text{ratio}_1 \to 1.0$, yielding $r_{\text{direct}} \approx 2.0 + \sqrt{4 + b^2} \ge 6.0M \ge r_{\text{ISCO}}$ inside the shadow. For $b_y < 0$, `frontBridgeWeight` was $1.0$ (`vis1 = 1.0`), drawing disk emission inside the shadow. For $b_y > 0$, `frontBridgeWeight` was $0.0$, so `vis1` stepped to $0.0$, creating a sharp cut at $b_y = 0$.
   - **Physics:** An accretion disk only exists for $r \ge r_{\text{ISCO}}$. Inside $r < r_{\text{ISCO}}$, the disk is empty vacuum. For foreground line-of-sight rays, the radial coordinate on the physical disk plane is $r_{\text{front}} = \sqrt{b_x^2 + (b_y / \cos i_{\text{eff}})^2}$. Inside the central hole ($r_{\text{front}} < r_{\text{ISCO}}$), emission is strictly zero.
2. **Angular Size Too Small:**
   - In Flight 13, Frontier's sky dome reported $r21 = 0.34666$ rad ($19.86^\circ$ radius, $40^\circ$ diameter). However, `g_latchedThetaG` was clamped to $0.036f$ ($2.06^\circ$), making the shadow radius only $0.187$ rad ($10.7^\circ$)—half the radius of Frontier's inner pinch and 10x smaller than the surrounding gravitational warp.

### 15.2 Flight 14 Fixes
1. **Physical Plane Foreground Intersection (`r_front`):**
   - In `src/shaders/black_hole_disk_simple.hlsl`, rays inside $b_{\text{Shadow}} < b_{\text{crit}}$ evaluate $r_{\text{direct}} = r_{\text{front}} = \sqrt{b_x^2 + (b_y / \cosIncReg)^2}$.
   - Where $r_{\text{front}} < r_{\text{ISCO}}$, `DiskEmissionProfile` returns 0.0, guaranteeing an unbroken, pure black event-horizon shadow with zero horizontal shelf.
   - Where $r_{\text{front}} \ge r_{\text{ISCO}}$, the disk shines across the front of the shadow following a smooth, continuous ellipse $b_x^2 + (b_y / \cosIncReg)^2 = r_{\text{ISCO}}^2$.
2. **1:1 Matched Scale (`g_latchedThetaG`):**
   - Scaled $\theta_g = \text{clamp}(r21 \times 0.185f, 0.0035f, 0.080f)$ in normal space and $\text{clamp}((bw \times r24x) \times 0.012f, 0.0035f, 0.065f)$ in supercruise.
   - At $r21 = 0.347$ rad, $\theta_g = 0.0641$ rad gives $\theta_{\text{shadow}} = 5.196 \times 0.0641 = 0.333$ rad ($19.1^\circ$), matching Frontier's inner pinch within 3% and restoring a majestic, imposing black hole.

---

## 16. Flight 14 Results & Flight 15 Architecture (2026-11-08)

Log: `edvr_gfx_20261108_224858.log` (build `6AF0E3C8` / `95396cac`, VR profile at 27 km and 31,480 Ls).
Screenshots: `20261108225109_1.jpg` (simple mode, 27 km), `20261108225328_1.jpg` (realistic mode, 27 km), `20261108230137_1.jpg` (supercruise, 31,480 Ls).

### 16.1 Flight 14 Findings
1. **`simple` Mode Confirmed Gorgeous:**
   - In `20261108225109_1.jpg`, `simple` mode rendered an imposing, screen-filling black hole matching Frontier's pinch ring, with a 100% unbroken, pure black central shadow and brilliant golden arches.
2. **`realistic` Mode Inner Overlap & Size Mismatch:**
   - In `20261108225328_1.jpg`, `realistic` mode had the inner overlap problem because $r_{\text{ISCO}} = 3.8M < 4.65M = b_{\text{crit}}$, allowing disk emission to evaluate inside the shadow circle. Additionally, $b_{\text{crit}} = 4.65M$ made the shadow 11% smaller than in `simple` mode ($5.196M$).
3. **Static Supercruise Size at Large Distances:**
   - In `20261108230137_1.jpg`, at 31,480 Ls the black hole stayed clamped to the floor `0.0035f` (~$8^\circ$, matching the targeting reticle) and did not grow dynamically as the ship moved between 300,000 Ls and 3,000 Ls.
4. **Pinched Warping at the Arch-Wing Junction:**
   - On the left and right limbs, Beloborodov's bending formula had a $\sim 2.0M$ radial jump between $r_{\text{front}} = |b_x|$ and $r_{\text{rear}} = 2.0 + \sqrt{4 + b_x^2}$. Blending across a narrow $[-0.08, 0.08]$ slice in $\cos\psi_1$ caused concentric ring shearing where the arch joins the wings.

### 16.2 Flight 15 Architecture
1. **Unbroken Pure Black Shadow & Exact Scale in `realistic` Mode:**
   - Set $r_{\text{ISCO}} = 5.5M$ and $b_{\text{crit}} = 5.196M$ for both modes in `black_hole_disk.cpp`, ensuring `realistic` mode matches `simple`'s imposing scale and pristine unbroken shadow center while preserving its O/B-type Planckian cyan/violet spectrum and Kerr $n=2$ sub-ring.
2. **Dynamic Supercruise Distance Scaling:**
   - $\theta_g = \text{clamp}(0.0008f + (bw \cdot r24x) \cdot 0.0032f, 0.0008f, 0.065f)$, smoothly expanding the disk from a compact, glowing star-like jewel at 54,000+ Ls into a screen-filling giant as you approach.
3. **Radiant Star-Like Optical Bloom & Glare:**
   - Boosted peak HDR radiance to $\sim 15.0$ and added an optical glare scattering envelope around the inner disk, driving Elite's native bloom pass to create atmospheric glare like bright stars.
4. **Smooth Limb Blending:**
   - Asymptotic decay of gravitational bending at the limbs ($b \gg b_{\text{crit}}$) with wide limb blending, eliminating pinched warping where the arch meets the wings.

---

## 17. Flight 15 Results & Flight 16 Architecture (2026-11-09)

Log: `edvr_gfx_20261109_000515.log` (build `6AF15AE0` / `2214d287`, VR profile at 11.3 Ls, 1.67 Ls, 0.24 Ls, and 25 km).
Screenshots: `20261109000932_1.jpg` (11.3 Ls), `20261109000946_1.jpg` (1.67 Ls), `20261109000958_1.jpg` (0.24 Ls).

### 17.1 Flight 15 Findings
1. **The 0.24 Ls Size Pop:**
   - In `black_hole_disk.cpp`, `bw \cdot r24x` is ~23.7 constantly in supercruise. Beyond 0.25 Ls, `r21 == 0`, so `bw \cdot r24x` drove $\theta_g = 0.065$ rad (filling the targeting reticle).
   - At 0.24 Ls, Frontier's engine initialized `r21` at a tiny initial value ($\sim 0.005$). The `if (r21 > 1e-4f)` branch immediately took over and slammed $\theta_g$ down to $0.005 \times 0.185 = 0.0009$ rad—an instant 70x pop down to a tiny dot in 1 frame.
2. **Vertically Blown-Out Expanding Upper Arch:**
   - In `black_hole_disk_simple.hlsl`, `limbFalloff = exp(-max(bShadow - b_crit, 0.0) / 3.5)` evaluated at the top of the arch ($b_y \gg b_{\text{crit}}$) decayed to near zero, replacing $r_{\text{rear}}$ with $r_{\text{front}} = \sqrt{b_x^2 + (b_y / \cosIncReg)^2}$. Because $\cosIncReg \approx 0.2$ to $0.3$, $r_{\text{front}}$ was huge, ballooning the upper arch vertically into a tall oblong capsule as the ship moved.
3. **Excessive Foggy Glare Washout:**
   - `hdrGlare` scattered a broad exponential diffuse envelope out to $15M$, creating excessive foggy haze across the disk and darkening the starfield in `outAlpha`.

### 17.2 Flight 16 Architecture
1. **Monotonic Dynamic Sizing (Zero Pop):**
   - At extreme supercruise distance (54k Ls), the black hole is a tight, brilliant star-like jewel at $\theta_g = 0.0015$ rad (~$2^\circ$ disk diameter).
   - As the ship approaches in supercruise, Frontier's `r21` rises smoothly from 0 to 0.35 rad.
   - Scaling $\theta_g = \text{clamp}(\max(r21 \times 0.185f, 0.0015f), 0.0015f, 0.075f)$ ensures strictly monotonic growth with zero pop.
2. **Tight Relativistic Upper Arch:**
   - Removed `limbFalloff` and `r_rear_smooth`. Restored pure Beloborodov $r_{\text{rear}}$ with photon-sphere pull, hugging the shadow cleanly across the top.
   - Smooth Hermite limb blend `frontBridgeWeight = smoothstep(-0.18, 0.18, cosPsi1)` provides seamless wing continuity without warping.
3. **Pure Black Shadow & Natural Optical Bloom:**
   - Removed `hdrGlare` fog and starfield opacity offset. Kept the razor-thin photon spike ($2.25$) and peak radiance ($1.45$), letting Elite's native bloom flare naturally without foggy wash.
   - $r_{\text{ISCO}} = 5.5M$ and $b_{\text{crit}} = 5.196M$ in both `simple` and `realistic` modes guarantees 100% pure black unbroken central shadow.

