#include "mover.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>

namespace
{
vr::HmdMatrix34_t identity34()
{
    vr::HmdMatrix34_t m = {};
    m.m[0][0] = m.m[1][1] = m.m[2][2] = 1.0f;
    return m;
}
constexpr uint32_t kMagic = 0x494C425A; // "ILBZ"

void mlog( const std::string& path, const char* what, double y )
{
    if ( path.empty() )
        return;
    std::ofstream f( path + ".log", std::ios::app );
    if ( f.good() )
        f << what << " Y=" << y << "\n";
}
} // namespace

bool Mover::captureBase()
{
    vr::IVRChaperoneSetup* setup = vr::VRChaperoneSetup();
    if ( !setup )
        return false;
    // Do not capture until the chaperone is calibrated, or we grab a
    // near-identity pose (origin at the raw/HMD-ground spot) and reset would
    // teleport everything there. States >= 200 are errors (per OVRAS).
    if ( vr::VRChaperone() )
    {
        vr::ChaperoneCalibrationState cs
            = vr::VRChaperone()->GetCalibrationState();
        if ( cs >= 200 )
            return false;
    }
    setup->RevertWorkingCopy();
    vr::HmdMatrix34_t pose;
    if ( !setup->GetWorkingStandingZeroPoseToRawTrackingPose( &pose ) )
        return false;

    uint32_t n = 0;
    setup->GetWorkingCollisionBoundsInfo( nullptr, &n );
    std::vector<vr::HmdQuad_t> quads;
    if ( n > 0 && n < 4096 )
    {
        quads.resize( n );
        if ( !setup->GetWorkingCollisionBoundsInfo( quads.data(), &n ) )
            quads.clear();
    }
    // A valid room has collision bounds; if there are none the chaperone is
    // not ready yet (or the user has no boundary) — do not adopt this base.
    if ( quads.empty() )
        return false;

    m_basePose = pose;
    m_baseQuads = std::move( quads );
    m_basePlayW = m_basePlayD = 0.0f;
    setup->GetWorkingPlayAreaSize( &m_basePlayW, &m_basePlayD );
    m_baseSeated = identity34();
    setup->GetWorkingSeatedZeroPoseToRawTrackingPose( &m_baseSeated );
    m_haveBase = true;
    m_offset = identity34();
    m_playerOffset = {};
    // the base IS the current live pose now, so treat it as "last committed"
    m_lastCommittedPose = m_basePose;
    m_haveLastCommitted = true;
    m_lastCommitTime = std::chrono::steady_clock::now();
    mlog( m_path, "captureBase base", m_basePose.m[1][3] );
    return true;
}

vr::HmdMatrix34_t Mover::offsetPose() const
{
    return matMul34( m_offset, m_basePose );
}

// Writes a target standing-zero pose to the working copy. When committing and
// adjustBounds, the bounds are rebuilt from the BASE bounds so the walls stay
// on the physical room; during a preview the bounds are left untouched (the
// compositor renders the grid from working bounds + the live pose, so the
// base bounds already sit on the room).
void Mover::writeState( const vr::HmdMatrix34_t& pose, bool adjustBounds,
                        bool commit )
{
    vr::IVRChaperoneSetup* setup = vr::VRChaperoneSetup();
    if ( !setup )
        return;
    setup->SetWorkingStandingZeroPoseToRawTrackingPose( &pose );

    if ( commit && adjustBounds && !m_baseQuads.empty() )
    {
        // Walls fixed to the room: pose * corner' = base * base_corner
        //   => corner' = pose^-1 * base * base_corner
        vr::HmdMatrix34_t b = matMul34( invertRigid( pose ), m_basePose );
        m_quadScratch = m_baseQuads;
        for ( auto& quad : m_quadScratch )
            for ( int c = 0; c < 4; ++c )
            {
                Vec3 p = { quad.vCorners[c].v[0], quad.vCorners[c].v[1],
                           quad.vCorners[c].v[2] };
                p = transformPoint( b, p );
                quad.vCorners[c].v[0] = static_cast<float>( p.x );
                quad.vCorners[c].v[1] = static_cast<float>( p.y );
                quad.vCorners[c].v[2] = static_cast<float>( p.z );
            }
        setup->SetWorkingCollisionBoundsInfo(
            m_quadScratch.data(),
            static_cast<uint32_t>( m_quadScratch.size() ) );
    }

    if ( commit )
    {
        setup->CommitWorkingCopy( vr::EChaperoneConfigFile_Live );
        setup->HideWorkingSetPreview();
        m_lastCommittedPose = pose;
        m_haveLastCommitted = true;
        m_lastCommitTime = std::chrono::steady_clock::now();
    }
    else
    {
        setup->ShowWorkingSetPreview();
    }
}

