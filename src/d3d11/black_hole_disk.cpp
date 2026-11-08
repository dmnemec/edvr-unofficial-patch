// Black hole accretion disk simulation and rendering hook.
// visual.black_hole_disk = off | simple | realistic
// Docs: docs/black-hole-accretion-disk-2026-11-04.md

#include "black_hole_disk.h"
#include "black_hole_disk_shader.h"
#include "journal_watch.h"
#include "shader_swap.h"
#include "vr_world_route.h"
#include "vscreen.h"
#include "../common/config.h"
#include "../common/guard.h"
#include "../common/log.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace edvr {

namespace detail {
std::atomic<bool> g_blackHoleDiskWanted{false};
BlackHoleDiskMode g_blackHoleDiskMode = BlackHoleDiskMode::Off;
}  // namespace detail

namespace {

using Microsoft::WRL::ComPtr;

// Shader Hash Registry (Orders 3, 4, 5, 6, 6b, 7a, 7b)
constexpr uint64_t kBhSkyDomeVsHash = 0x8471F654B0CBAA89ULL;
constexpr uint64_t kBhSkyDomePsHash = 0xB00B8B6FB5CDB153ULL;

constexpr uint64_t kStarfieldMainVsHash = 0x297B6F1F7F104F23ULL;
constexpr uint64_t kStarfieldBrightVsHash = 0xB278189EFA1D574DULL;
constexpr uint64_t kStarfieldPsHash = 0x06332CA168B6DA63ULL;

constexpr uint64_t kBhLensMeshVsHash = 0x662ACF4E126EFFD1ULL;
constexpr uint64_t kBhLensMeshPsHash = 0x2B156A05E98F2D5DULL;

constexpr uint64_t kBhCloseMeshVsHash = 0x99B41C87E45319E6ULL;
constexpr uint64_t kBhCloseMeshPsHash = 0xA9975F91040B0BCDULL;

constexpr uint64_t kNormalCelestialVsHash = 0x7F894EB5B6BA82A1ULL;
constexpr uint64_t kNormalCelestialPsHash = 0x095030F27D2C362AULL;

constexpr uint64_t kSupercruiseCelestialVsHash = 0x9BFC7FD232328391ULL;
constexpr uint64_t kSupercruiseCelestialPsHash = 0xCB7AF179DF4E6A60ULL;

// Config tracking
bool g_configRead = false;
uint32_t g_configGeneration = 0;
std::string g_unrecognisedSaid;

// Device and pipeline resources
ID3D11Device* g_device = nullptr;
ComPtr<ID3D11VertexShader> g_vs;
ComPtr<ID3D11PixelShader>  g_ps;
ComPtr<ID3D11Buffer>       g_cbConstants;
ComPtr<ID3D11BlendState>   g_blendState;
ComPtr<ID3D11DepthStencilState> g_depthStencilState;
ComPtr<ID3D11RasterizerState>   g_rasterizerState;
ComPtr<ID3D11SamplerState> g_linearClampSampler;
ComPtr<ID3D11Texture2D>    g_geodesicLutTex;
ComPtr<ID3D11ShaderResourceView> g_geodesicLutSrv;

bool g_resourcesReady = false;
bool g_resourceFailedNoted = false;
FaultBudget g_budget("blackHoleDisk", 4);

// Constant buffer CPU-side shadow table (128 slots, 8-slot linear probe with LRU eviction)
struct CbShadowEntry {
    const void* resource = nullptr;
    bool valid = false;
    uint32_t byteWidth = 0;
    uint64_t lastDrawFrame = 0;
    uint64_t frameUpdated = 0;
    void* mapped = nullptr;
    float data[25][4] = {};  // Caches up to 400 bytes (25 float4s, matching CB2[25])
};

static constexpr size_t kShadowTableSize = 128;
static constexpr size_t kShadowTableMask = kShadowTableSize - 1;
static constexpr size_t kShadowProbe = 8;
static CbShadowEntry g_cbShadowTable[kShadowTableSize];
static uint64_t g_frameIndex = 0;

inline uint32_t cbSlotOf(const void* p) {
    return ((static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) >> 4) * 2654435761u) >> 7) & (kShadowTableSize - 1);
}

CbShadowEntry* findWatch(const void* res) {
    if (!res) return nullptr;
    uint32_t i = cbSlotOf(res);
    for (uint32_t k = 0; k < kShadowProbe; ++k) {
        CbShadowEntry& w = g_cbShadowTable[(i + k) & kShadowTableMask];
        if (w.resource == res) return &w;
        if (!w.resource) return nullptr;
    }
    return nullptr;
}

