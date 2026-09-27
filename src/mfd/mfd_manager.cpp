#include "mfd_manager.h"
#include "../common/config.h"
#include "../common/log.h"
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

namespace {
struct MfdSharedState {
    volatile LONG initialized;
    volatile LONG draws;
    volatile LONG focusState;
    volatile LONG inFrustum;
    volatile LONG eyeX_mm;
    volatile LONG eyeY_mm;
    volatile LONG eyeZ_mm;
    volatile LONG screenX;
    volatile LONG screenY;
    volatile LONG screenW;
    volatile LONG screenH;
};

MfdSharedState* getMfdSharedState() {
    static MfdSharedState* s_shared = nullptr;
    if (s_shared) return s_shared;

    wchar_t name[64];
    swprintf_s(name, L"Local\\edvr_mfd_telemetry_v1_%lu", GetCurrentProcessId());
    HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(MfdSharedState), name);
    if (!hMap) return nullptr;

    s_shared = static_cast<MfdSharedState*>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MfdSharedState)));
    return s_shared;
}
} // namespace

void MfdManager::publishSharedTelemetry(uint32_t draws, int focusState, bool inFrustum,
                                       float eyeX, float eyeY, float eyeZ,
                                       float screenX, float screenY, float screenW, float screenH) {
    MfdSharedState* s = getMfdSharedState();
    if (!s) return;
    InterlockedExchange(&s->initialized, 1);
    InterlockedExchange(&s->draws, static_cast<LONG>(draws));
    InterlockedExchange(&s->focusState, static_cast<LONG>(focusState));
    InterlockedExchange(&s->inFrustum, inFrustum ? 1 : 0);
    InterlockedExchange(&s->eyeX_mm, static_cast<LONG>(std::round(eyeX * 1000.0f)));
    InterlockedExchange(&s->eyeY_mm, static_cast<LONG>(std::round(eyeY * 1000.0f)));
    InterlockedExchange(&s->eyeZ_mm, static_cast<LONG>(std::round(eyeZ * 1000.0f)));
    InterlockedExchange(&s->screenX, static_cast<LONG>(std::round(screenX)));
    InterlockedExchange(&s->screenY, static_cast<LONG>(std::round(screenY)));
    InterlockedExchange(&s->screenW, static_cast<LONG>(std::round(screenW)));
    InterlockedExchange(&s->screenH, static_cast<LONG>(std::round(screenH)));
}

bool MfdManager::readSharedTelemetry(uint32_t* draws, int* focusState, bool* inFrustum,
                                    float* eyeX, float* eyeY, float* eyeZ,
                                    float* screenX, float* screenY, float* screenW, float* screenH) {
    MfdSharedState* s = getMfdSharedState();
    if (!s || InterlockedCompareExchange(&s->initialized, 0, 0) == 0) return false;
    if (draws) *draws = static_cast<uint32_t>(InterlockedCompareExchange(&s->draws, 0, 0));
    if (focusState) *focusState = static_cast<int>(InterlockedCompareExchange(&s->focusState, 0, 0));
    if (inFrustum) *inFrustum = (InterlockedCompareExchange(&s->inFrustum, 0, 0) != 0);
    if (eyeX) *eyeX = static_cast<float>(InterlockedCompareExchange(&s->eyeX_mm, 0, 0)) / 1000.0f;
    if (eyeY) *eyeY = static_cast<float>(InterlockedCompareExchange(&s->eyeY_mm, 0, 0)) / 1000.0f;
    if (eyeZ) *eyeZ = static_cast<float>(InterlockedCompareExchange(&s->eyeZ_mm, 0, 0)) / 1000.0f;
    if (screenX) *screenX = static_cast<float>(InterlockedCompareExchange(&s->screenX, 0, 0));
    if (screenY) *screenY = static_cast<float>(InterlockedCompareExchange(&s->screenY, 0, 0));
    if (screenW) *screenW = static_cast<float>(InterlockedCompareExchange(&s->screenW, 0, 0));
    if (screenH) *screenH = static_cast<float>(InterlockedCompareExchange(&s->screenH, 0, 0));
    return true;
}

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

bool MfdManager::isEnabled() const {
    return m_enabled || edvr::Config::get().getBool("fix.cockpit_mfd", false);
}