void Mover::commit( bool adjustBounds )
{
    writeState( offsetPose(), adjustBounds, true );
    m_active = false;
    saveFile( m_path );
}

void Mover::update( const Vec3& deltaRaw, double yawDelta,
                    const Vec3& yawPivotRaw, double dt, bool adjustBounds )
{
    m_lastAdjustBounds = adjustBounds;
    if ( !m_haveBase )
    {
        // acquire the base lazily once the chaperone is calibrated
        captureBase();
        if ( !m_haveBase )
            return;
    }

    bool moving = deltaRaw.length() > 1e-9 || std::fabs( yawDelta ) > 1e-9;

    if ( !m_active )
    {
        if ( !moving )
            return;
        m_active = true;
        m_quietTime = 0.0;
    }

    if ( !moving )
    {
        m_quietTime += dt;
        if ( m_quietTime > kQuietCommitSeconds )
            commit( adjustBounds );
        return;
    }
    m_quietTime = 0.0;

    // Frame increment, raw side: rotate about the pivot, then translate.
    // T(-delta) * T(p) * R_y(yaw) * T(-p): rotation R_y, translation
    // p - R_y*p - delta.
    double c = std::cos( yawDelta ), s = std::sin( yawDelta );
    vr::HmdMatrix34_t inc = identity34();
    inc.m[0][0] = static_cast<float>( c );
    inc.m[0][2] = static_cast<float>( s );
    inc.m[2][0] = static_cast<float>( -s );
    inc.m[2][2] = static_cast<float>( c );
    Vec3 rp = { c * yawPivotRaw.x + s * yawPivotRaw.z, yawPivotRaw.y,
                -s * yawPivotRaw.x + c * yawPivotRaw.z };
    inc.m[0][3] = static_cast<float>( yawPivotRaw.x - rp.x - deltaRaw.x );
    inc.m[1][3] = static_cast<float>( -deltaRaw.y );
    inc.m[2][3] = static_cast<float>( yawPivotRaw.z - rp.z - deltaRaw.z );

    m_offset = matMul34( inc, m_offset );
    m_playerOffset += deltaRaw;

    writeState( offsetPose(), adjustBounds, false );
}

void Mover::finish( bool adjustBounds )
{
    if ( m_active )
        commit( adjustBounds );
}

void Mover::resetOffset()
{
    if ( !m_haveBase )
        return;
    m_offset = identity34();
    m_playerOffset = {};
    mlog( m_path, "reset writes base", m_basePose.m[1][3] );
    // The pose we are about to settle to is the base; record it so the
    // absorb-gate treats the imminent commit as ours (not external).
    m_lastCommittedPose = m_basePose;
    m_haveLastCommitted = true;
    m_lastCommitTime = std::chrono::steady_clock::now();
    // Apply the base through the SAME preview-then-settle path a boost uses:
    // stream a working-set preview now and let the normal quiet-commit (in
    // update) finalize it a moment later. A cold Commit here is treated by
    // OVRAS as a zero-pose reset and it recenters onto the HMD; a
    // previewed-then-committed change is seen as ordinary motion and ignored.
    m_active = true;
    m_quietTime = 0.0;
    writeState( m_basePose, m_lastAdjustBounds, false );
    saveFile( m_path );
}