CbShadowEntry* addWatch(const void* res, uint64_t frame) {
    uint32_t i = cbSlotOf(res);
    CbShadowEntry* victim = nullptr;
    for (uint32_t k = 0; k < kShadowProbe; ++k) {
        CbShadowEntry& w = g_cbShadowTable[(i + k) & kShadowTableMask];
        if (!w.resource) { victim = &w; break; }
        if (!victim || w.lastDrawFrame < victim->lastDrawFrame) victim = &w;
    }
    victim->resource = res;
    victim->valid = false;
    victim->byteWidth = 0;
    victim->lastDrawFrame = frame;
    victim->frameUpdated = 0;
    victim->mapped = nullptr;
    return victim;
}

CbShadowEntry* shadowFor(ID3D11Buffer* buf) {
    if (!buf) return nullptr;
    CbShadowEntry* w = findWatch(buf);
    if (!w) {
        w = addWatch(buf, g_frameIndex);
        D3D11_BUFFER_DESC bd{};
        buf->GetDesc(&bd);
        w->byteWidth = bd.ByteWidth ? bd.ByteWidth : 1u;
        return w;
    }
    w->lastDrawFrame = g_frameIndex;
    if (w->byteWidth == 0) {
        D3D11_BUFFER_DESC bd{};
        buf->GetDesc(&bd);
        w->byteWidth = bd.ByteWidth ? bd.ByteWidth : 1u;
    }
    return w;
}

const float* findShadowedCb(const void* resource) {
    if (!resource) return nullptr;
    CbShadowEntry* w = findWatch(resource);
    if (w && w->valid) {
        return reinterpret_cast<const float*>(w->data);
    }
    return nullptr;
}

// Per-eye Render Target View and invocation tracking
static ID3D11RenderTargetView* g_eyeRtvs[2] = { nullptr, nullptr };
static bool g_eyeDrawnThisFrame[3] = { false, false, false }; // 0: Eye 0, 1: Eye 1, 2: Flat
static bool g_bhSkySeenThisFrame = false;
static uint64_t g_bhSkyLastSeenFrame = 0;

// Cached camera basis and projection from Orders 4/5
static float g_cachedCamRight[3][3] = {};
static float g_cachedCamUp[3][3] = {};
static float g_cachedCamFwd[3][3] = {};
static float g_cachedP00[3] = {};
static float g_cachedP11[3] = {};
static float g_cachedP02[3] = {};
static float g_cachedP12[3] = {};
static bool  g_hasCachedCamBasis[3] = { false, false, false };

// Sky dome per-eye InvViewProj from Order 3 (vsCb2 / psCb2)
static float g_skyInvViewProj[3][4][4] = {};
static bool  g_hasSkyInvViewProj[3] = { false, false, false };

// Latched black hole world anchor
static float g_latchedBhWorldDir[3] = { 0.0f, 0.0f, 1.0f };
static float g_latchedThetaG = 0.018f;
static bool  g_hasLatchedBhWorldDir = false;
static bool  g_hasSkyBhAnchor = false;
static bool  g_lensCapturedNoted = false;
static bool  g_skyDomeNoted = false;
static bool  g_skyCb2Noted = false;
static bool  g_closeMeshNoted = false;
static bool  g_firstDrawLogged = false;

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
    ID3D11Buffer* psCb0 = nullptr;
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

struct Half4 {
    uint16_t r, g, b, a;
};

