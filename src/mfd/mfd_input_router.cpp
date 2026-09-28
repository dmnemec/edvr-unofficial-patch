#include "mfd_input_router.h"
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

namespace edvr::mfd {

MfdInputRouter::MfdInputRouter() = default;

void MfdInputRouter::pollJoystickInputs(bool& up, bool& down, bool& left, bool& right,
                                        bool& sel, bool& back, bool& next, bool& prev) {
    UINT numDevs = joyGetNumDevs();
    if (numDevs > 16) numDevs = 16;
    for (UINT i = 0; i < numDevs; ++i) {
        JOYINFOEX ji{};
        ji.dwSize = sizeof(JOYINFOEX);
        ji.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(i, &ji) == JOYERR_NOERROR) {
            // Check POV Hat (dwPOV: hundredths of degrees, 65535 = centered)
            if (ji.dwPOV != 65535 && ji.dwPOV <= 35900) {
                if (ji.dwPOV >= 31500 || ji.dwPOV <= 4500) up = true;
                if (ji.dwPOV >= 4500 && ji.dwPOV <= 13500) right = true;
                if (ji.dwPOV >= 13500 && ji.dwPOV <= 22500) down = true;
                if (ji.dwPOV >= 22500 && ji.dwPOV <= 31500) left = true;
            }
            // Check standard D-pad buttons / Secondary menu buttons:
            // Button 1 (0x1) = Select/Fire
            // Button 2 (0x2) = Back/Cancel
            // Button 3 (0x4) = Prev Tab (Q)
            // Button 4 (0x8) = Next Tab (E)
            if (ji.dwButtons & 0x01) sel = true;
            if (ji.dwButtons & 0x02) back = true;
            if (ji.dwButtons & 0x04) prev = true;
            if (ji.dwButtons & 0x08) next = true;
        }
    }
}

bool MfdInputRouter::processInput(bool isMfdFocused, IMfdProvider* activeProvider,
                                 bool keyDownUp, bool keyDownDown, bool keyDownLeft, bool keyDownRight,
                                 bool keyDownSelect, bool keyDownBack, bool keyDownNextTab, bool keyDownPrevTab) {
    // Detect rising edges:
    bool edgeUp = keyDownUp && !m_prevUp;
    bool edgeDown = keyDownDown && !m_prevDown;
    bool edgeLeft = keyDownLeft && !m_prevLeft;
    bool edgeRight = keyDownRight && !m_prevRight;
    bool edgeSelect = keyDownSelect && !m_prevSelect;
    bool edgeBack = keyDownBack && !m_prevBack;
    bool edgeNextTab = keyDownNextTab && !m_prevNextTab;
    bool edgePrevTab = keyDownPrevTab && !m_prevPrevTab;

    // Update stored state:
    m_prevUp = keyDownUp;
    m_prevDown = keyDownDown;
    m_prevLeft = keyDownLeft;
    m_prevRight = keyDownRight;
    m_prevSelect = keyDownSelect;
    m_prevBack = keyDownBack;
    m_prevNextTab = keyDownNextTab;
    m_prevPrevTab = keyDownPrevTab;

    m_lastSwallowed = false;

    // If an MFD is not focused, inputs pass directly to game flight controls:
    if (!isMfdFocused) {
        return false;
    }

    // Build the action mask for this frame:
    MfdInputAction action = MfdInputAction::kNone;
    if (edgeUp) action = action | MfdInputAction::kUp;
    if (edgeDown) action = action | MfdInputAction::kDown;
    if (edgeLeft) action = action | MfdInputAction::kLeft;
    if (edgeRight) action = action | MfdInputAction::kRight;
    if (edgeSelect) action = action | MfdInputAction::kSelect;
    if (edgeBack) action = action | MfdInputAction::kBack;
    if (edgeNextTab) action = action | MfdInputAction::kNextTab;
    if (edgePrevTab) action = action | MfdInputAction::kPrevTab;

    bool anyKeyDown = (keyDownUp || keyDownDown || keyDownLeft || keyDownRight ||
                       keyDownSelect || keyDownBack || keyDownNextTab || keyDownPrevTab);

    if (action != MfdInputAction::kNone && activeProvider) {
        activeProvider->onInput(action);
    }

    // When focused, all UI navigation keys are swallowed to protect ship flight systems
    m_lastSwallowed = anyKeyDown;
    return m_lastSwallowed;
}

} // namespace edvr::mfd
