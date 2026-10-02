#pragma once

#include <cstdint>

namespace edvr {

enum class DrawVerdict : uint8_t {
    kNone,
    kPanel,
    kSkip,
    kRemlok,
    kHolo,
    kTargetSharp,
    kNightVision,
    kIntroPanel,
    kGlareClamp,
    kGlareSteady,
    kParticle,
    kFssPanel,
    kFssReveal,
    kFssDump,
    kResolveBind,
    kQuadSkip,
    kLoaderPanel,
    kScrim,
    kBackdrop
};

}  // namespace edvr
