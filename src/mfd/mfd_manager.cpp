#include "mfd_manager.h"
#include "../common/config.h"
#include <algorithm>

#if __has_include(<openxr/openxr.h>)
#include <openxr/openxr.h>
#else
struct XrVector3f { float x, y, z; };
struct XrQuaternionf { float x, y, z, w; };
struct XrPosef { XrQuaternionf orientation; XrVector3f position; };
struct XrFovf { float angleLeft, angleRight, angleUp, angleDown; };
#endif

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace edvr::mfd {

MfdManager& MfdManager::instance() {
    static MfdManager s_instance;
    return s_instance;
}

MfdManager::MfdManager() {
    m_compositor = std::make_unique<OpenXrQuadCompositor>();
}

MfdManager::~MfdManager() {
    shutdown();
}

void MfdManager::ensureDefaultSlots() {
    if (!m_slots.empty()) return;

    // Slot 1: Lower-right console (default MFD)
    MfdPose poseRight;
    poseRight.position = Vec3(0.24f, -0.22f, -0.50f);
    poseRight.orientation = Quat::fromEulerDegrees(-25.0f, -15.0f, 0.0f);
    poseRight.widthM = 0.24f;
    poseRight.heightM = 0.16f;

    auto providerRight = std::make_unique<DeclarativeMfdProvider>("right_console", "SPANSH ROUTER");
    providerRight->loadFromJson(R"({
        "title": "SPANSH NEUTRON ROUTER",
        "subtitle": "WAYPOINT 2 OF 14",
        "statusBadge": "ONLINE",
        "footerHint": "[Q/E] TABS   [\x1E/\x1F] SELECT   [SPACE] COPY",
        "tabs": [
            {
                "title": "ROUTE",
                "type": "list",
                "items": [
                    {"id": "wp1", "label": "Jackson's Lighthouse", "value": "12.4 LY", "sublabel": "Neutron Star", "badge": "CURRENT"},
                    {"id": "wp2", "label": "Prua Phoe NC-D d12-14", "value": "148.1 LY", "sublabel": "Supercharge Ready", "badge": "NEXT"},
                    {"id": "wp3", "label": "Dryooe Prou CS-B d1-2", "value": "294.0 LY", "sublabel": "Class M Star", "badge": "REFUEL"}
                ]
            },
            {
                "title": "TELEMETRY",
                "type": "keyvalue",
                "items": [
                    {"key": "Current System", "value": "SOL"},
                    {"key": "Target System", "value": "Prua Phoe NC-D d12-14"},
                    {"key": "Route Distance", "value": "2,410.8 LY"},
                    {"key": "Fuel Reserve", "value": "98%"},
                    {"key": "Jump Range", "value": "64.2 LY"}
                ]
            }
        ]
    })");

    addSlot("right_console", std::move(providerRight), poseRight);
}

bool MfdManager::initialize(int renderWidth, int renderHeight) {
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;

    ensureDefaultSlots();
    m_enabled = edvr::Config::get().getBool("fix.cockpit_mfd", false);

    if (m_compositor) {
        if (auto* quad = dynamic_cast<OpenXrQuadCompositor*>(m_compositor.get())) {
            quad->initialize(renderWidth, renderHeight);
        }
    }
    return true;
}

void MfdManager::shutdown() {
    m_slots.clear();
    if (m_compositor) {
        m_compositor->shutdown();
    }
}

bool MfdManager::addSlot(std::string name, std::unique_ptr<IMfdProvider> provider, const MfdPose& pose) {
    if (!provider) return false;

    MfdSlot slot;
    slot.name = std::move(name);
    slot.pose = pose;
    slot.provider = std::move(provider);
    slot.renderer = std::make_unique<MfdRenderer>(m_renderWidth, m_renderHeight);
    slot.isVisible = true;

    m_slots.push_back(std::move(slot));
    return true;
}

MfdSlot* MfdManager::findSlot(const std::string& name) {
    for (auto& slot : m_slots) {
        if (slot.name == name) return &slot;
    }
    return nullptr;
}

MfdSlot* MfdManager::focusedSlot() {
    for (auto& slot : m_slots) {
        if (slot.gazeTracker.isFocused()) return &slot;
    }
    return nullptr;
}