inline uint16_t floatToHalf(float f) {
    uint32_t x = 0;
    std::memcpy(&x, &f, sizeof(float));
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
            float b = 32.0f * (u_tex * u_tex);

            float r1 = 0.0f, r2 = 0.0f, r3 = 0.0f;
            float captured = 0.0f;

            if (b < 1e-4f) {
                captured = 1.0f;
            } else {
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

BlackHoleDiskMode parseMode(const char* val, bool* recognised) {
    *recognised = true;
    if (!val || !*val || std::strcmp(val, "off") == 0 || std::strcmp(val, "0") == 0 || std::strcmp(val, "false") == 0) {
        return BlackHoleDiskMode::Off;
    }
    if (std::strcmp(val, "simple") == 0) {
        return BlackHoleDiskMode::Simple;
    }
    if (std::strcmp(val, "realistic") == 0) {
        return BlackHoleDiskMode::Realistic;
    }
    *recognised = false;
    return BlackHoleDiskMode::Off;
}

bool ensurePipelineResources(ID3D11DeviceContext* ctx) {
    if (g_resourcesReady) return true;

    ComPtr<ID3D11Device> dev;
    ctx->GetDevice(&dev);
    if (!dev) return false;

    if (g_device != dev.Get()) {
        blackHoleDiskShutdown();
        g_device = dev.Get();
    }

    if (!g_vs) {
        g_vs.Attach(shaderSwapCreateVs(ctx, kBlackHoleDiskSimpleVS, sizeof(kBlackHoleDiskSimpleVS),
                                       "black_hole_disk_vs", "black hole accretion disk"));
    }
    if (!g_ps) {
        g_ps.Attach(shaderSwapCreatePs(ctx, kBlackHoleDiskSimplePS, sizeof(kBlackHoleDiskSimplePS),
                                       "black_hole_disk_ps", "black hole accretion disk"));
    }
    if (!g_vs || !g_ps) {
        if (!g_resourceFailedNoted) {
            g_resourceFailedNoted = true;
            Log::get().note("black hole disk: failed to create pipeline resources");
        }
        return false;
    }

    if (!g_cbConstants) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(BlackHoleDiskConstants);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &g_cbConstants))) return false;
    }

    if (!g_blendState) {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bd, &g_blendState))) return false;
    }

    if (!g_depthStencilState) {
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        dd.StencilEnable = FALSE;
        if (FAILED(dev->CreateDepthStencilState(&dd, &g_depthStencilState))) return false;
    }

    if (!g_rasterizerState) {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = FALSE;
        if (FAILED(dev->CreateRasterizerState(&rd, &g_rasterizerState))) return false;
    }

    if (!g_linearClampSampler) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &g_linearClampSampler))) return false;
    }

    if (!g_geodesicLutSrv) {
        std::vector<Half4> lutPixels;
        generateGeodesicLut(lutPixels, 256, 256, 0.85f);

        D3D11_TEXTURE2D_DESC td{};
        td.Width = 256;
        td.Height = 256;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initData{};
        initData.pSysMem = lutPixels.data();
        initData.SysMemPitch = 256 * sizeof(Half4);

        if (SUCCEEDED(dev->CreateTexture2D(&td, &initData, &g_geodesicLutTex))) {
            dev->CreateShaderResourceView(g_geodesicLutTex.Get(), nullptr, &g_geodesicLutSrv);
        }
    }

    g_resourcesReady = true;
    return true;
}

}  // namespace

void blackHoleDiskCbMapped(void* resource, void* data) {
    if (!resource || !data) return;
    CbShadowEntry* w = findWatch(resource);
    if (w) w->mapped = data;
}

void blackHoleDiskCbUnmapped(void* resource) {
    if (!resource) return;
    CbShadowEntry* w = findWatch(resource);
    if (!w) return;
    if (w->mapped && w->byteWidth > 0) {
        uint32_t n = std::min<uint32_t>(w->byteWidth, static_cast<uint32_t>(sizeof(w->data)));
        const bool ok = guardedBudget(g_budget, [&] {
            std::memcpy(w->data, w->mapped, n);
        });
        if (ok) {
            w->valid = true;
            w->frameUpdated = g_frameIndex;
        } else {
            w->valid = false;
        }
    } else {
        w->valid = false;
    }
    w->mapped = nullptr;
}

void blackHoleDiskCbUpdated(void* resource, const void* data, uint32_t bytes) {
    if (!resource || !data) return;
    CbShadowEntry* w = findWatch(resource);
    if (!w) return;
    if (w->byteWidth > 0) {
        uint32_t n = std::min<uint32_t>(bytes ? bytes : w->byteWidth,
                                       std::min<uint32_t>(w->byteWidth, static_cast<uint32_t>(sizeof(w->data))));
        const bool ok = guardedBudget(g_budget, [&] {
            std::memcpy(w->data, data, n);
        });
        if (ok) {
            w->valid = true;
            w->frameUpdated = g_frameIndex;
        } else {
            w->valid = false;
        }
    }
}

void blackHoleDiskBufferInvalidated(const void* resource) {
    if (!resource) {
        for (CbShadowEntry& w : g_cbShadowTable) {
            if (w.resource) {
                w.valid = false;
                w.mapped = nullptr;
            }
        }
        return;
    }
    CbShadowEntry* w = findWatch(resource);
    if (w) {
        w->valid = false;
        w->mapped = nullptr;
        w->byteWidth = 0; // Force GetDesc re-verification on next draw!
    }
}

