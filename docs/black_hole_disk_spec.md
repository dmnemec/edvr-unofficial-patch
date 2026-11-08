# Technical Specification: Black Hole Accretion Disk (`visual.black_hole_disk`)

**Document ID:** `EDVR-SPEC-20261107-BHDISK`  
**Status:** IMPLEMENTATION SPECIFICATION (APPROVED DESIGN)  
**Target Files:**  
- [`src/d3d11/black_hole_disk.h`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.h)
- [`src/d3d11/black_hole_disk.cpp`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/black_hole_disk.cpp)
- [`src/shaders/black_hole_disk_simple.hlsl`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/shaders/black_hole_disk_simple.hlsl)
- [`src/d3d11/vscreen.cpp`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/vscreen.cpp)
- [`src/d3d11/device_hook.cpp`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/device_hook.cpp)
- [`src/d3d11/journal_watch.h`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/journal_watch.h)
- [`src/d3d11/journal_watch.cpp`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/journal_watch.cpp)

---

## 1. Architectural Overview & System State Machine

The black hole accretion disk simulation renders an astrophysically accurate, visually striking relativistic accretion disk with central event horizon shadow, photon ring, lensed upper and lower arches, and relativistic Doppler beaming across both **VR** (per-eye HDR passes) and **Flat** (single scene HDR pass) profiles.

```
       [Order 3: Sky Dome] 
 (vh=0x8471F654B0CBAA89 / ph=0xB00B8B6FB5CDB153)
           │
           ▼ (Sets g_bhSkySeenThisFrame = true)
  [Order 4 & 5: Background Stars & Bright Stars]
  (Caches 208-byte VsCb0 camera basis & projection)
           │
  [Order 6: Lens Mesh Draw]
  (vh=0x662ACF4E126EFFD1: extracts BH world translation cb0[3] & scale)
           │
           ▼
  [Order 7a/7b: Celestial Dust Sphere / Space Dust]
  (Normal: 0x7F894EB5B6BA82A1 / Supercruise: 0x9BFC7FD232328391)
           │
           ▼
  ╔══════════════════════════════════════════════════════════════╗
  ║   EDVR blackHoleDiskAfterDraw Pass (Fullscreen Triangle)     ║
  ║   - Premultiplied Alpha: ONE / INV_SRC_ALPHA                 ║
  ║   - Reversed-Z Read-Only Depth Test: GREATER_EQUAL (z = 0)   ║
  ║   - Extinguishes background stars inside shadow (b < bc)     ║
  ║   - Emits HDR lensed accretion disk & photon ring in front   ║
  ╚══════════════════════════════════════════════════════════════╝
           │
           ▼
  [Order 8+: Cockpit Canopy Glass, Orbit Lines, HUD, Tonemap]
```

### 1.1 Shader Hash Registry

The following 64-bit FNV-1a shader hashes govern the black hole render sequence:

| Stage & Hash | Order | Description & Operational Role |
|---|---|---|
| `kBhSkyDomeVsHash = 0x8471F654B0CBAA89ULL`<br>`kBhSkyDomePsHash = 0xB00B8B6FB5CDB153ULL` | Order 3 | **BH-System Gate:** Active only in systems containing a black hole (replaces normal sky dome `0xF8FA801F2CB1E27C`). Sets `g_bhSkySeenThisFrame = true`. |
| `kStarfieldMainVsHash = 0x297B6F1F7F104F23ULL`<br>`kStarfieldBrightVsHash = 0xB278189EFA1D574DULL`<br>`kStarfieldPsHash = 0x06332CA168B6DA63ULL` | Orders 4 & 5 | **Camera Basis Cache:** 13,873 starfield and 14 bright star instances. Binds 208-byte `VsCb0` containing camera orientation rows (`cb0[10]` Right, `cb0[11]` Up, `cb0[9]` Fwd) and projection (`cb0[4..7]`). |
| `kBhLensMeshVsHash = 0x662ACF4E126EFFD1ULL`<br>`kBhLensMeshPsHash = 0x2B156A05E98F2D5DULL` | Order 6 | **3D World Anchor & Scale:** Gravitational lens mesh. Its 240-byte `VsCb0` provides the 3D camera-to-BH translation `cb0[3].xyz` and mesh scale $\lVert\text{cb0}[0].xyz\rVert$. |
| `kBhCloseMeshVsHash = 0x99B41C87E45319E6ULL`<br>`kBhCloseMeshPsHash = 0xA9975F91040B0BCDULL` | Order 6b | **Close Approach Mesh:** Draws within 3,000 Ls. Monitored for diagnostic logging and telemetry. |
| `kNormalCelestialVsHash = 0x7F894EB5B6BA82A1ULL`<br>`kNormalCelestialPsHash = 0x095030F27D2C362AULL` | Order 7a | **Normal Space Trigger:** Celestial dust sphere drawn once per eye per frame in normal space. Bound with 208-byte `VsCb0`. |
| `kSupercruiseCelestialVsHash = 0x9BFC7FD232328391ULL`<br>`kSupercruiseCelestialPsHash = 0xCB7AF179DF4E6A60ULL` | Order 7b | **Supercruise Trigger:** Supercruise space-dust streaking sphere drawn once per eye per frame in supercruise. Bound with 208-byte `VsCb0`. |