bool MfdManager::initialize(int renderWidth, int renderHeight) {
    m_renderWidth = renderWidth;
    m_renderHeight = renderHeight;

    edvr::Config::get().init(edvr::executableDirectory());
    ensureDefaultSlots();
    m_enabled = edvr::Config::get().getBool("fix.cockpit_mfd", false);
    Log::get().note("mfd: initialized renderSize=(%dx%d), enabled=%d, default_slots=%zu\n",
                    m_renderWidth, m_renderHeight, isEnabled() ? 1 : 0, m_slots.size());

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
    edvr::Config::get().reloadIfChanged();
    if (!isEnabled()) return;
    ensureDefaultSlots();

    // Live update main MFD slot transform from config settings
    float posX = edvr::Config::get().getFloat("fix.mfd_pos_x", 0.0f);
    float posY = edvr::Config::get().getFloat("fix.mfd_pos_y", -0.16f);
    float posZ = edvr::Config::get().getFloat("fix.mfd_pos_z", -0.55f);
    float pitchDeg = edvr::Config::get().getFloat("fix.mfd_pitch", -20.0f);
    float yawDeg = edvr::Config::get().getFloat("fix.mfd_yaw", 0.0f);
    float scale = edvr::Config::get().getFloat("fix.mfd_scale", 1.0f);

    auto* mainSlot = findSlot("main_mfd");
    if (mainSlot) {
        mainSlot->pose.position = Vec3(posX, posY, posZ);
        mainSlot->pose.orientation = Quat::fromEulerDegrees(pitchDeg, yawDeg, 0.0f);
        mainSlot->pose.widthM = 0.28f * (scale > 0.1f ? scale : 1.0f);
        mainSlot->pose.heightM = 0.18f * (scale > 0.1f ? scale : 1.0f);
    }

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
    if (!isEnabled()) return;
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
    if (!isEnabled() || m_slots.empty() || !device || !context || !rtv || !vs || !ps || !sampler) return;
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

        static uint32_t s_cullLog = 0;
        // Must be in front of the eye (-Z in OpenXR eye space)
        if (eyeLocal.z >= -0.05f) {
            m_debugStats.inFrustum = false;
            if (s_cullLog < 10) {
                s_cullLog++;
                Log::get().note("mfd_cull: behind eye (z=%.2f >= -0.05)\n", eyeLocal.z);
            }
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
            if (s_cullLog < 10) {
                s_cullLog++;
                Log::get().note("mfd_cull: invalid FOV tangents (tanW=%.4f tanH=%.4f)\n", tanW, tanH);
            }
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
            if (s_cullLog < 10) {
                s_cullLog++;
                Log::get().note("mfd_cull: projected size too small (pixW=%.1f pixH=%.1f)\n", pixW, pixH);
            }
            continue;
        }

        float vx = cx - pixW * 0.5f;
        float vy = cy - pixH * 0.5f;

        // Clip to viewport bounds
        if (vx + pixW < 0 || vx >= static_cast<float>(viewportWidth) ||
            vy + pixH < 0 || vy >= static_cast<float>(viewportHeight)) {
            m_debugStats.inFrustum = false;
            if (s_cullLog < 10) {
                s_cullLog++;
                Log::get().note("mfd_cull: off-screen bounds (vx=%.1f vy=%.1f pixW=%.1f pixH=%.1f vp=(%ux%u))\n",
                                vx, vy, pixW, pixH, viewportWidth, viewportHeight);
            }
            continue;
        }

        m_debugStats.inFrustum = true;
        m_debugStats.renderDraws++;

        int focusStateInt = 0;
        auto* focused = focusedSlot();
        if (focused) {
            focusStateInt = (focused->gazeTracker.currentState() == MfdFocusState::kFocused) ? 2 : 1;
        }
        publishSharedTelemetry(m_debugStats.renderDraws, focusStateInt, m_debugStats.inFrustum,
                               m_debugStats.eyeLocalX, m_debugStats.eyeLocalY, m_debugStats.eyeLocalZ,
                               m_debugStats.screenX, m_debugStats.screenY, m_debugStats.screenW, m_debugStats.screenH);

        ID3D11ShaderResourceView* mfdSrv = nullptr;
        slot.renderer->createOrUpdateD3D11Srv(device, context, &mfdSrv);
        if (!mfdSrv) {
            if (s_cullLog < 10) {
                s_cullLog++;
                Log::get().note("mfd_cull: failed to create D3D11 SRV\n");
            }
            continue;
        }

        // Bind viewport for MFD overlay
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = std::max(0.0f, vx);
        vp.TopLeftY = std::max(0.0f, vy);
        vp.Width = std::min(static_cast<float>(viewportWidth) - vp.TopLeftX, pixW);
        vp.Height = std::min(static_cast<float>(viewportHeight) - vp.TopLeftY, pixH);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;

        if (m_debugStats.renderDraws <= 10 || (m_debugStats.renderDraws % 300) == 0) {
            Log::get().note("mfd_draw: #%u eyeLocal=(%.2f,%.2f,%.2f) vp=(%.0f,%.0f,%.0fx%.0f) headLocked=%d\n",
                            m_debugStats.renderDraws, eyeLocal.x, eyeLocal.y, eyeLocal.z,
                            vp.TopLeftX, vp.TopLeftY, vp.Width, vp.Height, headLocked ? 1 : 0);
        }

        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
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