void blackHoleDiskConfigure(Config& cfg) {
    g_configRead = true;
    g_configGeneration = cfg.generation();

    const std::string val = cfg.getString("visual.black_hole_disk", "off");
    bool recognised = true;
    const BlackHoleDiskMode mode = parseMode(val.c_str(), &recognised);

    if (!recognised && val != g_unrecognisedSaid) {
        g_unrecognisedSaid = val;
        Log::get().note("black hole disk: visual.black_hole_disk = \"%s\" is not off, simple, or realistic; read as off", val.c_str());
    }

    if (mode != detail::g_blackHoleDiskMode) {
        detail::g_blackHoleDiskMode = mode;
        const bool wanted = (mode != BlackHoleDiskMode::Off);
        detail::g_blackHoleDiskWanted.store(wanted, std::memory_order_relaxed);
        Log::get().note("black hole disk: mode set to %s",
                        mode == BlackHoleDiskMode::Simple ? "simple" :
                        mode == BlackHoleDiskMode::Realistic ? "realistic" : "off");
    }
}

void blackHoleDiskOnFrameStart() {
    g_eyeRtvs[0] = nullptr;
    g_eyeRtvs[1] = nullptr;
    g_eyeDrawnThisFrame[0] = false;
    g_eyeDrawnThisFrame[1] = false;
    g_eyeDrawnThisFrame[2] = false;
    g_bhSkySeenThisFrame = false;
    ++g_frameIndex;

    Config& cfg = Config::get();
    if (!g_configRead || cfg.generation() != g_configGeneration) {
        blackHoleDiskConfigure(cfg);
    }
}

void blackHoleDiskFrameBoundary(ID3D11DeviceContext* /*ctx*/) {
    blackHoleDiskOnFrameStart();
}

