#include "mfd_input_router.h"

namespace edvr::mfd {

MfdInputRouter::MfdInputRouter() = default;

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