void MfdManager::update(const Vec3& headPos, const Vec3& headForward, float dtSeconds) {
    if (!m_enabled) return;
    ensureDefaultSlots();

    for (auto& slot : m_slots) {
        if (!slot.isVisible || !slot.provider) continue;

        // Step 1: Update provider internal data / timers
        slot.provider->update(dtSeconds);

        // Step 2: Track head gaze and evaluate focus state
        MfdFocusState prevState = slot.gazeTracker.currentState();
        MfdFocusState newState = slot.gazeTracker.update(headPos, headForward, slot.pose, dtSeconds);

        // Step 3: Notify provider if focus state crossed threshold
        bool wasFocused = (prevState == MfdFocusState::kFocused);
        bool isFocused = (newState == MfdFocusState::kFocused);
        if (wasFocused != isFocused) {
            slot.provider->onFocusChanged(isFocused);
        }
    }
}

void MfdManager::render() {
    if (!m_enabled) return;
    ensureDefaultSlots();

    for (auto& slot : m_slots) {
        if (!slot.isVisible || !slot.provider || !slot.renderer) continue;

        // Rasterize view model into pixel buffer
        slot.renderer->render(slot.provider->viewModel());

        // Submit rendered frame to active compositor
        if (m_compositor && m_compositor->isReady()) {
            m_compositor->updatePose(slot.pose);
            m_compositor->submitTexture(slot.renderer->pixelData(),
                                        slot.renderer->width(),
                                        slot.renderer->height());
        }
    }
}