void blackHoleDiskAfterDraw(ID3D11DeviceContext* ctx, uint64_t vsHash, uint64_t psHash) {
    if (!ctx) return;
    if (!g_budget.shouldRun()) return;

    // Order 3: Sky Dome
    if (vsHash == kBhSkyDomeVsHash && psHash == kBhSkyDomePsHash) {
        D3D11_VIEWPORT vp{};
        UINT vpCount = 1;
        ctx->RSGetViewports(&vpCount, &vp);
        if (vpCount >= 1 && vp.Width <= 512.0f) {
            return;
        }

        g_bhSkySeenThisFrame = true;
        g_bhSkyLastSeenFrame = g_frameIndex;
        if (!g_skyDomeNoted) {
            g_skyDomeNoted = true;
            Log::get().note("black hole disk: sky dome seen at frame %llu", static_cast<unsigned long long>(g_frameIndex));
        }

        ID3D11RenderTargetView* rtv0 = nullptr;
        ctx->OMGetRenderTargets(1, &rtv0, nullptr);
        int eyeSlot = 0;
        if (runtimeFlatProfile()) {
            eyeSlot = 2;
        } else {
            if (!g_eyeRtvs[0] || g_eyeRtvs[0] == rtv0) {
                g_eyeRtvs[0] = rtv0;
                eyeSlot = 0;
            } else {
                g_eyeRtvs[1] = rtv0;
                eyeSlot = 1;
            }
        }
        if (rtv0) rtv0->Release();

        ID3D11Buffer* vsCb2 = nullptr;
        ID3D11Buffer* psCb2 = nullptr;
        ctx->VSGetConstantBuffers(2, 1, &vsCb2);
        ctx->PSGetConstantBuffers(2, 1, &psCb2);

        CbShadowEntry* wVs2 = shadowFor(vsCb2);
        CbShadowEntry* wPs2 = shadowFor(psCb2);

        if (wVs2 && wVs2->valid && wVs2->byteWidth >= 15 * 16) {
            std::memcpy(g_skyInvViewProj[eyeSlot], &wVs2->data[11][0], 4 * 4 * sizeof(float));
            g_hasSkyInvViewProj[eyeSlot] = true;
        } else if (wPs2 && wPs2->valid && wPs2->byteWidth >= 15 * 16) {
            std::memcpy(g_skyInvViewProj[eyeSlot], &wPs2->data[11][0], 4 * 4 * sizeof(float));
            g_hasSkyInvViewProj[eyeSlot] = true;
        }

        CbShadowEntry* wBh = (wPs2 && wPs2->valid && wPs2->byteWidth >= 25 * 16) ? wPs2
                           : ((wVs2 && wVs2->valid && wVs2->byteWidth >= 25 * 16) ? wVs2 : nullptr);
        if (wBh) {
            float bx = wBh->data[18][0];
            float by = wBh->data[18][1];
            float bz = wBh->data[18][2];
            float bw = wBh->data[18][3];
            float r21 = wBh->data[21][0];
            float r24x = wBh->data[24][0];
            float r24y = wBh->data[24][1];

            if (!g_skyCb2Noted) {
                g_skyCb2Noted = true;
                Log::get().note("black hole disk: sky cb2[18]=(%.4f, %.4f, %.4f, %.4f) cb2[21].x=%.5f cb2[24]=(%.4f, %.4f)",
                                bx, by, bz, bw, r21, r24x, r24y);
            }

            float bLen = std::sqrt(bx * bx + by * by + bz * bz);
            if (bLen > 1e-4f) {
                g_latchedBhWorldDir[0] = bx / bLen;
                g_latchedBhWorldDir[1] = by / bLen;
                g_latchedBhWorldDir[2] = bz / bLen;
                g_hasLatchedBhWorldDir = true;
                g_hasSkyBhAnchor = true;

                if (r21 > 1e-4f) {
                    g_latchedThetaG = std::clamp(r21 * 0.105f, 0.0022f, 0.036f);
                } else if (bw * r24x > 1e-4f) {
                    g_latchedThetaG = std::clamp((bw * r24x) * 0.0065f, 0.0022f, 0.036f);
                } else {
                    g_latchedThetaG = std::max(g_latchedThetaG, 0.0022f);
                }
            }
        }

        if (vsCb2) vsCb2->Release();
        if (psCb2) psCb2->Release();
        return;
    }

    // Orders 4 & 5: Starfield & Bright Stars (Camera basis cache)
    if (vsHash == kStarfieldMainVsHash || vsHash == kStarfieldBrightVsHash) {
        D3D11_VIEWPORT vp{};
        UINT vpCount = 1;
        ctx->RSGetViewports(&vpCount, &vp);
        if (vpCount >= 1 && vp.Width <= 512.0f) {
            return;
        }

        ID3D11Buffer* vsCb0 = nullptr;
        ctx->VSGetConstantBuffers(0, 1, &vsCb0);
        if (vsCb0) {
            CbShadowEntry* wVs0 = shadowFor(vsCb0);
            if (wVs0 && wVs0->valid && wVs0->byteWidth >= 12 * 16) {
                ID3D11RenderTargetView* rtv0 = nullptr;
                ctx->OMGetRenderTargets(1, &rtv0, nullptr);
                int slot = 0;
                if (runtimeFlatProfile()) {
                    slot = 2;
                } else {
                    if (!g_eyeRtvs[0] || g_eyeRtvs[0] == rtv0) {
                        g_eyeRtvs[0] = rtv0;
                        slot = 0;
                    } else {
                        g_eyeRtvs[1] = rtv0;
                        slot = 1;
                    }
                }
                if (rtv0) rtv0->Release();

                std::memcpy(g_cachedCamRight[slot], &wVs0->data[10][0], 3 * sizeof(float));
                std::memcpy(g_cachedCamUp[slot],    &wVs0->data[11][0], 3 * sizeof(float));
                std::memcpy(g_cachedCamFwd[slot],   &wVs0->data[9][0],  3 * sizeof(float));
                g_cachedP00[slot] = wVs0->data[4][0];
                g_cachedP11[slot] = wVs0->data[5][1];
                g_cachedP02[slot] = wVs0->data[4][2];
                g_cachedP12[slot] = wVs0->data[5][2];
                g_hasCachedCamBasis[slot] = true;
            }
            vsCb0->Release();
        }
        if (vsHash != kStarfieldBrightVsHash || psHash != kStarfieldPsHash) {
            return;
        }
    }

    // Order 6: Lens Mesh Draw (3D World Anchor & Scale)
    if (vsHash == kBhLensMeshVsHash && psHash == kBhLensMeshPsHash) {
        g_bhSkySeenThisFrame = true;
        ID3D11Buffer* vsCb0 = nullptr;
        ctx->VSGetConstantBuffers(0, 1, &vsCb0);
        if (vsCb0) {
            CbShadowEntry* wVs0 = shadowFor(vsCb0);
            if (wVs0 && wVs0->valid && wVs0->byteWidth >= 4 * 16) {
                float tx = wVs0->data[3][0];
                float ty = wVs0->data[3][1];
                float tz = wVs0->data[3][2];
                float dist = std::sqrt(tx * tx + ty * ty + tz * tz);
                float sx = wVs0->data[0][0];
                float sy = wVs0->data[0][1];
                float sz = wVs0->data[0][2];
                float scale = std::sqrt(sx * sx + sy * sy + sz * sz);

                if (dist > 1e-5f) {
                    if (!g_hasSkyBhAnchor) {
                        g_latchedBhWorldDir[0] = tx / dist;
                        g_latchedBhWorldDir[1] = ty / dist;
                        g_latchedBhWorldDir[2] = tz / dist;
                        g_hasLatchedBhWorldDir = true;
                        float rawTheta = (scale / dist) * 0.08f;
                        g_latchedThetaG = std::clamp(rawTheta, 0.0003f, 0.45f);
                    }
                    if (!g_lensCapturedNoted) {
                        g_lensCapturedNoted = true;
                        Log::get().note("black hole disk: lens mesh anchor captured, dir=(%.3f, %.3f, %.3f) theta_g=%.5f dist=%.1f",
                                        tx / dist, ty / dist, tz / dist,
                                        (scale / dist) * 0.08f, dist);
                    }
                }
            }
            vsCb0->Release();
        }
        return;
    }

    // Order 6b: Close Approach Mesh
    if (vsHash == kBhCloseMeshVsHash) {
        g_bhSkySeenThisFrame = true;
        if (!g_closeMeshNoted) {
            g_closeMeshNoted = true;
            Log::get().note("black hole disk: close approach mesh seen");
        }
        return;
    }

    // Orders 5 and 7a / 7b: Starfield Bright / Celestial Dust Sphere / Supercruise Space Dust
    const bool isOrder5 = (vsHash == kStarfieldBrightVsHash && psHash == kStarfieldPsHash);
    const bool isCelestial7a = (vsHash == kNormalCelestialVsHash && psHash == kNormalCelestialPsHash);
    const bool isCelestial7b = (vsHash == kSupercruiseCelestialVsHash && (psHash == kSupercruiseCelestialPsHash || psHash == 0xDBF1725726018F52ULL));
    if (!isOrder5 && !isCelestial7a && !isCelestial7b) return;

    if (!g_bhSkySeenThisFrame && (g_frameIndex - g_bhSkyLastSeenFrame > 2)) return;

    // Do not issue the accretion disk draw until a real black hole anchor has been latched from the sky dome / lens mesh!
    if (!g_hasSkyBhAnchor && !g_hasLatchedBhWorldDir) return;

    VrWorldInternalScope internalScope;

    guardedBudget(g_budget, [&] {
        if (!ensurePipelineResources(ctx)) return;

        SavedPipeline saved;
        saved.capture(ctx);

        if (!saved.rtv[0]) {
            saved.restore(ctx);
            return;
        }

        if (saved.viewportCount >= 1 && saved.viewports[0].Width <= 512.0f) {
            saved.restore(ctx);
            return;
        }

        int eyeSlot = 0;
        if (runtimeFlatProfile()) {
            eyeSlot = 2;
        } else {
            if (!g_eyeRtvs[0] || g_eyeRtvs[0] == saved.rtv[0]) {
                g_eyeRtvs[0] = saved.rtv[0];
                eyeSlot = 0;
            } else {
                g_eyeRtvs[1] = saved.rtv[0];
                eyeSlot = 1;
            }
        }

        if (g_eyeDrawnThisFrame[eyeSlot]) {
            saved.restore(ctx);
            return;
        }
        g_eyeDrawnThisFrame[eyeSlot] = true;

        float camRight[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
        float camUp[4]    = { 0.0f, 1.0f, 0.0f, 1.0f };
        float camFwd[4]   = { 0.0f, 0.0f, 1.0f, 0.0f };
        float p12         = 0.0f;

        CbShadowEntry* wVs0 = shadowFor(saved.vsCb0);
        if (wVs0 && wVs0->valid && wVs0->byteWidth >= 12 * 16) {
            camRight[0] = wVs0->data[10][0]; camRight[1] = wVs0->data[10][1]; camRight[2] = wVs0->data[10][2];
            camUp[0]    = wVs0->data[11][0]; camUp[1]    = wVs0->data[11][1]; camUp[2]    = wVs0->data[11][2];
            camFwd[0]   = wVs0->data[9][0];  camFwd[1]   = wVs0->data[9][1];  camFwd[2]   = wVs0->data[9][2];
            camRight[3] = wVs0->data[4][0];
            camUp[3]    = wVs0->data[5][1];
            camFwd[3]   = wVs0->data[4][2];
            p12         = wVs0->data[5][2];
        } else if (g_hasCachedCamBasis[eyeSlot]) {
            camRight[0] = g_cachedCamRight[eyeSlot][0]; camRight[1] = g_cachedCamRight[eyeSlot][1]; camRight[2] = g_cachedCamRight[eyeSlot][2];
            camUp[0]    = g_cachedCamUp[eyeSlot][0];    camUp[1]    = g_cachedCamUp[eyeSlot][1];    camUp[2]    = g_cachedCamUp[eyeSlot][2];
            camFwd[0]   = g_cachedCamFwd[eyeSlot][0];   camFwd[1]   = g_cachedCamFwd[eyeSlot][1];   camFwd[2]   = g_cachedCamFwd[eyeSlot][2];
            camRight[3] = g_cachedP00[eyeSlot];
            camUp[3]    = g_cachedP11[eyeSlot];
            camFwd[3]   = g_cachedP02[eyeSlot];
            p12         = g_cachedP12[eyeSlot];
        }

        // Fixed 3D world-space spin axis per star system (independent of camera position / d_bh!),
        // so orbiting around the black hole continuously changes the 3D inclination and azimuth.
        uint64_t sysAddr = journalSystemAddress();
        if (sysAddr == 0) sysAddr = 0x123456789ABCDEF0ULL;
        float phiAxis = static_cast<float>((sysAddr & 0xFFFF) % 360) * (3.1415926535f / 180.0f);
        float tiltDeg = 16.0f + static_cast<float>((sysAddr >> 16) % 24);
        float tiltRad = tiltDeg * (3.1415926535f / 180.0f);

        float sinT = std::sin(tiltRad);
        float cosT = std::cos(tiltRad);
        float diskNormal[4] = {
            sinT * std::cos(phiAxis),
            cosT,
            sinT * std::sin(phiAxis),
            0.0f
        };
        float normLen = std::sqrt(diskNormal[0] * diskNormal[0] + diskNormal[1] * diskNormal[1] + diskNormal[2] * diskNormal[2]);
        if (normLen > 1e-4f) { diskNormal[0] /= normLen; diskNormal[1] /= normLen; diskNormal[2] /= normLen; }

        // Fixed world-space orthonormal basis (diskU, diskV) in the equatorial disk plane:
        float worldRef[3] = { 0.0f, 0.0f, 1.0f };
        if (std::abs(diskNormal[2]) > 0.9f) {
            worldRef[0] = 1.0f; worldRef[1] = 0.0f; worldRef[2] = 0.0f;
        }
        float diskU[3] = {
            diskNormal[1] * worldRef[2] - diskNormal[2] * worldRef[1],
            diskNormal[2] * worldRef[0] - diskNormal[0] * worldRef[2],
            diskNormal[0] * worldRef[1] - diskNormal[1] * worldRef[0]
        };
        float uLen = std::sqrt(diskU[0] * diskU[0] + diskU[1] * diskU[1] + diskU[2] * diskU[2]);
        if (uLen > 1e-4f) { diskU[0] /= uLen; diskU[1] /= uLen; diskU[2] /= uLen; }

        float diskV[3] = {
            diskNormal[1] * diskU[2] - diskNormal[2] * diskU[1],
            diskNormal[2] * diskU[0] - diskNormal[0] * diskU[2],
            diskNormal[0] * diskU[1] - diskNormal[1] * diskU[0]
        };

        const BlackHoleDiskMode mode = detail::g_blackHoleDiskMode;
        float a_spin = 0.0f;
        float r_isco = 5.5f;
        float r_horizon = 2.0f;
        float b_crit = 5.1961524f;
        float modeVal = 1.0f;

        if (mode == BlackHoleDiskMode::Realistic) {
            a_spin = 0.85f;
            r_isco = 3.8f;
            r_horizon = 1.5268f;
            b_crit = 4.65f;
            modeVal = 2.0f;
        }

        float stellarMass = journalBlackHoleStellarMass();
        float peakTempK = 14000.0f;
        if (stellarMass > 0.0f) {
            float mClamped = std::max(stellarMass, 3.0f);
            peakTempK = 14000.0f * std::pow(10.0f / mClamped, 0.25f);
        }

        BlackHoleDiskConstants cb{};
        std::memcpy(cb.camRight, camRight, sizeof(camRight));
        std::memcpy(cb.camUp,    camUp,    sizeof(camUp));
        std::memcpy(cb.camFwd,   camFwd,   sizeof(camFwd));
        cb.projParams[0] = p12;
        cb.projParams[1] = static_cast<float>(qpcNow()) * 1e-7f;
        cb.projParams[2] = modeVal;
        cb.projParams[3] = a_spin;

        cb.bhWorldDir[0] = g_latchedBhWorldDir[0];
        cb.bhWorldDir[1] = g_latchedBhWorldDir[1];
        cb.bhWorldDir[2] = g_latchedBhWorldDir[2];
        cb.bhWorldDir[3] = g_latchedThetaG;

        cb.diskNormal[0] = diskNormal[0];
        cb.diskNormal[1] = diskNormal[1];
        cb.diskNormal[2] = diskNormal[2];
        cb.diskNormal[3] = r_isco;

        cb.diskTangentU[0] = diskU[0];
        cb.diskTangentU[1] = diskU[1];
        cb.diskTangentU[2] = diskU[2];
        cb.diskTangentU[3] = 20.0f; // r_out

        cb.diskTangentV[0] = diskV[0];
        cb.diskTangentV[1] = diskV[1];
        cb.diskTangentV[2] = diskV[2];
        cb.diskTangentV[3] = peakTempK;

        if (g_hasSkyInvViewProj[eyeSlot]) {
            cb.params[0] = 1.0f; // useInvViewProj = 1.0
            std::memcpy(cb.invViewProj0, g_skyInvViewProj[eyeSlot][0], sizeof(float) * 4);
            std::memcpy(cb.invViewProj1, g_skyInvViewProj[eyeSlot][1], sizeof(float) * 4);
            std::memcpy(cb.invViewProj2, g_skyInvViewProj[eyeSlot][2], sizeof(float) * 4);
            std::memcpy(cb.invViewProj3, g_skyInvViewProj[eyeSlot][3], sizeof(float) * 4);
        } else {
            cb.params[0] = 0.0f; // useInvViewProj = 0.0 (cam basis fallback)
        }
        cb.params[1] = 1.0f; // exposureScale
        cb.params[2] = r_horizon;
        cb.params[3] = b_crit;

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(ctx->Map(g_cbConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)) && mapped.pData) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx->Unmap(g_cbConstants.Get(), 0);
        }

        FLOAT blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        ctx->OMSetBlendState(g_blendState.Get(), blendFactor, 0xFFFFFFFFu);
        ctx->OMSetDepthStencilState(g_depthStencilState.Get(), 0);
        ctx->RSSetState(g_rasterizerState.Get());
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ctx->VSSetShader(g_vs.Get(), nullptr, 0);
        ctx->PSSetShader(g_ps.Get(), nullptr, 0);

        ID3D11Buffer* cbs[1] = { g_cbConstants.Get() };
        ctx->VSSetConstantBuffers(0, 1, cbs);
        ctx->PSSetConstantBuffers(0, 1, cbs);

        if (mode == BlackHoleDiskMode::Realistic && g_geodesicLutSrv) {
            ID3D11ShaderResourceView* srvs[1] = { g_geodesicLutSrv.Get() };
            ctx->PSSetShaderResources(0, 1, srvs);
            ID3D11SamplerState* samplers[1] = { g_linearClampSampler.Get() };
            ctx->PSSetSamplers(0, 1, samplers);
        }

        vScreenDrawRaw(ctx, 3, 0);

        if (!g_firstDrawLogged) {
            g_firstDrawLogged = true;
            Log::get().note("black hole disk: first draw issued (eyeSlot=%d useInvVp=%.0f dir=(%.3f,%.3f,%.3f) theta_g=%.5f)",
                            eyeSlot, cb.params[0], cb.bhWorldDir[0], cb.bhWorldDir[1], cb.bhWorldDir[2], cb.bhWorldDir[3]);
        }

        saved.restore(ctx);
    });
}

void blackHoleDiskShutdown() {
    g_vs.Reset();
    g_ps.Reset();
    g_cbConstants.Reset();
    g_blendState.Reset();
    g_depthStencilState.Reset();
    g_rasterizerState.Reset();
    g_linearClampSampler.Reset();
    g_geodesicLutTex.Reset();
    g_geodesicLutSrv.Reset();
    g_resourcesReady = false;
    g_device = nullptr;
    std::memset(g_cbShadowTable, 0, sizeof(g_cbShadowTable));
    std::memset(g_skyInvViewProj, 0, sizeof(g_skyInvViewProj));
    g_hasSkyInvViewProj[0] = g_hasSkyInvViewProj[1] = g_hasSkyInvViewProj[2] = false;
    g_hasCachedCamBasis[0] = g_hasCachedCamBasis[1] = g_hasCachedCamBasis[2] = false;
    g_hasLatchedBhWorldDir = false;
    g_hasSkyBhAnchor = false;
    g_firstDrawLogged = false;
    g_skyCb2Noted = false;
    g_lensCapturedNoted = false;
    g_skyDomeNoted = false;
    g_closeMeshNoted = false;
}

}  // namespace edvr
