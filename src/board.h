#pragma once
// Board mode ("electric board", Onewheel-inspired, skateboard stance).
//
// Stage 1+2 scope: hip-offset-corrected CoM lean readout, throw-out /
// mount / dismount state machine, world-anchored board overlay image, and
// the universe lift while riding. The ride physics (lean integrator,
// pivot gate, artificial yaw) build on top of this in later stages.
//
// The board lives in RAW tracking space (room-fixed by construction, like
// all body math). The overlay transform is converted raw -> standing every
// frame via GetRawZeroPoseToStandingAbsoluteTrackingPose, so the drawn
// board stays put in the physical room even while the mover shifts the
// universe.

#include "config.h"
#include "velocity.h"
#include "vr_math.h"

#include <openvr.h>
#include <string>

enum class BoardPhase
{
    Stowed,      // on the belt
    Held,        // grabbed, waiting for release to throw
    Landed,      // on the ground, tail down, waiting for rear foot
    RearMounted, // rear foot on the tail
    Active       // both feet on: riding
};

struct BoardStatus
{
    BoardPhase phase = BoardPhase::Stowed;
    const char* phaseName = "stowed";
    bool suspendBoost = false; // true from RearMounted on
    double liftDeltaY = 0.0;   // per-frame universe lift delta to apply
    // lean; normalized by half stance length. along: + = toward front foot.
    double leanAlong = 0.0;   // after the zero-lean calibration bias
    double leanLateral = 0.0;
    double leanRawAlong = 0.0;   // before the bias (for the calibrate capture)
    double leanRawLateral = 0.0;
    bool leanValid = false;
    // ride output
    double simSpeed = 0.0;     // signed, m/s along the board axis
    Vec3 rideVelocity;         // raw space, m/s (kept during dismount decay)
    double yawDelta = 0.0;     // radians of artificial rotation this frame
    Vec3 yawPivot;             // raw space pivot for the rotation
    bool pivoting = false;     // low-speed pivot gate open
};

class Board
{
public:
    bool init( const std::string& baseDir );
    void shutdown();

    BoardStatus update( const BodyState& body, bool grabLeft, bool grabRight,
                        const Config& cfg, double dt );

    // Hard dismount: kick the rider off the board and zero its speed now (used
    // by wall-hit detection). Returns to the belt with no glide-out.
    void forceDismount();

private:
    void setPhase( BoardPhase p );
    void land( const BodyState& body, const Config& cfg );
    void updateOverlay( const BodyState& body, const Config& cfg, double dt );
    void updateMiniOverlay( const BodyState& body );
    bool footInZone( const Vec3& foot, double alongMin, double alongMax,
                     const Config& cfg ) const;

    vr::VROverlayHandle_t m_overlay = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t m_mini = vr::k_ulOverlayHandleInvalid;

    BoardPhase m_phase = BoardPhase::Stowed;
    int m_grabHand = 0; // 1 left, 2 right while Held
    Vec3 m_tail;        // raw space, on the floor
    Vec3 m_axis;        // horizontal unit vector, tail -> nose (riding dir)
    double m_floorY = 0.0; // raw-space y of the floor at the board
    double m_pitch = 0.0;  // current visual pitch (rad), lerps rest <-> 0
    double m_lift = 0.0;   // current universe lift applied
    // running minimum of each foot's raw y while mounted — the dismount
    // threshold measures from the planted height, not the (possibly mid-air)
    // height at the activation instant
    double m_footBaseRearY = 0.0, m_footBaseFrontY = 0.0;
    double m_raiseTimer = 0.0;
    double m_mountGrace = 0.0; // dismount ignored briefly after mounting
    double m_simSpeed = 0.0;   // signed ride speed along m_axis
    double m_pivotGate = 0.0;  // seconds remaining of lean gating
    bool m_grabWasDown = false;
};
