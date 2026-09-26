# Virtual Cinema SBS 3D and On-Foot Perspective Evaluation

## Status

*Written 2026-09-26 following flight test evaluation of virtual cinema Side-by-Side (SBS) 3D mode and on-foot camera behaviour.*

- **State:** Verified in flight testing. Virtual cinema SBS 3D projection is implemented in `panel_curve.cpp` via `fix.vscreen_sbs_3d` and `fix.vscreen_sbs_swap_eyes`, with live in-headset F8 menu controls.
- **Open:**
  - Whether external stereoscopic content feeds or flat cinema 3D media playback projected onto the virtual screen can leverage custom aspect ratios.
- **Ruled out:**
  - *In-game Display 3D = "Side-by-Side"* as an on-foot VR 3D solution: ruled out by flight test (2026-09-26). Elite's engine splits each eye's VR render target when in-game 3D SBS is selected, producing four viewports total (two side-by-side viewports inside each eye's display).
  - *Odyssey on-foot gameplay in SBS 3D without engine stereo camera matrices*: ruled out by flight test (2026-09-26). The perspective is severely distorted with hyper-stereo divergence, excessive perceived eye separation, incorrect forward convergence, and a narrow central overlap region. Odyssey's on-foot camera pipeline is fundamentally monoscopic.
- **Next steps:**
  - Maintain the virtual cinema SBS 3D feature for stereoscopic cinema and flat media projection.
  - Ensure documentation clearly guides users to keep in-game Display 3D disabled (standard 2D flat / HMD) when using EDVR's SBS virtual screen.

---

## Overview and Purpose

Elite Dangerous: Odyssey renders on-foot gameplay (and HMD Cinema Mode) onto a flat virtual screen suspended in the player's 3D VR cockpit or helmet space. EDVR provides geometric control over this virtual cinema display through `panel_distance` (depth) and `panel_curvature` (wrapping the quad into a curved theatre screen).

To support stereoscopic 3D content displayed on the virtual screen, EDVR implements Side-by-Side (SBS) 3D virtual cinema projection. When active, EDVR splits the virtual screen texture across the headset's eyes, mapping the left half of the rendered frame to the left eye and the right half to the right eye.

---

## Implementation and Configuration

### Configuration Keys (`edvr.ini`)

Under the `[fix]` section of `edvr.ini`:

```ini
# Enable Side-by-Side (SBS) 3D on the virtual cinema screen
# ui: On-foot SBS 3D | choices on, off | menu
fix.vscreen_sbs_3d = off

# Swap left and right eyes for Side-by-Side (SBS) 3D
# ui: Swap SBS 3D eyes | choices on, off | menu
fix.vscreen_sbs_swap_eyes = off
```

Both settings take effect live and are exposed directly in the in-headset F8 settings menu:
- **`On-foot SBS 3D`**: Toggles the stereo split on the virtual screen.
- **`Swap SBS 3D eyes`**: Inverts eye assignment for cross-eye stereoscopic material.

### D3D11 Pipeline Architecture (`panel_curve.cpp`)

The virtual cinema screen in Elite is drawn via a small indexed draw call compositing a quad into each eye's render target. In `src/d3d11/panel_curve.cpp`:

1. **Dual Vertex Buffers:** When `fix.vscreen_sbs_3d` is enabled, `panel_curve` allocates two separate vertex buffers (`g_vb[0]` and `g_vb[1]`), one for each eye.
2. **UV Partitioning:**
   - Left eye (Eye 0): Maps normalized horizontal texture coordinates $U \in [0.0, 0.5]$.
   - Right eye (Eye 1): Maps normalized horizontal texture coordinates $U \in [0.5, 1.0]$.
   - Formula:
     $$u = \begin{cases} u_{\text{norm}} \times 0.5 & \text{for Eye 0} \\ 0.5 + u_{\text{norm}} \times 0.5 & \text{for Eye 1} \end{cases}$$
3. **Eye Swap:** If `fix.vscreen_sbs_swap_eyes = on`, the target eye index is inverted (`1 - targetEye`), swapping the UV halves between the physical displays.
4. **Composition:** The SBS UV split composes cleanly with `panel_curvature` tessellation and `panel_distance` depth scaling; curved cylinder vertices receive their respective eye's half-texture coordinates without distorting the bend geometry.

---

## The In-Game 3D Setting Pitfall

During flight testing, attempting to enable stereoscopy by changing Elite's native graphics settings revealed a critical pitfall:

### The Failure Mode

In Elite's options menu under **Graphics $\rightarrow$ Display $\rightarrow$ 3D**, the game offers a native "Side-by-Side" (SBS 3D) display mode intended for 3D TVs and monitors. 

When running in VR (HMD mode) and setting this in-game option to "Side-by-Side":
- The game engine applies the SBS viewport split *inside each VR eye buffer*.
- Each eye's VR display surface receives two squished side-by-side viewports.
- Result in headset: **Four viewports total** — two miniature side-by-side viewports visible in the left lens, and two miniature side-by-side viewports visible in the right lens. Fusing this image in VR is impossible.

### Correct Usage

- **In-Game Display Mode:** Must remain standard **2D / HMD** (native flat presentation).
- **EDVR Virtual Screen:** EDVR's `fix.vscreen_sbs_3d` intercepts the flat composite quad at the D3D11 level and performs the eye-split in VR space, ensuring each eye of the headset sees only its designated half of the image stretched across the virtual cinema panel.

---

## On-Foot SBS 3D Evaluation and Findings

Flight testing evaluated whether virtual cinema SBS 3D could provide a comfortable pseudoscopic or stereoscopic on-foot experience in Odyssey.

### Observed Flight Symptoms

1. **Severe Hyper-Stereo Divergence:** The virtual perspective felt uncomfortably distorted, as if the virtual cameras were placed excessively far apart (inter-ocular distance far beyond human IPD).
2. **Convergence Mismatch:** Straight-ahead lines and nearby geometry failed to converge naturally; the player's eyes had to diverge or strain inward unnaturally depending on focal distance.
3. **Narrow Central Overlap:** Only a small sliver in the centre of the field of view could be fused binocularly, with severe binocular rivalry and double vision across the periphery.

### Root Cause Analysis

- **Monoscopic Engine Pipeline:** Odyssey's on-foot renderer was architected strictly as a single-camera monoscopic pipeline. The game does not compute two separate viewpoint passes with offset camera origins for on-foot scenes.
- **Absence of Stereo Projection Matrices:** True VR stereoscopy requires two distinct view and projection matrices separated along the inter-pupillary axis, sharing a common world coordinate space and converging correctly across depth planes.
- **Forced Projection Artifacts:** When a monoscopic viewport is forced into an SBS format (or if an artificial SBS split is applied without dual-camera world-space offsets), the geometry lacks genuine horizontal disparity. Without engine-level dual-camera matrices, SBS projection cannot synthesize true stereopsis and instead introduces severe ocular strain and divergence.

### Conclusion

Virtual cinema SBS 3D is architecturally functional and operates as designed for pre-authored stereoscopic SBS content (such as 3D cinema media or external stereo inputs projected onto the virtual quad). However, Odyssey's on-foot gameplay cannot be turned into a viable stereoscopic VR experience solely through virtual screen SBS splitting without native dual-camera engine hooks.
