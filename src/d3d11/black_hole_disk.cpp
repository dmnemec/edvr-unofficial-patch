// Black hole accretion disk simulation and rendering hook.
// visual.black_hole_disk = off | simple | realistic
// Docs: docs/black-hole-accretion-disk-2026-11-04.md

#include "black_hole_disk.h"
#include "black_hole_disk_shader.h"
#include "billboard_rollfree.h"
#include "flat_compute_readback.h"
#include "shader_swap.h"
#include "../common/config.h"
#include "../common/log.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <string>

namespace edvr {

namespace detail {
std::atomic<bool> g_blackHoleDiskWanted{false};
BlackHoleDiskMode g_blackHoleDiskMode = BlackHoleDiskMode::Off;
}  // namespace detail

namespace {

using Microsoft::WRL::ComPtr;

constexpr uint64_t kLensVsHash = 0x41E245D488BFE83Eull;
constexpr uint64_t kLensPsHash = 0x6EF82262EB12A037ull;

// Config tracking
bool g_configRead = false;
uint32_t g_configGeneration = 0;
std::string g_unrecognisedSaid;

// Device and pipeline resources
ID3D11Device* g_device = nullptr;
ComPtr<ID3D11VertexShader> g_vsSimple;
ComPtr<ID3D11PixelShader>  g_psSimple;
ComPtr<ID3D11Buffer>       g_cb2Patch;
ComPtr<ID3D11Buffer>       g_cbParams;
ComPtr<ID3D11BlendState>   g_blendState;
ComPtr<ID3D11DepthStencilState> g_depthStencilState;
ComPtr<ID3D11RasterizerState>   g_rasterizerState;
ComPtr<ID3D11SamplerState> g_pointSampler;

bool g_resourcesReady = false;
bool g_resourceFailedNoted = false;

// Constant buffer CPU-side shadow (144 bytes, zero PCIe readback stall)
void* g_watchedCbResource = nullptr;
void* g_mappedCbPointer = nullptr;
float g_cb2Shadow[9][4] = {};
bool  g_cb2ShadowValid = false;

struct AccretionDiskParams {
    float diskParams0[4]; // r_shadow (5.2), r_photon (3.0), r_isco (6.0), r_out (25.0)
    float diskParams1[4]; // angular_scale, doppler_strength (3.0), brightness_scale (1.0), temperature
    float diskParams2[4]; // normal override
    float diskParams3[4]; // reserved
};

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
    ID3D11ShaderResourceView* psSrv0 = nullptr;
    ID3D11SamplerState* psSampler0 = nullptr;
    ID3D11Buffer* psCb2 = nullptr;
    ID3D11Buffer* psCb3 = nullptr;

    void capture(ID3D11DeviceContext* c) {
        c->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtv, &dsv);
        c->OMGetBlendState(&blend, blendFactor, &sampleMask);
        c->OMGetDepthStencilState(&depth, &stencilRef);
        c->RSGetState(&raster);
        c->RSGetViewports(&viewportCount, viewports);
        c->IAGetInputLayout(&layout);
        c->IAGetPrimitiveTopology(&topology);
        c->VSGetShader(&vs, nullptr, nullptr);
        c->PSGetShader(&ps, nullptr, nullptr);
        c->PSGetShaderResources(0, 1, &psSrv0);
        c->PSGetSamplers(0, 1, &psSampler0);
        c->PSGetConstantBuffers(2, 1, &psCb2);
        c->PSGetConstantBuffers(3, 1, &psCb3);
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
        c->PSSetShaderResources(0, 1, &psSrv0);
        c->PSSetSamplers(0, 1, &psSampler0);
        c->PSSetConstantBuffers(2, 1, &psCb2);
        c->PSSetConstantBuffers(3, 1, &psCb3);

