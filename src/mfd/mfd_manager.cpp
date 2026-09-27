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

    // Center/forward console MFD (default MFD)
    float posX = edvr::Config::get().getFloat("fix.mfd_pos_x", 0.0f);
    float posY = edvr::Config::get().getFloat("fix.mfd_pos_y", -0.16f);
    float posZ = edvr::Config::get().getFloat("fix.mfd_pos_z", -0.55f);
    float pitchDeg = edvr::Config::get().getFloat("fix.mfd_pitch", -20.0f);
    float yawDeg = edvr::Config::get().getFloat("fix.mfd_yaw", 0.0f);
    float scale = edvr::Config::get().getFloat("fix.mfd_scale", 1.0f);

    MfdPose poseCenter;
    poseCenter.position = Vec3(posX, posY, posZ);
    poseCenter.orientation = Quat::fromEulerDegrees(pitchDeg, yawDeg, 0.0f);
    poseCenter.widthM = 0.28f * (scale > 0.1f ? scale : 1.0f);
    poseCenter.heightM = 0.18f * (scale > 0.1f ? scale : 1.0f);

    auto providerCenter = std::make_unique<DeclarativeMfdProvider>("main_mfd", "SPANSH ROUTER");
    providerCenter->loadFromJson(R"({
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

    addSlot("main_mfd", std::move(providerCenter), poseCenter);
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
    if (!m_enabled || m_slots.empty() || !device || !context || !rtv || !vs || !ps || !sampler) return;
    if (viewportWidth == 0 || viewportHeight == 0) return;

    bool headLocked = edvr::Config::get().getBool("fix.mfd_head_locked", false);
    Vec3 eyePos(eyePose.position.x, eyePose.position.y, eyePose.position.z);
    Quat eyeRot(eyePose.orientation.x, eyePose.orientation.y, eyePose.orientation.z, eyePose.orientation.w);

    for (auto& slot : m_slots) {
        if (!slot.isVisible || !slot.renderer) continue;

        Vec3 eyeLocal;
        if (headLocked) {
            // In head-locked HUD mode, position is relative to current gaze direction
            eyeLocal = slot.pose.position;
        } else {
            // Transform MFD cockpit position to eye-local space
            Vec3 relPos = slot.pose.position - eyePos;
            Quat invEyeRot(-eyeRot.x, -eyeRot.y, -eyeRot.z, eyeRot.w);
            eyeLocal = invEyeRot.rotate(relPos);
        }

        m_debugStats.eyeLocalX = eyeLocal.x;
        m_debugStats.eyeLocalY = eyeLocal.y;
        m_debugStats.eyeLocalZ = eyeLocal.z;

        // Must be in front of the eye (-Z in OpenXR eye space)
        if (eyeLocal.z >= -0.05f) {
            m_debugStats.inFrustum = false;
            continue;
        }

        float zDist = -eyeLocal.z; // positive distance forward

        float tanX = eyeLocal.x / zDist;
        float tanY = eyeLocal.y / zDist;

        float tanLeft = std::tan(eyeFov.angleLeft);
        float tanRight = std::tan(eyeFov.angleRight);
        float tanUp = std::tan(eyeFov.angleUp);
        float tanDown = std::tan(eyeFov.angleDown);
        float tanW = tanRight - tanLeft;
        float tanH = tanUp - tanDown;
        if (tanW <= 1e-4f || tanH <= 1e-4f) {
            m_debugStats.inFrustum = false;
            continue;
        }

        float normX = (tanX - tanLeft) / tanW;
        float normY = (tanUp - tanY) / tanH;

        float cx = normX * static_cast<float>(viewportWidth);
        float cy = normY * static_cast<float>(viewportHeight);

        float pixW = (slot.pose.widthM / zDist) / tanW * static_cast<float>(viewportWidth);
        float pixH = (slot.pose.heightM / zDist) / tanH * static_cast<float>(viewportHeight);

        m_debugStats.screenX = cx;
        m_debugStats.screenY = cy;
        m_debugStats.screenW = pixW;
        m_debugStats.screenH = pixH;

        if (pixW < 10.0f || pixH < 10.0f) {
            m_debugStats.inFrustum = false;
            continue;
        }

        float vx = cx - pixW * 0.5f;
        float vy = cy - pixH * 0.5f;

        // Clip to viewport bounds
        if (vx + pixW < 0 || vx >= static_cast<float>(viewportWidth) ||
            vy + pixH < 0 || vy >= static_cast<float>(viewportHeight)) {
            m_debugStats.inFrustum = false;
            continue;
        }

        m_debugStats.inFrustum = true;
        m_debugStats.renderDraws++;

        ID3D11ShaderResourceView* mfdSrv = nullptr;
        slot.renderer->createOrUpdateD3D11Srv(device, context, &mfdSrv);
        if (!mfdSrv) continue;

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

        if (constantsBuffer) {
            context->UpdateSubresource(constantsBuffer, 0, nullptr, &cb, 0, 0);
            context->PSSetConstantBuffers(0, 1, &constantsBuffer);
        }

        context->Draw(3, 0);
    }
}

const XrCompositionLayerBaseHeader* MfdManager::getOpenXrLayerHeader() {
    return nullptr;
}

} // namespace edvr::mfd
