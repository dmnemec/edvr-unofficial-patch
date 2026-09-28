# Cockpit MFD (Multi-Function Displays) — Design & Status

## Status
- **State**: In development (Option A Declarative JSON live; contextual activity gating, vJoy input, color themes & interactive settings operational).
- **Active Branch**: `feat/cockpit-mfd` on `fork/feat/cockpit-mfd`.
- **Latest Verified Flight**: Gaze dwell tracking, auto-hide fading, in-MFD live settings adjustment, vJoy POV/button routing, 6 color themes, and title-screen suppression verified.
- **Open Items / Current Goals**:
  1. **HUD Operating Mode Gating (Combat vs Analysis Mode)**: Support selective MFD visibility depending on whether the ship/SRV cockpit is in Combat Mode (`Flags` bit 27 = 0) or Analysis Mode (`Flags` bit 27 = 1).
  2. **Compound State Combinations (Boolean AND / OR Evaluation)**: Allow combining vehicle, flight, and HUD mode contexts with flexible Boolean logic (e.g. `(In Ship OR In SRV) AND (Analysis Mode)` or `(In Flight) AND (Combat Mode OR Hardpoints Deployed)`).
  3. **Custom Slot Configurator**: Scalable slot definitions allowing users to configure custom MFD displays and attach data providers via declarative JSON.

---

## Architecture & Implementation Notes

### 1. Contextual Activity & Game State Gating
- Read Elite `Status.json` flags bitmask and state:
  - `Flags` bit 0: Docked (on landing pad)
  - `Flags` bit 1: Landed (on planet surface)
  - `Flags` bit 2: Landing Gear Down
  - `Flags` bit 6: Hardpoints Deployed
  - `Flags` bit 24: In Main Ship
  - `Flags` bit 25: In Fighter (SLF)
  - `Flags` bit 26: In SRV
  - `Flags` bit 27: Hud in Analysis Mode (0 = Combat Mode, 1 = Analysis Mode)
  - `Flags2` bit 0: On Foot
- **Title / Loading Screen Suppression**: If `Flags == 0` or game is not in active cockpit, all MFD rendering and gaze focus is suppressed.
- **HUD Mode Contexts**:
  - `kActivityCombatMode` (`Flags` bit 27 == 0): Display only when cockpit is in Combat Mode (weapons/targeting MFDs).
  - `kActivityAnalysisMode` (`Flags` bit 27 == 1): Display only when cockpit is in Analysis Mode (exploration, discovery scanner, exobiology).
- **Compound Boolean Combinations (AND / OR Selectors)**:
  - Context states are organized into orthogonal categories:
    1. **Vehicle Type**: Ship, SRV, SLF Fighter, On Foot
    2. **Flight State**: In Flight, Docked / Landed, Hardpoints Deployed
    3. **HUD Mode**: Combat Mode, Analysis Mode
  - Evaluation Architecture:
    - **Category-Based AND**: Conditions across different categories must ALL be satisfied (e.g. `Vehicle == Ship` AND `FlightState == InFlight` AND `HudMode == AnalysisMode`).
    - **Within-Category OR**: Multiple selections within a category match if ANY is satisfied (e.g. `Vehicle == (Ship OR SRV)`).
    - **Explicit Boolean Mask Sets**: Slots carry both an `includeAnyMask` (OR filter) and a `requireAllMask` (AND filter) for full user customization in config and the in-MFD settings tab.

### 2. Extensible Slot Architecture
- `MfdSlot` encapsulates `MfdPose`, `MfdGazeTracker`, `IMfdProvider`, `MfdRenderer`, `MfdColorTheme`, and per-screen settings.
- Slots are managed in a dynamic vector in `MfdManager` with lookup by unique ID (`main_mfd`, `left_mfd`, `right_mfd`, or custom user slots).

### 3. Gaze Tracking & Gaze Delay
- `MfdGazeTracker` tests ray-plane intersection from seated eye pose against each MFD quad.
- Gaze dwell timer (`m_dwellEnterTime`, default 150ms) ensures natural head glance filtering before triggering focus.
- Gaze-activated visibility: if `slot.autoHideUntilGaze` is enabled, alpha smoothly ramps $0\% \to \text{opacity}$ on dwell acquisition and fades back when looking away.

### 4. DirectInput & vJoy Input Routing
- `MfdInputRouter` polls `GetAsyncKeyState` for keyboard and `joyGetPosEx` across connected joystick/vJoy devices (POV hats, D-pads, secondary action buttons).
- When focused, inputs are routed to active MFD and swallowed via `inputGateSetPrivate` to safeguard ship flight systems.

### 5. Interactive In-MFD Settings Tab
- Persistent `SETTINGS` tab allows per-screen real-time adjustments via Left / Right inputs:
  - Position X / Y / Z, Pitch / Yaw, Glass Opacity, Scale, Color Theme (6 palettes), Auto-Hide, Dwell Delay, and Off-Center Anchor offsets.