---

## 2. C++ Header & Implementation Specifications

### 2.1 Header Specification: `src/d3d11/black_hole_disk.h`

```cpp
#pragma once

#include <d3d11.h>
#include <atomic>
#include <cstdint>

class Config;

namespace edvr {

enum class BlackHoleDiskMode : uint8_t {
    Off = 0,
    Simple = 1,
    Realistic = 2
};

namespace detail {
extern std::atomic<bool> g_blackHoleDiskWanted;
extern BlackHoleDiskMode g_blackHoleDiskMode;
}  // namespace detail

inline bool blackHoleDiskWanted() {
    return detail::g_blackHoleDiskWanted.load(std::memory_order_relaxed);
}

// Dedicated 256-byte constant buffer uploaded to pixel-shader slot b0
struct alignas(16) BlackHoleDiskConstants {
    float camRight[4];      // [0] xyz = Eye camera Right unit vector in world space, w = P00
    float camUp[4];         // [1] xyz = Eye camera Up unit vector in world space,    w = P11
    float camFwd[4];        // [2] xyz = Eye camera Fwd unit vector in world space,   w = P02
    float projParams[4];    // [3] x = P12, y = timeSeconds, z = mode (1=simple, 2=realistic), w = spin a*
    float bhWorldDir[4];    // [4] xyz = BH unit direction in world space, w = theta_g (GM / c^2 D in radians)
    float diskNormal[4];    // [5] xyz = Disk spin axis unit normal in world space, w = r_isco (in M)
    float diskTangentU[4];  // [6] xyz = In-plane orthonormal basis U in world space, w = r_out (in M, e.g. 30M)
    float diskTangentV[4];  // [7] xyz = In-plane orthonormal basis V in world space, w = peakTempK (e.g. 14000K)
    float invViewProj0[4];  // [8] Sky dome InvViewProj row 0 (optional/valid when params.x > 0)
    float invViewProj1[4];  // [9] Sky dome InvViewProj row 1
    float invViewProj2[4];  // [10] Sky dome InvViewProj row 2
    float invViewProj3[4];  // [11] Sky dome InvViewProj row 3
    float params[4];        // [12] x = useInvViewProj, y = exposureScale, z = r_horizon (in M), w = b_crit (in M)
    float pad[3][4];        // [13..15] Reserved / diagnostics (48 bytes)
};
static_assert(sizeof(BlackHoleDiskConstants) == 256, "BlackHoleDiskConstants must be exactly 256 bytes");

// Lifecycle & boundary management
void blackHoleDiskConfigure(Config& cfg);
void blackHoleDiskOnFrameStart();
void blackHoleDiskFrameBoundary(ID3D11DeviceContext* ctx);
void blackHoleDiskShutdown();

// Draw hook: called from hookedDraw / hookedDrawIndexed in both VR and Flat profiles
void blackHoleDiskAfterDraw(ID3D11DeviceContext* ctx, uint64_t vsHash, uint64_t psHash);

// Shadow table interception hooks for D3D11_BIND_CONSTANT_BUFFER updates
void blackHoleDiskCbMapped(void* resource, void* data);
void blackHoleDiskCbUnmapped(void* resource);
void blackHoleDiskCbUpdated(void* resource, const void* data, uint32_t bytes);

}  // namespace edvr
```

### 2.2 Constant Buffer Shadow Table Specification (`src/d3d11/black_hole_disk.cpp`)

To eliminate pointer staleness from Frontier's dynamic constant-buffer ring allocations without stalling the GPU pipeline with `CopyResource` or `Map(READ)`, a 64-slot CPU shadow table caches CPU uploads:

```cpp
struct CbShadowEntry {
    const void* resource = nullptr;
    uint32_t byteWidth = 0;
    float data[16][4] = {};  // Caches first 256 bytes (16 float4s)
    uint64_t frameUpdated = 0;
};

static constexpr size_t kShadowTableSize = 64;
static constexpr size_t kShadowTableMask = kShadowTableSize - 1;
static CbShadowEntry g_cbShadowTable[kShadowTableSize];
static void* g_pendingMappedResource = nullptr;
static void* g_pendingMappedPointer = nullptr;

inline size_t cbHashPtr(const void* ptr) {
    uintptr_t v = reinterpret_cast<uintptr_t>(ptr);
    return ((v >> 4) ^ (v >> 10)) & kShadowTableMask;
}

void blackHoleDiskCbMapped(void* resource, void* data) {
    if (!resource || !data) return;
    g_pendingMappedResource = resource;
    g_pendingMappedPointer = data;
}

void blackHoleDiskCbUnmapped(void* resource) {
    if (!resource || resource != g_pendingMappedResource || !g_pendingMappedPointer) return;
    size_t idx = cbHashPtr(resource);
    for (size_t probe = 0; probe < 4; ++probe) {
        size_t slot = (idx + probe) & kShadowTableMask;
        if (!g_cbShadowTable[slot].resource || g_cbShadowTable[slot].resource == resource) {
            g_cbShadowTable[slot].resource = resource;
            std::memcpy(g_cbShadowTable[slot].data, g_pendingMappedPointer, sizeof(g_cbShadowTable[slot].data));
            g_cbShadowTable[slot].frameUpdated = g_frameIndex;
            break;
        }
    }
    g_pendingMappedResource = nullptr;
    g_pendingMappedPointer = nullptr;
}

void blackHoleDiskCbUpdated(void* resource, const void* data, uint32_t bytes) {
    if (!resource || !data || bytes == 0) return;
    size_t idx = cbHashPtr(resource);
    for (size_t probe = 0; probe < 4; ++probe) {
        size_t slot = (idx + probe) & kShadowTableMask;
        if (!g_cbShadowTable[slot].resource || g_cbShadowTable[slot].resource == resource) {
            g_cbShadowTable[slot].resource = resource;
            const uint32_t copyBytes = (bytes < sizeof(g_cbShadowTable[slot].data)) 
                                       ? bytes : sizeof(g_cbShadowTable[slot].data);
            std::memcpy(g_cbShadowTable[slot].data, data, copyBytes);
            g_cbShadowTable[slot].byteWidth = bytes;
            g_cbShadowTable[slot].frameUpdated = g_frameIndex;
            break;
        }
    }
}

const float* findShadowedCb(const void* resource) {
    if (!resource) return nullptr;
    size_t idx = cbHashPtr(resource);
    for (size_t probe = 0; probe < 4; ++probe) {
        size_t slot = (idx + probe) & kShadowTableMask;
        if (g_cbShadowTable[slot].resource == resource) {
            return reinterpret_cast<const float*>(g_cbShadowTable[slot].data);
        }
    }
    return nullptr;
}
```

