#pragma once
// Playspace offset backend — fixed-base model (as OVR Advanced Settings does):
//
// A BASE chaperone (standing-zero pose + collision bounds) is captured once —
// the pristine room with no artificial offset — and re-acquired whenever the
// chaperone universe changes (room setup / recenter). All motion is applied
// ABSOLUTELY against that fixed base: live pose = M_offset * base, where
// M_offset is an accumulated raw-side transform held in memory. The live
// chaperone is NEVER read back as a reference, so there is no drift.
//
// Reset simply zeroes M_offset and writes base pose + base bounds back —
// deterministic, and it always returns to the real room.
//
// Motion still commits in episodes (preview during motion, one Commit(Live)
// when it settles) so it composes with other tools and avoids per-frame
// commit races.

#include "vr_math.h"

#include <chrono>
#include <openvr.h>
#include <string>
#include <vector>

class Mover
{
public:
    // deltaRaw: artificial displacement this frame (raw space).
    // yawDelta: artificial rotation this frame (radians, about +Y) around
    // yawPivotRaw (raw space). Pass zeros while inactive.
    void update( const Vec3& deltaRaw, double yawDelta, const Vec3& yawPivotRaw,
                 double dt, bool adjustBounds );

    // Commit any open episode (used on shutdown when offsets should persist).
    void finish( bool adjustBounds );

    // Load base + offset from `path`; if no base is stored, capture the
    // current chaperone as base. When resetOnStart, snap back to base.
    void initOffset( const std::string& path, bool resetOnStart );
    // Snap the universe back to base now (offset 0, walls on the room).
    void resetOffset();
    // Call when SteamVR fires VREvent_ChaperoneUniverseHasChanged /
    // ...ZeroPoseReset. If the change came from OUTSIDE us (e.g. an OVRAS
    // floor fix) while we are idle and un-offset, absorb it into the base so
    // reset/dismount don't revert it. Our own commits are ignored.
    void onUniverseChanged();
    // Net translation applied since base, raw space (meters), for display.
    Vec3 offsetXYZ() const { return m_playerOffset; }

private:
    void commit( bool adjustBounds );
    bool captureBase();     // read the live chaperone into the base
    void writeState( const vr::HmdMatrix34_t& pose, bool adjustBounds,
                     bool commit );
    bool loadFile( const std::string& path );
    bool saveFile( const std::string& path ) const;
    vr::HmdMatrix34_t offsetPose() const; // M_offset * base
    bool offsetIsIdentity() const;

    bool m_active = false;
    double m_quietTime = 0.0;
    bool m_lastAdjustBounds = true;

    // fixed base (the real room) + accumulated offset
    bool m_haveBase = false;
    vr::HmdMatrix34_t m_basePose = {};
    std::vector<vr::HmdQuad_t> m_baseQuads;
    float m_basePlayW = 0.0f, m_basePlayD = 0.0f;
    vr::HmdMatrix34_t m_baseSeated = {};
    vr::HmdMatrix34_t m_offset = {}; // raw-side, identity = at base
    Vec3 m_playerOffset;             // sum of applied deltas, for display

    // last pose we committed + when, to tell our own chaperone changes apart
    // from external ones (OVRAS floor fix, room setup)
    vr::HmdMatrix34_t m_lastCommittedPose = {};
    bool m_haveLastCommitted = false;
    std::chrono::steady_clock::time_point m_lastCommitTime;

    std::vector<vr::HmdQuad_t> m_quadScratch;
    std::string m_path;

    static constexpr double kQuietCommitSeconds = 0.5;
};
