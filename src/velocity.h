#pragma once
// Body velocity estimation from a weighted average of all tracked devices
// (6-point FBT friendly), smoothed with an EWMA. Works in RAW tracking space
// so results are unaffected by playspace offsets (including our own).

#include "config.h"
#include "vr_math.h"

#include <openvr.h>

#include <string>

struct BodyState
{
    Vec3 velocity;        // smoothed, horizontal, raw space, m/s
    double speed = 0.0;   // |velocity|
    double rawSpeed = 0.0; // weighted average before smoothing, m/s
    Vec3 hipForward;      // horizontal unit vector, raw space (yaw offset applied)
    Vec3 hipForwardRaw;   // same, before the yaw offset — for calibration
    Vec3 hmdForward;      // horizontal unit vector, raw space
    bool hipFromWaist = false;
    int devicesUsed = 0;

    // positions (raw space) for the body model / board mode
    Vec3 headPos;
    bool headValid = false;
    Vec3 handLeft, handRight;
    bool handLeftValid = false, handRightValid = false;
    Vec3 footLeft, footRight;
    Vec3 footLeftVel, footRightVel; // horizontal, raw space
    bool footLeftValid = false, footRightValid = false;
    vr::HmdMatrix34_t hipPose = {};
    bool hipPoseValid = false;
    Vec3 hipCorrected;    // hip pose * configured local offset
    Vec3 com;             // weighted center-of-mass estimate
    bool comValid = false;
};

class VelocityEstimator
{
public:
    // poses must be indexed by device id in TrackingUniverseRawAndUncalibrated.
    BodyState update( const vr::TrackedDevicePose_t* poses,
                      double dt,
                      const Config& cfg );

private:
    // One Euro filter, single axis (Casiez et al. 2012).
    struct OneEuroAxis
    {
        bool init = false;
        double x = 0.0, dx = 0.0, lastRaw = 0.0;
        double filter( double v, double dt, double minCutoff, double beta,
                       double dCutoff );
        void reset() { init = false; }
    };

    struct PartCache
    {
        bool valid = false;
        int part = 0;      // BodyPart enum value
        int side = 0;      // 0 unknown, 1 left, 2 right (feet/hands)
    };

    Vec3 m_smoothed;
    PartCache m_partCache[vr::k_unMaxTrackedDeviceCount];
    OneEuroAxis m_euroX, m_euroZ;
    OneEuroAxis m_euroMag;  // split mode: scalar speed filter
    Vec3 m_dirSmooth;       // split/envelope: smoothed unit direction
    double m_envSpeed = 0.0; // envelope mode: followed speed
    double m_stillTime = 0.0; // envelope mode: time below deadband
    std::string m_lastFilter;
    Vec3 m_lastPos[vr::k_unMaxTrackedDeviceCount];
    bool m_lastPosValid[vr::k_unMaxTrackedDeviceCount] = {};
};