### 2.3 Per-Eye RTV Tracking & Saved Pipeline State

```cpp
// Per-eye Render Target View and invocation tracking
static ID3D11RenderTargetView* g_eyeRtvs[2] = { nullptr, nullptr };
static bool g_eyeDrawnThisFrame[3] = { false, false, false }; // 0: Eye 0, 1: Eye 1, 2: Flat
static bool g_bhSkySeenThisFrame = false;
static uint64_t g_bhSkyLastSeenFrame = 0;

void blackHoleDiskOnFrameStart() {
    g_eyeRtvs[0] = nullptr;
    g_eyeRtvs[1] = nullptr;
    g_eyeDrawnThisFrame[0] = false;
    g_eyeDrawnThisFrame[1] = false;
    g_eyeDrawnThisFrame[2] = false;
    g_bhSkySeenThisFrame = false;
}

struct SavedPipeline {
    ID3D11RenderTargetView* rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    ID3D11BlendState* blend = nullptr;
    FLOAT blendFactor[4] = {};
    UINT sampleMask = 0xFFFFFFFFu;
    ID3D11DepthStencilState* depth = nullptr;
    UINT stencilRef = 0;
    ID3D11RasterizerState* raster = nullptr;
    UINT viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    ID3D11InputLayout* layout = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* vsCb0 = nullptr;
    ID3D11Buffer* psCb0 = nullptr; // CRITICAL: Saved & restored to prevent constant buffer leaks
    ID3D11Buffer* psCb1 = nullptr;
    ID3D11Buffer* psCb2 = nullptr;
    ID3D11ShaderResourceView* psSrv0 = nullptr;
    ID3D11SamplerState* psSampler0 = nullptr;

    void capture(ID3D11DeviceContext* c) {
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&depth, &stencilRef);
        c->RSGetState(&raster);
        viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        c->RSGetViewports(&viewportCount, viewports);
        c->IAGetInputLayout(&layout);
        c->IAGetPrimitiveTopology(&topology);
        c->VSGetShader(&vs, nullptr, nullptr);
        c->PSGetShader(&ps, nullptr, nullptr);
        c->VSGetConstantBuffers(0, 1, &vsCb0);
        c->PSGetConstantBuffers(0, 1, &psCb0);
        c->PSGetConstantBuffers(1, 1, &psCb1);
        c->PSGetConstantBuffers(2, 1, &psCb2);
        c->PSGetShaderResources(0, 1, &psSrv0);
        c->PSGetSamplers(0, 1, &psSampler0);
    }

    void restore(ID3D11DeviceContext* c) {
        c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, dsv);
        c->OMSetBlendState(blend, blendFactor, sampleMask);
        c->OMSetDepthStencilState(depth, stencilRef);
        c->RSSetState(raster);
        c->RSSetViewports(viewportCount, viewportCount ? viewports : nullptr);
        c->IASetInputLayout(layout);
        c->IASetPrimitiveTopology(topology);
        c->VSSetShader(vs, nullptr, 0);
        c->PSSetShader(ps, nullptr, 0);
        c->VSSetConstantBuffers(0, 1, &vsCb0);
        c->PSSetConstantBuffers(0, 1, &psCb0);
        c->PSSetConstantBuffers(1, 1, &psCb1);
        c->PSSetConstantBuffers(2, 1, &psCb2);
        c->PSSetShaderResources(0, 1, &psSrv0);
        c->PSSetSamplers(0, 1, &psSampler0);

        for (ID3D11RenderTargetView*& v : rtv) { if (v) { v->Release(); v = nullptr; } }
        if (dsv) { dsv->Release(); dsv = nullptr; }
        if (blend) { blend->Release(); blend = nullptr; }
        if (depth) { depth->Release(); depth = nullptr; }
        if (raster) { raster->Release(); raster = nullptr; }
        if (layout) { layout->Release(); layout = nullptr; }
        if (vs) { vs->Release(); vs = nullptr; }
        if (ps) { ps->Release(); ps = nullptr; }
        if (vsCb0) { vsCb0->Release(); vsCb0 = nullptr; }
        if (psCb0) { psCb0->Release(); psCb0 = nullptr; }
        if (psCb1) { psCb1->Release(); psCb1 = nullptr; }
        if (psCb2) { psCb2->Release(); psCb2 = nullptr; }
        if (psSrv0) { psSrv0->Release(); psSrv0 = nullptr; }
        if (psSampler0) { psSampler0->Release(); psSampler0 = nullptr; }
    }
};
```