        for (ID3D11RenderTargetView*& v : rtv) {
            if (v) v->Release();
            v = nullptr;
        }
        if (dsv) { dsv->Release(); dsv = nullptr; }
        if (blend) { blend->Release(); blend = nullptr; }
        if (depth) { depth->Release(); depth = nullptr; }
        if (raster) { raster->Release(); raster = nullptr; }
        if (layout) { layout->Release(); layout = nullptr; }
        if (vs) { vs->Release(); vs = nullptr; }
        if (ps) { ps->Release(); ps = nullptr; }
        if (psSrv0) { psSrv0->Release(); psSrv0 = nullptr; }
        if (psSampler0) { psSampler0->Release(); psSampler0 = nullptr; }
        if (psCb2) { psCb2->Release(); psCb2 = nullptr; }
        if (psCb3) { psCb3->Release(); psCb3 = nullptr; }
    }
};

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

    // Shaders
    if (!g_vsSimple) {
        g_vsSimple.Attach(shaderSwapCreateVs(ctx, kBlackHoleDiskSimpleVS, sizeof(kBlackHoleDiskSimpleVS),
                                             "black_hole_disk_simple_vs", "black hole accretion disk"));
    }
    if (!g_psSimple) {
        g_psSimple.Attach(shaderSwapCreatePs(ctx, kBlackHoleDiskSimplePS, sizeof(kBlackHoleDiskSimplePS),
                                             "black_hole_disk_simple_ps", "black hole accretion disk"));
    }
    if (!g_vsSimple || !g_psSimple) {
        if (!g_resourceFailedNoted) {
            g_resourceFailedNoted = true;
            Log::get().note("black hole disk: failed to create simple shaders");
        }
        return false;
    }

    // Dynamic constant buffers:
    // cb2 patch buffer: 144 bytes (9 * float4 = 144 bytes; round up to multiple of 16 -> 144 bytes)
    if (!g_cb2Patch) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = 144;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev->CreateBuffer(&bd, nullptr, &g_cb2Patch))) return false;
    }

    // Staging read buffer for game CB2

    // cbParams buffer: sizeof(AccretionDiskParams) = 64 bytes
    if (!g_cbParams) {
        AccretionDiskParams p{};
        p.diskParams0[0] = 5.2f;  // r_shadow
        p.diskParams0[1] = 3.0f;  // r_photon
        p.diskParams0[2] = 6.0f;  // r_isco
        p.diskParams0[3] = 25.0f; // r_out
        p.diskParams1[0] = 1.0f;  // angular_scale
        p.diskParams1[1] = 3.0f;  // doppler_strength
        p.diskParams1[2] = 1.0f;  // brightness_scale
        p.diskParams1[3] = 1.0f;  // temperature

        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(AccretionDiskParams);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = &p;
        if (FAILED(dev->CreateBuffer(&bd, &sd, &g_cbParams))) return false;
    }

    // Blend state: Premultiplied alpha / Additive (Src = ONE, Dest = INV_SRC_ALPHA, Op = ADD)
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

    // Depth Stencil State: Read-only depth test (Reversed-Z: GREATER_EQUAL, writes off)
    if (!g_depthStencilState) {
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = TRUE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        dd.StencilEnable = FALSE;
        if (FAILED(dev->CreateDepthStencilState(&dd, &g_depthStencilState))) return false;
    }

    // Rasterizer state: Cull none
    if (!g_rasterizerState) {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = FALSE;
        if (FAILED(dev->CreateRasterizerState(&rd, &g_rasterizerState))) return false;
    }

    // Point Clamp Sampler:
    if (!g_pointSampler) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        if (FAILED(dev->CreateSamplerState(&sd, &g_pointSampler))) return false;
    }

    g_resourcesReady = true;
    return true;
}

}  // namespace

void blackHoleDiskCbMapped(void* resource, void* data) {
    if (resource == g_watchedCbResource) {
        g_mappedCbPointer = data;
    }
}

void blackHoleDiskCbUnmapped(void* resource) {
    if (resource == g_watchedCbResource && g_mappedCbPointer) {
        std::memcpy(g_cb2Shadow, g_mappedCbPointer, sizeof(g_cb2Shadow));
        g_cb2ShadowValid = true;
        g_mappedCbPointer = nullptr;
    }
}

void blackHoleDiskCbUpdated(void* resource, const void* data, uint32_t bytes) {
    if (resource == g_watchedCbResource && data) {
        const uint32_t n = (bytes > 0 && bytes < sizeof(g_cb2Shadow)) ? bytes : sizeof(g_cb2Shadow);
        std::memcpy(g_cb2Shadow, data, n);
        g_cb2ShadowValid = true;
    }
}