namespace
{
bool poseApproxEqual( const vr::HmdMatrix34_t& a, const vr::HmdMatrix34_t& b )
{
    // translation within 5 mm and rotation columns within ~0.5 deg
    for ( int i = 0; i < 3; ++i )
        if ( std::fabs( a.m[i][3] - b.m[i][3] ) > 0.005 )
            return false;
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            if ( std::fabs( a.m[i][j] - b.m[i][j] ) > 0.01 )
                return false;
    return true;
}
} // namespace

bool Mover::offsetIsIdentity() const
{
    const vr::HmdMatrix34_t id = identity34();
    return poseApproxEqual( m_offset, id );
}

void Mover::onUniverseChanged()
{
    // Only absorb changes we did NOT make, and only while idle and un-offset
    // (so we don't bake a boost/ride offset or fight our own commits).
    if ( m_active || !m_haveBase || !offsetIsIdentity() )
        return;
    // Ignore anything close in time to our own commit (our commit — and any
    // tool reacting to it — fires this event too).
    if ( m_haveLastCommitted )
    {
        double since = std::chrono::duration<double>(
                           std::chrono::steady_clock::now() - m_lastCommitTime )
                           .count();
        if ( since < 1.0 )
            return;
    }

    vr::IVRChaperoneSetup* setup = vr::VRChaperoneSetup();
    if ( !setup )
        return;
    setup->RevertWorkingCopy();
    vr::HmdMatrix34_t live;
    if ( !setup->GetWorkingStandingZeroPoseToRawTrackingPose( &live ) )
        return;
    mlog( m_path, "onUniverse live", live.m[1][3] );
    mlog( m_path, "onUniverse lastComm", m_lastCommittedPose.m[1][3] );
    // If the live pose still matches what we last committed, nothing external
    // actually changed (this was our own event) — leave the base alone.
    if ( m_haveLastCommitted && poseApproxEqual( live, m_lastCommittedPose ) )
        return;

    // External change (e.g. OVRAS floor fix) — adopt it as the new base so
    // reset and dismount return to the corrected floor, not the old one.
    if ( captureBase() )
    {
        saveFile( m_path );
        std::printf( "External chaperone change absorbed into base.\n" );
    }
}

bool Mover::saveFile( const std::string& path ) const
{
    if ( path.empty() || !m_haveBase )
        return false;
    std::ofstream f( path, std::ios::binary );
    if ( !f.good() )
        return false;
    uint32_t magic = kMagic;
    f.write( reinterpret_cast<const char*>( &magic ), sizeof( magic ) );
    f.write( reinterpret_cast<const char*>( &m_basePose ),
             sizeof( m_basePose ) );
    uint32_t n = static_cast<uint32_t>( m_baseQuads.size() );
    f.write( reinterpret_cast<const char*>( &n ), sizeof( n ) );
    if ( n > 0 )
        f.write( reinterpret_cast<const char*>( m_baseQuads.data() ),
                 static_cast<std::streamsize>( n * sizeof( vr::HmdQuad_t ) ) );
    f.write( reinterpret_cast<const char*>( &m_offset ), sizeof( m_offset ) );
    double xyz[3] = { m_playerOffset.x, m_playerOffset.y, m_playerOffset.z };
    f.write( reinterpret_cast<const char*>( xyz ), sizeof( xyz ) );
    float play[2] = { m_basePlayW, m_basePlayD };
    f.write( reinterpret_cast<const char*>( play ), sizeof( play ) );
    f.write( reinterpret_cast<const char*>( &m_baseSeated ),
             sizeof( m_baseSeated ) );
    return f.good();
}