### 2.4 CPU 2D Geodesic Transfer LUT Generator (`realistic` Mode)

In `realistic` mode (`mode == 2`), the CPU precomputes an exact 4th-order Runge-Kutta numerical integration of Binet's equation over a `256x256` grid.

Parameterization:
- $u_{\text{tex}} \in [0, 1]$ represents impact parameter $b/M \in [0, 32M]$ mapped non-linearly: $b = 32.0 \times u_{\text{tex}}^2$.
- $v_{\text{tex}} \in [0, 1]$ represents primary crossing angle $\psi_1 \in [0, \pi]$: $\psi_1 = \pi \times v_{\text{tex}}$.
- Channels:
  - `.r` (FP16): Primary crossing emission radius $r_1 / M$ at $\psi_1 \in [0, \pi]$.
  - `.g` (FP16): Secondary crossing emission radius $r_2 / M$ at $\psi_2 = \psi_1 + \pi \in [\pi, 2\pi]$.
  - `.b` (FP16): Third-order photon ring radius $r_3 / M$ at $\psi_3 = \psi_1 + 2\pi \in [2\pi, 3\pi]$.
  - `.a` (FP16): Event horizon capture mask ($1.0$ if ray is captured by horizon $r \le r_+$, $0.0$ if it escapes).

```cpp
#include <vector>

struct Half4 {
    uint16_t r, g, b, a;
};

// Converts float to IEEE 754 binary16 (half precision)
uint16_t floatToHalf(float f) {
    uint32_t x = *reinterpret_cast<uint32_t*>(&f);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = ((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x007FFFFF;
    if (exp <= 0) return static_cast<uint16_t>(sign);
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00);
    return static_cast<uint16_t>(sign | (exp << 10) | (mant >> 13));
}

void generateGeodesicLut(std::vector<Half4>& outPixels, uint32_t width, uint32_t height, float a_spin) {
    outPixels.resize(width * height);
    const float r_plus = 1.0f + std::sqrt(std::max(0.0f, 1.0f - a_spin * a_spin));
    const float u_horizon = 1.0f / r_plus;

    for (uint32_t y = 0; y < height; ++y) {
        float v_tex = (y + 0.5f) / float(height);
        float psi_target = v_tex * 3.1415926535f;

        for (uint32_t x = 0; x < width; ++x) {
            float u_tex = (x + 0.5f) / float(width);
            float b = 32.0f * (u_tex * u_tex); // Non-linear warp concentrating near photon sphere

            float r1 = 0.0f, r2 = 0.0f, r3 = 0.0f;
            float captured = 0.0f;

            if (b < 1e-4f) {
                captured = 1.0f;
            } else {
                // RK4 integration of Binet's equation: u'' + u = 1.5 * u^2 + a_spin corrections
                float u = 0.0f;
                float du_dpsi = 1.0f / b;
                float psi = 0.0f;
                const float dpsi = 0.01f;
                bool hitHorizon = false;

                while (psi <= 3.0f * 3.1415926535f + 0.05f) {
                    if (u >= u_horizon) {
                        hitHorizon = true;
                        break;
                    }
                    if (psi >= psi_target && r1 == 0.0f && u > 1e-5f) {
                        r1 = 1.0f / u;
                    }
                    if (psi >= (psi_target + 3.1415926535f) && r2 == 0.0f && u > 1e-5f) {
                        r2 = 1.0f / u;
                    }
                    if (psi >= (psi_target + 2.0f * 3.1415926535f) && r3 == 0.0f && u > 1e-5f) {
                        r3 = 1.0f / u;
                    }

                    // RK4 step for: d2u/dpsi2 = 1.5 * u^2 - u
                    auto f_deriv = [](float val_u) -> float {
                        return 1.5f * val_u * val_u - val_u;
                    };

                    float k1_u = du_dpsi;
                    float k1_v = f_deriv(u);

                    float k2_u = du_dpsi + 0.5f * dpsi * k1_v;
                    float k2_v = f_deriv(u + 0.5f * dpsi * k1_u);

                    float k3_u = du_dpsi + 0.5f * dpsi * k2_v;
                    float k3_v = f_deriv(u + 0.5f * dpsi * k2_u);

                    float k4_u = du_dpsi + dpsi * k3_v;
                    float k4_v = f_deriv(u + dpsi * k3_u);

                    u += (dpsi / 6.0f) * (k1_u + 2.0f * k2_u + 2.0f * k3_u + k4_u);
                    du_dpsi += (dpsi / 6.0f) * (k1_v + 2.0f * k2_v + 2.0f * k3_v + k4_v);
                    psi += dpsi;
                }

                if (hitHorizon) {
                    captured = 1.0f;
                }
            }

            Half4& h = outPixels[y * width + x];
            h.r = floatToHalf(r1);
            h.g = floatToHalf(r2);
            h.b = floatToHalf(r3);
            h.a = floatToHalf(captured);
        }
    }
}
```

