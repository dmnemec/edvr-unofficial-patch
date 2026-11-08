#pragma once

#include <atomic>
#include <cstdint>
#include <d3d11.h>

class Config;

namespace edvr {

enum class BlackHoleDiskMode : uint8_t { Off = 0, Simple = 1, Realistic = 2 };

namespace detail {
extern std::atomic<bool> g_blackHoleDiskWanted;
extern BlackHoleDiskMode g_blackHoleDiskMode;
} // namespace detail

inline bool blackHoleDiskWanted() {
  return detail::g_blackHoleDiskWanted.load(std::memory_order_relaxed);
}

// Dedicated 256-byte constant buffer uploaded to pixel-shader slot b0
struct alignas(16) BlackHoleDiskConstants {
  float camRight[4]; // [0] xyz = Eye camera Right unit vector in world space, w
                     // = P00
  float camUp[4];  // [1] xyz = Eye camera Up unit vector in world space,    w =
                   // P11
  float camFwd[4]; // [2] xyz = Eye camera Fwd unit vector in world space,   w =
                   // P02
  float projParams[4]; // [3] x = P12, y = timeSeconds, z = mode (1=simple,
                       // 2=realistic), w = spin a*
  float bhWorldDir[4]; // [4] xyz = BH unit direction in world space, w =
                       // theta_g (GM / c^2 D in radians)
  float diskNormal[4]; // [5] xyz = Disk spin axis unit normal in world space, w
                       // = r_isco (in M)
  float diskTangentU[4]; // [6] xyz = In-plane orthonormal basis U in world
                         // space, w = r_out (in M, e.g. 30M)
  float diskTangentV[4]; // [7] xyz = In-plane orthonormal basis V in world
                         // space, w = peakTempK (e.g. 14000K)
  float invViewProj0[4]; // [8] Sky dome InvViewProj row 0 (optional/valid when
                         // params.x > 0)
  float invViewProj1[4]; // [9] Sky dome InvViewProj row 1
  float invViewProj2[4]; // [10] Sky dome InvViewProj row 2
  float invViewProj3[4]; // [11] Sky dome InvViewProj row 3
  float params[4]; // [12] x = useInvViewProj, y = exposureScale, z = r_horizon
                   // (in M), w = b_crit (in M)
  float pad[3][4]; // [13..15] Reserved / diagnostics (48 bytes)
};
static_assert(sizeof(BlackHoleDiskConstants) == 256,
              "BlackHoleDiskConstants must be exactly 256 bytes");

// Lifecycle & boundary management
void blackHoleDiskConfigure(Config &cfg);
void blackHoleDiskOnFrameStart();
void blackHoleDiskFrameBoundary(ID3D11DeviceContext *ctx);
void blackHoleDiskShutdown();

// Draw hook: called from hookedDraw / hookedDrawIndexed in both VR and Flat
// profiles
void blackHoleDiskAfterDraw(ID3D11DeviceContext *ctx, uint64_t vsHash,
                            uint64_t psHash);

// Shadow table interception hooks for D3D11_BIND_CONSTANT_BUFFER updates
void blackHoleDiskCbMapped(void *resource, void *data);
void blackHoleDiskCbUnmapped(void *resource);
void blackHoleDiskCbUpdated(void *resource, const void *data, uint32_t bytes);
void blackHoleDiskBufferInvalidated(const void* resource);

} // namespace edvr
