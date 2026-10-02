#pragma once

#include <cstdint>

namespace edvr {

// POD structure passed to hooks on the render thread.
// Zero-allocation, standard layout, O(1) reads.
struct FrameContext {
    uint32_t eyeWidth = 0;
    uint32_t eyeHeight = 0;
    uint32_t frameIndex = 0;
    bool stereoActive = false;
    bool fssActive = false;
    bool flatMode = false;
    bool cameraJitterActive = false;
    bool weaponMotionActive = false;
};

}  // namespace edvr
