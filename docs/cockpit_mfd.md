# Cockpit MFD (Multi-Function Displays) — Design & Status

## Status
- **State**: In development (Option A Declarative JSON live; contextual activity gating & interactive controls in progress).
- **Active Branch**: `feat/cockpit-mfd` on `fork/feat/cockpit-mfd`.
- **Latest Verified Flight**: Gaze acquisition, direct flight input suppression, live `Status.json` pips/fuel/cargo updates, and Spansh waypoint clipboard copying confirmed working.
- **Open Items / Current Goals**:
  1. **Contextual Game State Gating (Ship / SRV / Docked / Flight / Hardpoints / On-Foot / Main Menu)**: Suppress MFD rendering during main menu, title screens, ship spin loading screens, and support per-screen vehicle/state visibility masks (e.g. ship-only, SRV-only, flight-only, hardpoints deployed).
  2. **Interactive In-MFD Settings**: Make the persistent `SETTINGS` tab interactive (modify position XYZ, tilt/yaw, opacity, scale, color theme per-screen directly while focused via Up/Down and Left/Right).
  3. **vJoy & DirectInput Joystick/HOTAS Input Capture**: Intercept secondary menu / vJoy inputs when an MFD is focused and route them to MFD navigation (tabs, scroll, select) while suppressing them from the ship.
  4. **Custom Color Profiles**: Support selectable color palettes (e.g. Amber/Orange, Cyan/Blue, Green, Custom EDHM-aligned themes) per screen and globally.
  5. **Gaze Dwell Delay**: Configurable gaze-delay timer (e.g. 150-300ms) before a screen activates its focused state to allow natural cockpit scanning without accidental activation.
  6. **Gaze-Only Auto-Visibility**: Option per-screen (and global override) to render MFD screens invisible/hidden until gaze enters their focus area + dwell delay elapses.
  7. **Focus Cone Sensitivity & Off-Center Anchor**: Configurable focus cone radius and vertical/horizontal focus offset (e.g. anchor focus point to the lower third of the screen).
  8. **Per-Screen Default Tilt / Yaw & Deviation Limits**: Independent baseline orientation per screen with clamping/deviation limits from baseline for head-perspective rotation.
  9. **Unlimited Dynamic Slots**: Scalable slot container supporting arbitrary numbers of MFD screens (preconfiguring default 3: center, left, right; enabling custom slots via JSON/config).

---

## Architecture & Implementation Notes

### 1. Contextual Activity & Game State Gating
- Read Elite `Status.json` flags bitmask and GUI focus / game state:
  - `Flags` bit 0: Docked (on landing pad)
  - `Flags` bit 1: Landed (on planet surface)
  - `Flags` bit 2: Landing Gear Down
  - `Flags` bit 6: Hardpoints Deployed
  - `Flags` bit 26: In SRV
  - `Flags` bit 27: In Main Ship
  - `Flags` bit 28: In Fighter
  - `Flags2` bit 0: On Foot
- If `Flags == 0` or `Status.json` has not updated with active gameplay telemetry (e.g. title/loading screen), MFD rendering is completely suppressed.
- Per-slot visibility masks allow MFDs to display selectively (e.g. only in Ship, only in SRV, or only in Flight).

### 2. Extensible Slot Architecture
- `MfdSlot` encapsulates `MfdPose`, `MfdGazeTracker`, `IMfdProvider`, `MfdRenderer`, and per-screen config settings.
- Slots are managed in a dynamic vector in `MfdManager` with lookup by unique ID (`main_mfd`, `left_mfd`, `right_mfd`, or custom user slots).

### 3. Gaze Tracking & Gaze Delay
- `MfdGazeTracker` tests ray-plane intersection from seated eye pose against each MFD quad.
- Gaze activation timer (`m_dwellTimer`) ensures the pilot's gaze must linger within the threshold before transitioning `kUnfocused -> kFocused`.
- Gaze-activated visibility: if `slot.autoHideUntilGaze` is enabled, alpha renders at $0\%$ until dwell timer confirms focus, then smoothly transitions up.

### 4. DirectInput & vJoy Capture
- In addition to standard DirectInput keyboard filtering (`inputGateSetPrivate`), DirectInput joystick polling / vJoy state must be inspected or intercepted for bound UI navigation axes/buttons when `focusState == 2`.

### 5. Interactive In-MFD Settings Tab
- When navigating the `SETTINGS` tab, Up/Down selects setting row (e.g. `Position X`, `Position Y`, `Position Z`, `Pitch Tilt`, `Yaw Angle`, `Opacity`, `Color Theme`, `Gaze Delay`, `Auto-Hide`).
- Left/Right adjusts value increments in real-time. Changes write immediately to `slot.pose` / `slot.settings` and mirror to `edvr.ini`.