bool Mover::loadFile( const std::string& path )
{
    std::ifstream f( path, std::ios::binary );
    if ( !f.good() )
        return false;
    uint32_t magic = 0;
    f.read( reinterpret_cast<char*>( &magic ), sizeof( magic ) );
    if ( !f.good() || magic != kMagic )
        return false;
    vr::HmdMatrix34_t basePose;
    f.read( reinterpret_cast<char*>( &basePose ), sizeof( basePose ) );
    uint32_t n = 0;
    f.read( reinterpret_cast<char*>( &n ), sizeof( n ) );
    if ( !f.good() || n >= 4096 )
        return false;
    std::vector<vr::HmdQuad_t> quads;
    if ( n > 0 )
    {
        quads.resize( n );
        f.read( reinterpret_cast<char*>( quads.data() ),
                static_cast<std::streamsize>( n * sizeof( vr::HmdQuad_t ) ) );
    }
    vr::HmdMatrix34_t offset;
    f.read( reinterpret_cast<char*>( &offset ), sizeof( offset ) );
    double xyz[3] = {};
    f.read( reinterpret_cast<char*>( xyz ), sizeof( xyz ) );
    float play[2] = { 0.0f, 0.0f };
    f.read( reinterpret_cast<char*>( play ), sizeof( play ) );
    vr::HmdMatrix34_t seated;
    f.read( reinterpret_cast<char*>( &seated ), sizeof( seated ) );
    if ( !f.good() )
        return false;
    m_basePose = basePose;
    m_baseQuads = std::move( quads );
    m_offset = offset;
    m_playerOffset = { xyz[0], xyz[1], xyz[2] };
    m_basePlayW = play[0];
    m_basePlayD = play[1];
    m_baseSeated = seated;
    m_haveBase = true;
    // loaded base is the "last committed" reference so the absorb-gate works
    // from the first frame (it otherwise ran ungated during startup)
    m_lastCommittedPose = m_basePose;
    m_haveLastCommitted = true;
    m_lastCommitTime = std::chrono::steady_clock::now();
    mlog( m_path, "loadFile base", m_basePose.m[1][3] );
    mlog( m_path, "loadFile offset", offset.m[1][3] );
    return true;
}

void Mover::initOffset( const std::string& path, bool resetOnStart )
{
    m_path = path;
    // reference: HMD height in raw space + current live standing-zero Y
    if ( vr::VRSystem() )
    {
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseRawAndUncalibrated, 0.0f, poses,
            vr::k_unMaxTrackedDeviceCount );
        mlog( path, "init HMD raw",
              poses[vr::k_unTrackedDeviceIndex_Hmd]
                  .mDeviceToAbsoluteTracking.m[1][3] );
    }
    if ( vr::VRChaperoneSetup() )
    {
        vr::VRChaperoneSetup()->RevertWorkingCopy();
        vr::HmdMatrix34_t live;
        if ( vr::VRChaperoneSetup()
                 ->GetWorkingStandingZeroPoseToRawTrackingPose( &live ) )
            mlog( path, "init live standing", live.m[1][3] );
    }
    if ( loadFile( path ) )
    {
        std::printf( "Base chaperone loaded; offset %.2f %.2f %.2f m.\n",
                     m_playerOffset.x, m_playerOffset.y, m_playerOffset.z );

        // If the chaperone changed while we were off (room setup, floor fix,
        // recalibration) and we have no pending offset, the SAVED base is
        // stale — adopt the current live chaperone instead of writing the old
        // one back (which would revert the floor and lift/drop the player).
        bool adopted = false;
        if ( offsetIsIdentity() )
        {
            vr::IVRChaperoneSetup* setup = vr::VRChaperoneSetup();
            if ( setup )
            {
                setup->RevertWorkingCopy();
                vr::HmdMatrix34_t live;
                if ( setup->GetWorkingStandingZeroPoseToRawTrackingPose(
                         &live )
                     && !poseApproxEqual( live, m_basePose ) )
                {
                    mlog( path, "init: live != saved base, adopting live",
                          live.m[1][3] );
                    if ( captureBase() )
                    {
                        saveFile( path );
                        adopted = true;
                        std::printf( "Chaperone changed while off; adopted "
                                     "live as base.\n" );
                    }
                }
            }
        }

        if ( !adopted && resetOnStart )
        {
            resetOffset();
            std::printf( "Playspace reset to base on start.\n" );
        }
    }
    else if ( captureBase() )
    {
        saveFile( path );
        std::printf( "Base chaperone captured. If it is offset/rotated, run "
                     "SteamVR room setup to re-acquire a clean base.\n" );
    }
}
