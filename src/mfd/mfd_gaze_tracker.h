#pragma once

#include "mfd_types.h"

namespace edvr::mfd {

// Tracks the pilot's head gaze relative to an MFD in seated tracking space.
// Implements angular/frustum ray intersection and hysteresis dwell-time filtering.
class MfdGazeTracker {
public:
    MfdGazeTracker();

    // Configuration parameters:
    void setDwellEnterTime(float seconds) { m_dwellEnterTime = seconds; }
    void setDwellExitTime(float seconds) { m_dwellExitTime = seconds; }
    void setConeAngleDegrees(float degrees) { m_coneAngleDegrees = degrees; }

    // Evaluates gaze on each frame.
    // headPos: Head position in seated space (meters)
    // headForward: Normalized forward vector of the HMD (Z-forward or -Z depending on convention)
    // mfdPose: Position, orientation, and dimensions of the target MFD
    // dtSeconds: Time delta since previous frame
    MfdFocusState update(const Vec3& headPos, const Vec3& headForward,
                         const MfdPose& mfdPose, float dtSeconds);

    MfdFocusState currentState() const { return m_state; }
    bool isFocused() const { return m_state == MfdFocusState::kFocused; }
    float gazeAngleDegrees() const { return m_lastGazeAngleDeg; }

    // Direct ray-plane intersection test:
    // Returns true if ray from head intersects the physical MFD rectangular surface.
    static bool testRayIntersection(const Vec3& headPos, const Vec3& headDir,
                                    const MfdPose& mfdPose, float* outDistance = nullptr);

    // Angular test:
    // Returns angle in degrees between head forward and direction vector to MFD center.
    static float computeGazeAngle(const Vec3& headPos, const Vec3& headForward,
                                 const Vec3& mfdCenter);

private:
    MfdFocusState m_state = MfdFocusState::kUnfocused;
    float m_dwellEnterTime = 0.12f;   // Time (s) gaze must dwell on MFD to trigger focus
    float m_dwellExitTime = 0.20f;    // Grace period (s) before focus is released
    float m_coneAngleDegrees = 18.0f; // Angular threshold for focus acquisition
    float m_timer = 0.0f;
    float m_lastGazeAngleDeg = 180.0f;
};

} // namespace edvr::mfd
