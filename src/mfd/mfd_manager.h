#pragma once

#include "mfd_types.h"
#include "mfd_provider.h"
#include "mfd_renderer.h"
#include "mfd_gaze_tracker.h"
#include "mfd_input_router.h"
#include "mfd_compositor.h"

#include <d3d11.h>
#include <memory>
#include <string>
#include <vector>

struct XrPosef;
struct XrFovf;
struct XrCompositionLayerBaseHeader;

namespace edvr::mfd {

// Representation of an in-cockpit MFD display slot.
struct MfdSlot {
    std::string name;
    MfdPose pose;
    std::unique_ptr<IMfdProvider> provider;
    std::unique_ptr<MfdRenderer> renderer;
    MfdGazeTracker gazeTracker;
    bool isVisible = true;
};

// Central manager coordinating in-cockpit MFD displays, gaze tracking,
// input routing, and rendering.
class MfdManager {
public:
    static MfdManager& instance();

    MfdManager();
    ~MfdManager();

    // Lifecycle
    bool initialize(int renderWidth = 512, int renderHeight = 384);
    void shutdown();
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    // Slot management
    bool addSlot(std::string name, std::unique_ptr<IMfdProvider> provider, const MfdPose& pose);
    MfdSlot* findSlot(const std::string& name);
    const std::vector<MfdSlot>& slots() const { return m_slots; }
    size_t slotCount() const { return m_slots.size(); }

    // Currently focused slot (if any)
    MfdSlot* focusedSlot();

    // Per-frame update (called from VR render loop)
    void update(const Vec3& headPos, const Vec3& headForward, float dtSeconds);

    // Render all visible MFD slots
    void render();

    // Render projected MFD onto eye swapchain RTV in 3D cockpit space
    void renderToEyeRtv(ID3D11Device* device, ID3D11DeviceContext* context,
                        ID3D11RenderTargetView* rtv,
                        const XrPosef& eyePose, const XrFovf& eyeFov,
                        uint32_t viewportWidth, uint32_t viewportHeight,
                        ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                        ID3D11SamplerState* sampler, ID3D11Buffer* constantsBuffer);

    // Layer header accessor for OpenXR frame boundary
    const XrCompositionLayerBaseHeader* getOpenXrLayerHeader();

    // Input routing
    MfdInputRouter& inputRouter() { return m_inputRouter; }

    // Compositor access (Option A: OpenXR Quad layer, Option B: D3D11 scene mesh)
    IMfdCompositor* compositor() { return m_compositor.get(); }
    void setCompositor(std::unique_ptr<IMfdCompositor> compositor) {
        m_compositor = std::move(compositor);
    }

private:
    bool m_enabled = false;
    int m_renderWidth = 512;
    int m_renderHeight = 384;

    std::vector<MfdSlot> m_slots;
    MfdInputRouter m_inputRouter;
    std::unique_ptr<IMfdCompositor> m_compositor;

    void ensureDefaultSlots();
};

} // namespace edvr::mfd