void MfdManager::renderToEyeRtv(ID3D11Device* device, ID3D11DeviceContext* context,
                                ID3D11RenderTargetView* rtv,
                                const XrPosef& eyePose, const XrFovf& eyeFov,
                                uint32_t viewportWidth, uint32_t viewportHeight,
                                ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                                ID3D11SamplerState* sampler, ID3D11Buffer* constantsBuffer) {
    if (!m_enabled || m_slots.empty() || !device || !context || !rtv) return;

    Vec3 eyePos(eyePose.position.x, eyePose.position.y, eyePose.position.z);
    Quat eyeRot(eyePose.orientation.x, eyePose.orientation.y, eyePose.orientation.z, eyePose.orientation.w);

    for (auto& slot : m_slots) {
        if (!slot.isVisible || !slot.renderer) continue;

        // Transform MFD position to eye-local space:
        Vec3 relPos = slot.pose.position - eyePos;
        Quat invEyeRot(-eyeRot.x, -eyeRot.y, -eyeRot.z, eyeRot.w);
        Vec3 eyeLocal = invEyeRot.rotate(relPos);

        // Must be in front of the eye (-Z in OpenXR eye space)
        if (eyeLocal.z >= -0.05f) continue;

        float zDist = -eyeLocal.z; // positive distance forward

        float tanX = eyeLocal.x / zDist;
        float tanY = eyeLocal.y / zDist;

        float fovW = eyeFov.angleRight - eyeFov.angleLeft;
        float fovH = eyeFov.angleUp - eyeFov.angleDown;
        if (fovW <= 1e-4f || fovH <= 1e-4f) continue;

        float normX = (tanX - eyeFov.angleLeft) / fovW;
        float normY = (eyeFov.angleUp - tanY) / fovH;

        float cx = normX * static_cast<float>(viewportWidth);
        float cy = normY * static_cast<float>(viewportHeight);

        float pixW = (slot.pose.widthM / zDist) / fovW * static_cast<float>(viewportWidth);
        float pixH = (slot.pose.heightM / zDist) / fovH * static_cast<float>(viewportHeight);

        if (pixW < 10.0f || pixH < 10.0f) continue;

        float vx = cx - pixW * 0.5f;
        float vy = cy - pixH * 0.5f;

        // Clip to viewport bounds
        if (vx + pixW < 0 || vx >= static_cast<float>(viewportWidth) ||
            vy + pixH < 0 || vy >= static_cast<float>(viewportHeight)) continue;

        ID3D11ShaderResourceView* mfdSrv = nullptr;
        slot.renderer->createOrUpdateD3D11Srv(device, context, &mfdSrv);
        if (!mfdSrv) continue;

        // Save current context state to ensure complete restoration
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> origRtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> origDsv;
        context->OMGetRenderTargets(1, &origRtv, &origDsv);

        Microsoft::WRL::ComPtr<ID3D11BlendState> origBlend;
        FLOAT origBlendFactor[4]{}; UINT origSampleMask = 0;
        context->OMGetBlendState(&origBlend, origBlendFactor, &origSampleMask);

        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> origDepth;
        UINT origStencilRef = 0;
        context->OMGetDepthStencilState(&origDepth, &origStencilRef);

        D3D11_VIEWPORT origVp{}; UINT numVp = 1;
        context->RSGetViewports(&numVp, &origVp);

        Microsoft::WRL::ComPtr<ID3D11VertexShader> origVs;
        context->VSGetShader(&origVs, nullptr, nullptr);
        Microsoft::WRL::ComPtr<ID3D11PixelShader> origPs;
        context->PSGetShader(&origPs, nullptr, nullptr);

        Microsoft::WRL::ComPtr<ID3D11SamplerState> origSampler;
        context->PSGetSamplers(0, 1, &origSampler);

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> origSrv;
        context->PSGetShaderResources(0, 1, &origSrv);

        Microsoft::WRL::ComPtr<ID3D11Buffer> origPsCb;
        context->PSGetConstantBuffers(0, 1, &origPsCb);
        Microsoft::WRL::ComPtr<ID3D11Buffer> origVsCb;
        context->VSGetConstantBuffers(0, 1, &origVsCb);

        // Bind viewport for MFD overlay
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = std::max(0.0f, vx);
        vp.TopLeftY = std::max(0.0f, vy);
        vp.Width = std::min(static_cast<float>(viewportWidth) - vp.TopLeftX, pixW);
        vp.Height = std::min(static_cast<float>(viewportHeight) - vp.TopLeftY, pixH);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        context->RSSetViewports(1, &vp);
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->VSSetShader(vs, nullptr, 0);
        context->PSSetShader(ps, nullptr, 0);
        context->PSSetSamplers(0, 1, &sampler);
        context->PSSetShaderResources(0, 1, &mfdSrv);

        struct BlitConstants { float bounds[4]; float clampUV[4]; float encodeSRGB; float pad[3]; } cb{};
        cb.bounds[0] = 0.0f; cb.bounds[1] = 0.0f; cb.bounds[2] = 1.0f; cb.bounds[3] = 1.0f;
        cb.clampUV[0] = 0.0f; cb.clampUV[1] = 0.0f; cb.clampUV[2] = 1.0f; cb.clampUV[3] = 1.0f;
        cb.encodeSRGB = 1.0f;

        Microsoft::WRL::ComPtr<ID3D11Buffer> mfdCb;
        D3D11_BUFFER_DESC cbd{}; cbd.ByteWidth = sizeof(cb); cbd.Usage = D3D11_USAGE_DEFAULT; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA cbdData{ &cb, 0, 0 };
        if (SUCCEEDED(device->CreateBuffer(&cbd, &cbdData, &mfdCb))) {
            context->PSSetConstantBuffers(0, 1, mfdCb.GetAddressOf());
        } else if (constantsBuffer) {
            context->UpdateSubresource(constantsBuffer, 0, nullptr, &cb, 0, 0);
            context->PSSetConstantBuffers(0, 1, &constantsBuffer);
        }

        context->Draw(3, 0);

        // Restore context state exactly
        ID3D11ShaderResourceView* origSrvPtr = origSrv.Get();
        context->PSSetShaderResources(0, 1, &origSrvPtr);
        ID3D11SamplerState* origSamplerPtr = origSampler.Get();
        context->PSSetSamplers(0, 1, &origSamplerPtr);
        ID3D11Buffer* origPsCbPtr = origPsCb.Get();
        context->PSSetConstantBuffers(0, 1, &origPsCbPtr);
        ID3D11Buffer* origVsCbPtr = origVsCb.Get();
        context->VSSetConstantBuffers(0, 1, &origVsCbPtr);
        context->VSSetShader(origVs.Get(), nullptr, 0);
        context->PSSetShader(origPs.Get(), nullptr, 0);
        context->OMSetBlendState(origBlend.Get(), origBlendFactor, origSampleMask);
        context->OMSetDepthStencilState(origDepth.Get(), origStencilRef);
        ID3D11RenderTargetView* origRtvPtr = origRtv.Get();
        context->OMSetRenderTargets(1, &origRtvPtr, origDsv.Get());
        if (numVp > 0) {
            context->RSSetViewports(1, &origVp);
        }
    }
}

const XrCompositionLayerBaseHeader* MfdManager::getOpenXrLayerHeader() {
    return nullptr;
}

} // namespace edvr::mfd