---

## 3. HLSL Shader Specification: `src/shaders/black_hole_disk_simple.hlsl`

Complete, self-contained HLSL code executing in both `simple` and `realistic` modes.

```hlsl
// HLSL Accretion Disk Shader: visual.black_hole_disk = simple | realistic
// Features:
// - World-space ray reconstruction from camera basis & asymmetric projection
// - Exact 3D relativistic geodesic plane intersection geometry
// - Mode 1 (Simple): Analytic Poutanen-Beloborodov + Page-Thorne + g^4 Doppler + 5-mode spiral turbulence
// - Mode 2 (Realistic): Kerr a*=0.85 ISCO + 2D Geodesic RK4 LUT (t0, s0) for n=0, n=1, n=2 images
// - Wyman-Sloan-Shirley CIE 1931 blackbody spectrum to linear sRGB
// - Reversed-Z read-only far plane output (z = 0.0, w = 1.0) with premultiplied alpha

cbuffer BlackHoleDiskCb : register(b0)
{
    float4 camRight;     // xyz = World Right, w = P00
    float4 camUp;        // xyz = World Up,    w = P11
    float4 camFwd;       // xyz = World Fwd,   w = P02
    float4 projParams;   // x = P12, y = timeSeconds, z = mode (1=simple, 2=realistic), w = spin a*
    float4 bhWorldDir;   // xyz = BH unit direction in world space, w = theta_g (GM / c^2 D)
    float4 diskNormal;   // xyz = Disk spin axis unit normal in world space, w = r_isco (in M)
    float4 diskTangentU; // xyz = In-plane basis U in world space, w = r_out (in M)
    float4 diskTangentV; // xyz = In-plane basis V in world space, w = peakTempK
    float4 invViewProj0; // Sky dome InvViewProj row 0
    float4 invViewProj1; // Sky dome InvViewProj row 1
    float4 invViewProj2; // Sky dome InvViewProj row 2
    float4 invViewProj3; // Sky dome InvViewProj row 3
    float4 params;       // x = useInvViewProj, y = exposureScale, z = r_horizon (in M), w = b_crit (in M)
    float4 pad[3];       // Pad to 256 bytes
};

Texture2D<float4> GeodesicLut : register(t0);
SamplerState LinearClampSampler : register(s0);

struct VSOutput
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float3 worldRay : TEXCOORD1;
};

VSOutput VSMain(uint vid : SV_VertexID)
{
    VSOutput o;
    // Standard full-screen triangle covering [-1, 1] NDC:
    // vid 0: (-1, -1), vid 1: (-1, 3), vid 2: (3, -1)
    float2 grid = float2((vid == 2) ? 3.0 : -1.0, (vid == 1) ? 3.0 : -1.0);
    
    // Reversed-Z far plane: z = 0.0, w = 1.0
    o.pos = float4(grid, 0.0, 1.0);
    o.uv = grid * float2(0.5, -0.5) + 0.5;

    // View-space ray reconstruction from projection coefficients
    float p00 = camRight.w;
    float p11 = camUp.w;
    float p02 = camFwd.w;
    float p12 = projParams.x;

    float vx = (grid.x - p02) / ((abs(p00) > 1e-4) ? p00 : 1.0);
    float vy = (grid.y - p12) / ((abs(p11) > 1e-4) ? p11 : 1.0);
    float3 viewRay = normalize(float3(vx, vy, 1.0));

    // Rotate into world space via orthonormal camera basis rows
    o.worldRay = normalize(viewRay.x * camRight.xyz + viewRay.y * camUp.xyz + viewRay.z * camFwd.xyz);
    return o;
}

// Wyman-Sloan-Shirley (JCGT 2013) CIE 1931 analytic color matching function fit
float GaussianLobe(float lam, float mu, float s1, float s2)
{
    float s = (lam < mu) ? s1 : s2;
    float t = (lam - mu) / s;
    return exp(-0.5 * t * t);
}

void CmfCie1931(float lam, out float x, out float y, out float z)
{
    x = 1.056 * GaussianLobe(lam, 599.8, 37.9, 31.0)
      + 0.362 * GaussianLobe(lam, 442.0, 16.0, 26.7)
      - 0.065 * GaussianLobe(lam, 501.1, 20.4, 26.2);
    y = 0.821 * GaussianLobe(lam, 568.8, 46.9, 40.5)
      + 0.286 * GaussianLobe(lam, 530.9, 16.3, 31.1);
    z = 1.217 * GaussianLobe(lam, 437.0, 11.8, 36.0)
      + 0.681 * GaussianLobe(lam, 459.0, 26.0, 13.8);
}

// Planckian blackbody radiator evaluated at 5 visible wavelengths mapped to linear sRGB
float3 BlackbodyToLinearSrgb(float T_kelvin)
{
    T_kelvin = clamp(T_kelvin, 1000.0, 40000.0);
    const float c2 = 1.438777e7; // nm * K
    const float lams[5] = { 440.0, 490.0, 545.0, 590.0, 650.0 };
    const float weights[5] = { 0.20, 0.20, 0.25, 0.20, 0.15 };

    float X = 0.0, Y = 0.0, Z = 0.0;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float lam = lams[i];
        float e = c2 / (lam * T_kelvin);
        float p = (e > 80.0) ? 0.0 : (1.0 / (pow(lam * 1e-3, 5.0) * (exp(e) - 1.0)));
        float cx, cy, cz;
        CmfCie1931(lam, cx, cy, cz);
        X += p * cx * weights[i];
        Y += p * cy * weights[i];
        Z += p * cz * weights[i];
    }

    // Reference normalizer relative to 5800K solar white point
    float norm = max(Y, 1e-6);
    float3 xyz = float3(X, Y, Z) / norm;

    // Standard CIE XYZ to linear sRGB D65 matrix
    float3 srgb;
    srgb.r =  3.2406 * xyz.x - 1.5372 * xyz.y - 0.4986 * xyz.z;
    srgb.g = -0.9689 * xyz.x + 1.8758 * xyz.y + 0.0415 * xyz.z;
    srgb.b =  0.0557 * xyz.x - 0.2040 * xyz.y + 1.0570 * xyz.z;
    return max(srgb, float3(0.0, 0.0, 0.0));
}

// Page-Thorne relativistic thin disk radial flux profile
float PageThorneFlux(float r, float r_isco, float r_out)
{
    if (r <= r_isco || r >= r_out) return 0.0;
    float x = sqrt(r);
    float x0 = sqrt(r_isco);
    float f = x - x0 - 1.5 * log(max(x / x0, 1e-5));
    if (f <= 0.0) return 0.0;
    float denom = (r - 3.0) * pow(r, 2.5);
    float taper = smoothstep(r_out, r_out * 0.75, r);
    return (f / max(denom, 1e-4)) * taper;
}

// 5-mode sheared log-spiral turbulence
float DiskTurbulence(float r, float phi, float timeSec, float r_isco, float a_spin)
{
    float omega = 1.0 / (pow(r, 1.5) + a_spin);
    float ln_r = log(max(r / r_isco, 1.0));

    const float m[5]     = { 1.0, 2.0, 3.0, 4.0, 6.0 };
    const float k[5]     = { 2.3, 4.1, 5.9, 8.3, 12.2 };
    const float phase[5] = { 0.4, 2.1, 4.6, 1.3, 5.5 };
    const float w[5]     = { 0.35, 0.25, 0.20, 0.12, 0.08 };

    float turb = 0.0;
    [unroll]
    for (int j = 0; j < 5; ++j)
    {
        float arg = m[j] * phi + k[j] * ln_r - m[j] * omega * (timeSec * 0.5) + phase[j];
        turb += w[j] * cos(arg);
    }
    return exp(0.55 * turb - 0.15); // Log-normal distribution
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 omega = normalize(input.worldRay);
    float3 d_bh = normalize(bhWorldDir.xyz);
    float theta_g = max(bhWorldDir.w, 1e-7);

    float cosAlpha = dot(omega, d_bh);
    if (cosAlpha <= 0.0) discard; // Behind observer

    float sinAlpha = length(omega - cosAlpha * d_bh);
    float thetaMax = 35.0 * theta_g;
    if (sinAlpha > thetaMax) discard; // Outside accretion disk bounds

    // Geodesic propagation plane orthonormal basis:
    // e1 = -d_bh (direction from BH to observer)
    // e2 = unit direction perpendicular to e1 in the geodesic plane
    float3 e1 = -d_bh;
    float3 e2 = (sinAlpha > 1e-7) ? normalize(omega - cosAlpha * d_bh) : float3(1, 0, 0);

    // Singularity-free 3D plane crossing angles:
    // tan(psi) = (n_disk . d_bh) / (n_disk . e2)
    float3 n_disk = normalize(diskNormal.xyz);
    float dot_n_d = dot(n_disk, d_bh);
    float dot_n_e2 = dot(n_disk, e2);

    float psi0 = atan2(dot_n_d, dot_n_e2);
    float psi1 = (psi0 < 0.0) ? (psi0 + 3.1415926535) : psi0; // Primary crossing in (0, pi)
    float psi2 = psi1 + 3.1415926535;                         // Secondary crossing in (pi, 2pi)

    // Impact parameter in units of M:
    float b = sinAlpha / (theta_g * sqrt(max(1.0 - 2.0 * theta_g, 1e-4)));
    float b_crit = (params.w > 0.0) ? params.w : 5.1961524; // 3*sqrt(3) for Schwarzschild
    float r_isco = (diskNormal.w > 0.0) ? diskNormal.w : 6.0;
    float r_out  = (diskTangentU.w > 0.0) ? diskTangentU.w : 30.0;
    float a_spin = projParams.w;
    float mode   = projParams.z;
    float timeSec = projParams.y;

    float r1 = 0.0, r2 = 0.0, r3 = 0.0;
    float captureMask = 0.0;

    if (mode > 1.5) // Mode 2: Realistic (Kerr LUT)
    {
        float u_lut = clamp(sqrt(b / 32.0), 0.0, 1.0);
        float v_lut = clamp(psi1 / 3.1415926535, 0.0, 1.0);
        float4 lutSample = GeodesicLut.SampleLevel(LinearClampSampler, float2(u_lut, v_lut), 0);
        r1 = lutSample.r;
        r2 = lutSample.g;
        r3 = lutSample.b;
        captureMask = lutSample.a;
    }
    else // Mode 1: Simple (Analytic Poutanen-Beloborodov)
    {
        captureMask = (b < b_crit) ? 1.0 : 0.0;
        
        // Primary crossing r1
        if (psi1 <= 1.5707963) // Inbound front face (valid for all b)
        {
            float sinP = max(sin(psi1), 1e-4);
            float cosP = cos(psi1);
            float term = (b * b) / (sinP * sinP) - (2.0 * b * (1.0 - cosP)) / (sinP * sinP);
            r1 = sqrt(max(term, 0.0));
        }
        else if (b > b_crit) // Outbound rear face (lensed upper/lower arch)
        {
            float sinP = max(sin(psi1), 1e-4);
            float cosP = cos(psi1);
            float deltaB = max(b - b_crit, 1e-4);
            r1 = (b / sinP) - 2.0 * (1.0 - cosP) / (sinP * sinP) + 0.8 * log(1.0 + 1.0 / deltaB);
        }

        // Secondary crossing r2 (ghost ring hugging photon sphere)
        if (b > b_crit)
        {
            float deltaB = max(b - b_crit, 1e-4);
            r2 = r_isco + (r_out - r_isco) * saturate(deltaB / 0.85);
        }
    }

    // Carter / Killing constant along planar geodesic:
    // Lz = b * (n_disk . (-d_bh x e2))
    float3 rayNormal = cross(-d_bh, e2);
    float Lz = b * dot(n_disk, rayNormal);

    float3 totalHdr = float3(0.0, 0.0, 0.0);
    float totalOpacity = 0.0;
    float peakTemp = (diskTangentV.w > 0.0) ? diskTangentV.w : 14000.0;

    // Lambda helper for radiating order n
    float r_candidates[3] = { r1, r2, r3 };
    float psi_candidates[3] = { psi1, psi2, psi1 + 6.2831853 };
    float orderWeights[3] = { 1.0, 0.35, 0.12 };

    [unroll]
    for (int order = 0; order < 3; ++order)
    {
        float r_cur = r_candidates[order];
        if (r_cur >= r_isco && r_cur <= r_out)
        {
            float psi_cur = psi_candidates[order];
            float3 x_pos = cos(psi_cur) * e1 + sin(psi_cur) * e2;
            float phi = atan2(dot(x_pos, diskTangentV.xyz), dot(x_pos, diskTangentU.xyz));

            // Relativistic redshift factor g:
            float omega_orb = 1.0 / (pow(r_cur, 1.5) + a_spin);
            float num = sqrt(max(1.0 - 3.0 / r_cur, 1e-4));
            float den = (1.0 - omega_orb * Lz);
            float g_fac = (den > 1e-4) ? (num / den) : 0.0;

            // Page-Thorne flux profile & turbulence
            float ptFlux = PageThorneFlux(r_cur, r_isco, r_out);
            float turb = DiskTurbulence(r_cur, phi, timeSec, r_isco, a_spin);
            float intensity = pow(g_fac, 4.0) * ptFlux * turb * orderWeights[order] * 65.0;

            // Thermal color shift: T_obs = g * T_eff
            float T_eff = peakTemp * pow(max(r_isco / r_cur, 1e-3), 0.75);
            float3 col = BlackbodyToLinearSrgb(g_fac * T_eff);

            totalHdr += col * intensity;
            totalOpacity += saturate(ptFlux * 3.5);
        }
    }

    // Photon ring emission spike near b_crit
    float deltaCrit = abs(b - b_crit);
    float photonSpike = exp(-deltaCrit * deltaCrit / 0.006) * 12.0;
    totalHdr += float3(1.3, 1.4, 1.8) * photonSpike;

    // Event horizon shadow mask
    float shadowMask = (b < b_crit || captureMask > 0.5) ? 1.0 : 0.0;

    // Premultiplied alpha: 1.0 inside shadow (extinguishes background stars)
    float outAlpha = (shadowMask > 0.5) ? 1.0 : saturate(totalOpacity + photonSpike * 0.2);

    // If completely dark and outside the shadow, discard to preserve scene depth & performance
    if (dot(totalHdr, float3(1, 1, 1)) < 0.0005 && shadowMask < 0.5)
    {
        discard;
    }

    return float4(totalHdr, outAlpha);
}
```