void blackHoleDiskFrameBoundary(ID3D11DeviceContext* ctx) {
    Config& cfg = Config::get();
    const uint32_t generation = cfg.generation();
    if (!g_configRead || generation != g_configGeneration) {
        g_configRead = true;
        g_configGeneration = generation;

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
}

void blackHoleDiskAfterDraw(ID3D11DeviceContext* ctx, uint64_t vsHash, uint64_t psHash) {
    if (!ctx) return;
    if (vsHash != kLensVsHash || psHash != kLensPsHash) return;

    if (!ensurePipelineResources(ctx)) return;

    FlatComputeInternalScope internalScope;

    // Capture the pipeline state
    SavedPipeline saved;
    saved.capture(ctx);

    // Read the game's CB2 constant buffer currently bound at pixel shader slot 2
    if (!saved.psCb2) {
        saved.restore(ctx);
        return;
    }

    // Register this buffer as the watched resource for Map/Unmap and UpdateSubresource tees
    g_watchedCbResource = saved.psCb2;

    float cb2Data[9][4];
    if (g_cb2ShadowValid) {
        std::memcpy(cb2Data, g_cb2Shadow, sizeof(cb2Data));
    } else {
        // Fallback if not yet captured by shadow: default forward orientation
        std::memset(cb2Data, 0, sizeof(cb2Data));
        cb2Data[4][2] = 1.0f; cb2Data[4][3] = 1000.0f; // Dir +Z, dist 1000 LS
        cb2Data[5][0] = 1.0f;                          // Right +X
        cb2Data[6][1] = 1.0f;                          // Up +Y
        cb2Data[7][2] = 1.0f;                          // Fwd +Z
    }

    // Decouple visor roll from the body basis:
    // cb2[4] = Direction xyz, distance w
    // cb2[5] = Right
    // cb2[6] = Up
    // cb2[7] = Forward
    float unrolledRightUp[8];
    if (rollfree::basis(cb2Data[5], cb2Data[6], cb2Data[7], unrolledRightUp)) {
        // Replace cb2[5] (Right) and cb2[6] (Up) with the roll-free world-up basis
        std::memcpy(cb2Data[5], unrolledRightUp, 4 * sizeof(float));
        std::memcpy(cb2Data[6], unrolledRightUp + 4, 4 * sizeof(float));
    }

    // Upload patched cb2 data
    D3D11_MAPPED_SUBRESOURCE mappedPatch{};
    if (SUCCEEDED(ctx->Map(g_cb2Patch.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedPatch)) && mappedPatch.pData) {
        std::memcpy(mappedPatch.pData, cb2Data, sizeof(cb2Data));
        ctx->Unmap(g_cb2Patch.Get(), 0);
    }

    // Set up rendering state for the accretion disk fullscreen triangle
    FLOAT blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    ctx->OMSetBlendState(g_blendState.Get(), blendFactor, 0xFFFFFFFFu);
    ctx->OMSetDepthStencilState(g_depthStencilState.Get(), 0);
    ctx->RSSetState(g_rasterizerState.Get());
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->VSSetShader(g_vsSimple.Get(), nullptr, 0);
    ctx->PSSetShader(g_psSimple.Get(), nullptr, 0);

    ID3D11Buffer* psCbs[2] = { g_cb2Patch.Get(), g_cbParams.Get() };
    ctx->PSSetConstantBuffers(2, 2, psCbs);

    ID3D11SamplerState* samplers[1] = { g_pointSampler.Get() };
    ctx->PSSetSamplers(0, 1, samplers);

    // Issue fullscreen triangle draw
    ctx->Draw(3, 0);

    // Restore original state
    saved.restore(ctx);
}

void blackHoleDiskShutdown() {
    g_vsSimple.Reset();
    g_psSimple.Reset();
    g_cb2Patch.Reset();
    g_cbParams.Reset();
        g_blendState.Reset();
    g_depthStencilState.Reset();
    g_rasterizerState.Reset();
    g_pointSampler.Reset();
    g_resourcesReady = false;
    g_device = nullptr;
    g_watchedCbResource = nullptr;
    g_mappedCbPointer = nullptr;
    g_cb2ShadowValid = false;
}

}  // namespace edvr
