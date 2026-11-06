#pragma once

#include <d3d11.h>
#include <atomic>
#include <cstdint>

namespace edvr {

enum class BlackHoleDiskMode : uint8_t {
    Off,
    Simple,
    Realistic
};

namespace detail {
// Armed flag: visual.black_hole_disk != Off
extern std::atomic<bool> g_blackHoleDiskWanted;
extern BlackHoleDiskMode g_blackHoleDiskMode;
}  // namespace detail

// True while visual.black_hole_disk != off. One relaxed atomic load: the whole price of the feature while off.
inline bool blackHoleDiskWanted() {
    return detail::g_blackHoleDiskWanted.load(std::memory_order_relaxed);
}

// Frame boundary lifecycle: re-checks visual.black_hole_disk if config generation changed.
void blackHoleDiskFrameBoundary(ID3D11DeviceContext* ctx);

// After-draw hook in vscreen.cpp: detects the gravitational lens draw (vs=0x41E245D488BFE83E, ps=0x6EF82262EB12A037)
// and executes the unrolled accretion disk pass into the active eye HDR target.
void blackHoleDiskAfterDraw(ID3D11DeviceContext* ctx, uint64_t vsHash, uint64_t psHash);

// Shutdown / cleanup.
void blackHoleDiskShutdown();

}  // namespace edvr
