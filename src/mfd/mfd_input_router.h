#pragma once

#include "mfd_types.h"
#include "mfd_provider.h"
#include <cstdint>
#include <vector>

namespace edvr::mfd {

// Virtual-key and button mappings for cockpit UI navigation.
struct MfdKeyMapping {
    int vkUp = 0x26;        // VK_UP (or 'W')
    int vkDown = 0x28;      // VK_DOWN (or 'S')
    int vkLeft = 0x25;      // VK_LEFT (or 'A')
    int vkRight = 0x27;     // VK_RIGHT (or 'D')
    int vkSelect = 0x20;    // VK_SPACE (or VK_RETURN)
    int vkBack = 0x08;      // VK_BACK (or VK_ESCAPE)
    int vkNextTab = 'E';    // 'E' (or CycleNextPage)
    int vkPrevTab = 'Q';    // 'Q' (or CyclePreviousPage)

    // Secondary aliases (e.g. keyboard W/S/A/D alongside arrow keys):
    int vkUpAlt = 'W';
    int vkDownAlt = 'S';
    int vkLeftAlt = 'A';
    int vkRightAlt = 'D';
    int vkSelectAlt = 0x0D; // VK_RETURN
    int vkBackAlt = 0x1B;   // VK_ESCAPE
};

// Routes UI navigation inputs to the focused MFD while suppressing
// them from the game engine so flight controls (pips, thrusters) remain safe.
class MfdInputRouter {
public:
    MfdInputRouter();

    void setKeyMapping(const MfdKeyMapping& mapping) { m_mapping = mapping; }
    const MfdKeyMapping& keyMapping() const { return m_mapping; }

    // Edge-triggered button update:
    // Takes raw down states (from GetAsyncKeyState, DirectInput, or XInput),
    // detects rising edges, and if mfdFocused is true, dispatches actions
    // to provider and returns true (indicating input was swallowed).
    bool processInput(bool isMfdFocused, IMfdProvider* activeProvider,
                      bool keyDownUp, bool keyDownDown, bool keyDownLeft, bool keyDownRight,
                      bool keyDownSelect, bool keyDownBack, bool keyDownNextTab, bool keyDownPrevTab);

    // Convenience poller using virtual keys and external key-state checker.
    // isKeyDownFn is a predicate (e.g. [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; })
    template <typename Fn>
    bool pollAndRoute(bool isMfdFocused, IMfdProvider* activeProvider, Fn&& isKeyDownFn) {
        bool up = isKeyDownFn(m_mapping.vkUp) || isKeyDownFn(m_mapping.vkUpAlt);
        bool down = isKeyDownFn(m_mapping.vkDown) || isKeyDownFn(m_mapping.vkDownAlt);
        bool left = isKeyDownFn(m_mapping.vkLeft) || isKeyDownFn(m_mapping.vkLeftAlt);
        bool right = isKeyDownFn(m_mapping.vkRight) || isKeyDownFn(m_mapping.vkRightAlt);
        bool sel = isKeyDownFn(m_mapping.vkSelect) || isKeyDownFn(m_mapping.vkSelectAlt);
        bool back = isKeyDownFn(m_mapping.vkBack) || isKeyDownFn(m_mapping.vkBackAlt);
        bool next = isKeyDownFn(m_mapping.vkNextTab);
        bool prev = isKeyDownFn(m_mapping.vkPrevTab);

        return processInput(isMfdFocused, activeProvider, up, down, left, right, sel, back, next, prev);
    }

    // Indicates whether the most recent input tick swallowed game controls.
    bool lastInputSwallowed() const { return m_lastSwallowed; }

private:
    MfdKeyMapping m_mapping;

    // Previous frame down states for rising-edge detection:
    bool m_prevUp = false;
    bool m_prevDown = false;
    bool m_prevLeft = false;
    bool m_prevRight = false;
    bool m_prevSelect = false;
    bool m_prevBack = false;
    bool m_prevNextTab = false;
    bool m_prevPrevTab = false;

    bool m_lastSwallowed = false;
};

} // namespace edvr::mfd