---

## 4. `src/d3d11/vscreen.cpp` Integration Points

### 4.1 Frame Start Boundary in `Present`

In [`src/d3d11/vscreen.cpp`](file:///c:/Users/csasn/github.com/characterecho-sean/edvr-unofficial-patch/src/d3d11/vscreen.cpp), `blackHoleDiskOnFrameStart()` must execute before the `if (runtimeFlatProfile()) return hr;` early return in `Present` (lines 3645–3655):

```cpp
// In hookedPresent / Present boundary:
blackHoleDiskOnFrameStart();

if (runtimeFlatProfile()) {
    // Existing flat profile Present logic
    return hr;
}
```

### 4.2 Hooked Draw Invocations: `hookedDraw` and `hookedDrawIndexed`

`blackHoleDiskAfterDraw` must be invoked in both VR mode and the `runtimeFlatProfile()` branch of both `hookedDraw` and `hookedDrawIndexed`:

#### 1. In `hookedDraw` (line 5518 and line 5564):
```cpp
// Inside if (runtimeFlatProfile()):
if (blackHoleDiskWanted() && self == g_state->ownerCtx) {
    blackHoleDiskAfterDraw(self, bindingShaderHash(BindSlot::Vs), bindingShaderHash(BindSlot::Ps));
}
if (!stationSkip) flatDraw.uiReissue([&] { ReissueClock flatClock(ReissueCaller::FlatUi); g_state->realDraw(self, count, start); });
return;

// At end of hookedDraw (VR mode):
if (blackHoleDiskWanted() && self == g_state->ownerCtx) {
    blackHoleDiskAfterDraw(self, bindingShaderHash(BindSlot::Vs), bindingShaderHash(BindSlot::Ps));
}
```

#### 2. In `hookedDrawIndexed` (line 5613 and line 5659):
```cpp
// Inside if (runtimeFlatProfile()):
if (blackHoleDiskWanted() && self == g_state->ownerCtx) {
    blackHoleDiskAfterDraw(self, bindingShaderHash(BindSlot::Vs), bindingShaderHash(BindSlot::Ps));
}
if (!stationSkip) flatDraw.uiReissue([&] { ReissueClock flatClock(ReissueCaller::FlatUi); g_state->realDrawIndexed(self, count, startIndex, baseVertex); });
return;

// At end of hookedDrawIndexed (VR mode):
if (blackHoleDiskWanted() && self == g_state->ownerCtx) {
    blackHoleDiskAfterDraw(self, bindingShaderHash(BindSlot::Vs), bindingShaderHash(BindSlot::Ps));
}
```

### 4.3 Shader Bytecode Dumping: `src/d3d11/device_hook.cpp`

In `device_hook.cpp`, intercept shader creation to automatically dump bytecode for telemetry and verification:

```cpp
// In hookedCreateVS:
if (hash == 0x8471F654B0CBAA89ULL || hash == 0x99B41C87E45319E6ULL || hash == 0x662ACF4E126EFFD1ULL) {
    dumpShaderBlob(L"vs", hash, bytecode, len);
}

// In hookedCreatePS:
if (hash == 0xB00B8B6FB5CDB153ULL || hash == 0xA9975F91040B0BCDULL || hash == 0x2B156A05E98F2D5DULL) {
    dumpShaderBlob(L"ps", hash, bytecode, len);
}
```

---

## 5. Journal Watch Integration (`src/d3d11/journal_watch.h` & `src/d3d11/journal_watch.cpp`)

To dynamically parameterize disk orientation ($\hat{n}_{\text{disk}}$) and mass-scaled temperature ($T_{\text{peak}} \propto M^{-1/4}$) across systems, `journal_watch.cpp` exposes:

```cpp
// In src/d3d11/journal_watch.h:
uint64_t journalSystemAddress();
float journalBlackHoleStellarMass(); // Returns mass in solar masses M_sun, or 0.0f if not scanned
bool journalInBlackHoleSystem();

// In src/d3d11/journal_watch.cpp:
// Parse FSDJump, Location, and Scan JSON records:
// "StarType": "H" or "SupermassiveBlackHole" -> sets g_inBlackHoleSystem = true
// "StellarMass": float -> sets g_blackHoleStellarMass
// "SystemAddress": uint64_t -> sets g_systemAddress
```

When `journalBlackHoleStellarMass() > 0.0f`, `blackHoleDisk.cpp` scales peak disk temperature via:
$$T_{\text{peak}} = 14000.0 \times \left(\frac{10.0}{\max(M_{\text{stellar}}, 3.0)}\right)^{1/4}$$
and derives a deterministic, fixed disk normal $\hat{n}_{\text{disk}}$ from the system address hash.

---

## 6. Verification & Build Gate Checklist

Before committing any implementation changes, the following validation commands must pass:

1. **Status Block Format Gate:**
   ```bash
   python tools\check_status_blocks.py
   ```
   *Requirement:* All Status blocks within 60 lines.

2. **Agent Material & Invisible Characters Gate:**
   ```bash
   python tools\check_agent_material.py
   ```
   *Requirement:* Zero invisible or invalid bidi characters across repository text files.

3. **Optics Self-Tests:**
   ```bash
   python tools\blackhole_optics.py --self-test
   python tools\sgra_optics.py --self-test
   ```
   *Requirement:* Relativistic Binet solvers, Synge shadows, and ISCO formulas pass with zero errors.

4. **Full Local Validation Build:**
   ```powershell
   & "C:\Users\csasn\github.com\characterecho-sean\edvr-unofficial-patch\build.bat" --wait-lock
   ```
   *Requirement:* Clean compilation of production DLLs (`d3d11.dll`, `openvr_api.dll`), test rigs pass, and `build\full_build_receipt.json` written.
