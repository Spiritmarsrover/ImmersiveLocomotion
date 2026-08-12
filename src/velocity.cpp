#include "velocity.h"

#include <cmath>
#include <cstring>
#include <string>

namespace
{
enum class BodyPart
{
    Hmd,
    Hand,
    Waist,
    Foot,
    Other
};

// side: 0 unknown, 1 left, 2 right
BodyPart classify( vr::TrackedDeviceIndex_t id, int& side )
{
    side = 0;
    vr::IVRSystem* sys = vr::VRSystem();
    vr::ETrackedDeviceClass cls = sys->GetTrackedDeviceClass( id );
    if ( cls == vr::TrackedDeviceClass_HMD )
        return BodyPart::Hmd;
    if ( cls == vr::TrackedDeviceClass_Controller )
    {
        vr::ETrackedControllerRole role
            = sys->GetControllerRoleForTrackedDeviceIndex( id );
        if ( role == vr::TrackedControllerRole_LeftHand )
            side = 1;
        else if ( role == vr::TrackedControllerRole_RightHand )
            side = 2;
        return BodyPart::Hand;
    }
    if ( cls != vr::TrackedDeviceClass_GenericTracker )
        return BodyPart::Other;

    char buf[vr::k_unMaxPropertyStringSize] = {};
    sys->GetStringTrackedDeviceProperty( id,
                                         vr::Prop_ControllerType_String,
                                         buf,
                                         sizeof( buf ) );
    std::string type = buf;
    if ( type.find( "waist" ) != std::string::npos
         || type.find( "chest" ) != std::string::npos )
        return BodyPart::Waist;
    if ( type.find( "foot" ) != std::string::npos
         || type.find( "ankle" ) != std::string::npos
         || type.find( "knee" ) != std::string::npos )
    {
        if ( type.find( "left" ) != std::string::npos )
            side = 1;
        else if ( type.find( "right" ) != std::string::npos )
            side = 2;
        return BodyPart::Foot;
    }
    return BodyPart::Other;
}
} // namespace

double VelocityEstimator::OneEuroAxis::filter( double v, double dt,
                                               double minCutoff, double beta,
                                               double dCutoff )
{
    auto alphaFor = [dt]( double cutoff ) {
        double tau = 1.0 / ( 2.0 * 3.14159265358979323846 * cutoff );
        return 1.0 / ( 1.0 + tau / dt );
    };
    if ( !init )
    {
        init = true;
        x = v;
        lastRaw = v;
        dx = 0.0;
        return v;
    }
    // Derivative from consecutive RAW samples (per the paper). Using the
    // filtered state here instead creates feedback: the filter's own lag
    // inflates the derivative, opens the adaptive cutoff, and defeats heavy
    // smoothing no matter how low min_cutoff is set.
    double dxRaw = ( v - lastRaw ) / dt;
    lastRaw = v;
    dx += ( dxRaw - dx ) * alphaFor( dCutoff > 0.01 ? dCutoff : 0.01 );
    double cutoff = minCutoff + beta * std::fabs( dx );
    x += ( v - x ) * alphaFor( cutoff > 0.01 ? cutoff : 0.01 );
    return x;
}

BodyState VelocityEstimator::update( const vr::TrackedDevicePose_t* poses,
                                     double dt,
                                     const Config& cfg )
{
    BodyState out;
    Vec3 weightedSum;
    double weightTotal = 0.0;
    Vec3 hmdForward;
    Vec3 waistForward;
    bool haveWaistForward = false;

    for ( vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount;
          ++i )
    {
        const vr::TrackedDevicePose_t& pose = poses[i];
        if ( !pose.bDeviceIsConnected || !pose.bPoseIsValid
             || pose.eTrackingResult != vr::TrackingResult_Running_OK )
        {
            m_lastPosValid[i] = false;
            m_partCache[i].valid = false;
            continue;
        }

        if ( !m_partCache[i].valid )
        {
            int side = 0;
            m_partCache[i].part
                = static_cast<int>( classify( i, side ) );
            m_partCache[i].side = side;
            m_partCache[i].valid = true;
        }
        BodyPart part = static_cast<BodyPart>( m_partCache[i].part );
        int side = m_partCache[i].side;
        Vec3 devPos = positionOf( pose.mDeviceToAbsoluteTracking );

        double w = cfg.weightOther;
        switch ( part )
        {
        case BodyPart::Hmd:
            w = cfg.weightHmd;
            hmdForward = forwardOf( pose.mDeviceToAbsoluteTracking )
                             .horizontal()
                             .normalizedOrZero();
            out.headPos = devPos;
            out.headValid = true;
            break;
        case BodyPart::Hand:
            w = cfg.weightHands;
            if ( side == 1 )
            {
                out.handLeft = devPos;
                out.handLeftValid = true;
            }
            else if ( side == 2 )
            {
                out.handRight = devPos;
                out.handRightValid = true;
            }
            break;
        case BodyPart::Waist:
            w = cfg.weightWaist;
            waistForward = forwardOf( pose.mDeviceToAbsoluteTracking )
                               .horizontal()
                               .normalizedOrZero();
            haveWaistForward = waistForward.length() > 0.5;
            out.hipPose = pose.mDeviceToAbsoluteTracking;
            out.hipPoseValid = true;
            break;
        case BodyPart::Foot:
            w = cfg.weightFeet;
            if ( side == 1 )
            {
                out.footLeft = devPos;
                out.footLeftVel = velocityOf( pose ).horizontal();
                out.footLeftValid = true;
            }
            else if ( side == 2 )
            {
                out.footRight = devPos;
                out.footRightVel = velocityOf( pose ).horizontal();
                out.footRightValid = true;
            }
            break;
        case BodyPart::Other:
            w = cfg.weightOther;
            break;
        }

        Vec3 v;
        if ( cfg.useDriverVelocity )
        {
            v = velocityOf( pose );
        }
        else
        {
            Vec3 p = positionOf( pose.mDeviceToAbsoluteTracking );
            if ( m_lastPosValid[i] && dt > 1e-4 )
                v = ( p - m_lastPos[i] ) * ( 1.0 / dt );
            m_lastPos[i] = p;
            m_lastPosValid[i] = true;
        }

        if ( w <= 0.0 )
            continue;
        weightedSum += v.horizontal() * w;
        weightTotal += w;
        out.devicesUsed++;
    }

    Vec3 raw = weightTotal > 0.0 ? weightedSum * ( 1.0 / weightTotal ) : Vec3{};
    out.rawSpeed = raw.length();

    // --- smoothing filter selection ---
    if ( cfg.filter != m_lastFilter )
    {
        // seed the newly selected filter from the current state so switching
        // live doesn't jump
        m_euroX.reset();
        m_euroZ.reset();
        m_euroMag.reset();
        m_envSpeed = m_smoothed.length();
        m_dirSmooth = m_smoothed.normalizedOrZero();
        m_stillTime = 0.0;
        m_lastFilter = cfg.filter;
    }
    if ( cfg.filter == "alpha" )
    {
        // classic fixed per-frame blend (frame-rate dependent by design)
        double a = clamp( cfg.alpha, 0.0, 1.0 );
        m_smoothed += ( raw - m_smoothed ) * a;
    }
    else if ( cfg.filter == "oneeuro" )
    {
        if ( !m_euroX.init )
        {
            // continuity with whatever the previous filter had settled on
            m_euroX.x = m_smoothed.x;
            m_euroZ.x = m_smoothed.z;
            m_euroX.lastRaw = raw.x;
            m_euroZ.lastRaw = raw.z;
            m_euroX.init = m_euroZ.init = true;
        }
        m_smoothed.x = m_euroX.filter( raw.x, dt, cfg.euroMinCutoff,
                                       cfg.euroBeta, cfg.euroDCutoff );
        m_smoothed.z = m_euroZ.filter( raw.z, dt, cfg.euroMinCutoff,
                                       cfg.euroBeta, cfg.euroDCutoff );
        m_smoothed.y = 0.0;
    }
    else if ( cfg.filter == "split" )
    {
        // Magnitude and direction smoothed independently. Speed barely
        // changes during a redirect, so filtering it as a scalar keeps the
        // boost flat through turns by construction (no vector cancellation),
        // while the direction EWMA follows the new heading quickly.
        if ( !m_euroMag.init )
        {
            m_euroMag.x = m_smoothed.length();
            m_euroMag.lastRaw = out.rawSpeed;
            m_euroMag.init = true;
            m_dirSmooth = m_smoothed.normalizedOrZero();
        }
        double mag = m_euroMag.filter( out.rawSpeed, dt, cfg.euroMinCutoff,
                                       cfg.euroBeta, cfg.euroDCutoff );

        // Follow direction only while actually moving; hold it when nearly
        // stationary so sensor noise cannot spin the boost heading.
        if ( out.rawSpeed > cfg.deadband * 0.5 )
        {
            Vec3 dirRaw = raw.normalizedOrZero();
            double dtau = cfg.directionTau;
            double dalpha = dtau > 1e-4 ? 1.0 - std::exp( -dt / dtau ) : 1.0;
            m_dirSmooth += ( dirRaw - m_dirSmooth ) * dalpha;
            m_dirSmooth = m_dirSmooth.normalizedOrZero();
        }
        m_smoothed = m_dirSmooth * mag;
    }
    else if ( cfg.filter == "envelope" )
    {
        // Asymmetric envelope follower for MRDW laps: speed rises fast and
        // decays slowly, so the genuine dips at redirect corners are coasted
        // over instead of filtered — a linear filter cannot separate a
        // corner dip from a real stop (same frequency band), but the boost
        // input can: releasing the pad is the stop signal, and the boost
        // envelope handles that quickly. Standing still past
        // stationary_timeout forces a fast decay as a safety net.
        bool still = out.rawSpeed < cfg.deadband;
        m_stillTime = still ? m_stillTime + dt : 0.0;

        double tau;
        if ( out.rawSpeed >= m_envSpeed )
            tau = cfg.magAttackTau;
        else if ( m_stillTime > cfg.stationaryTimeout )
            tau = cfg.magAttackTau; // clearly stationary: drop quickly
        else
            tau = cfg.magReleaseTau;
        double a = tau > 1e-4 ? 1.0 - std::exp( -dt / tau ) : 1.0;
        m_envSpeed += ( out.rawSpeed - m_envSpeed ) * a;

        // direction: fast EWMA, held while nearly stationary
        if ( out.rawSpeed > cfg.deadband * 0.5 )
        {
            Vec3 dirRaw = raw.normalizedOrZero();
            double dtau = cfg.directionTau;
            double dalpha = dtau > 1e-4 ? 1.0 - std::exp( -dt / dtau ) : 1.0;
            m_dirSmooth += ( dirRaw - m_dirSmooth ) * dalpha;
            m_dirSmooth = m_dirSmooth.normalizedOrZero();
        }
        m_smoothed = m_dirSmooth * m_envSpeed;
    }
    else
    {
        // ewma: frame-rate independent, alpha = 1 - exp(-dt/tau)
        double tau = cfg.smoothingTau;
        double alpha = tau > 1e-4 ? 1.0 - std::exp( -dt / tau ) : 1.0;
        m_smoothed += ( raw - m_smoothed ) * alpha;
    }

    out.velocity = m_smoothed;
    out.speed = m_smoothed.length();

    // Hip forward selection: waist tracker when available (or forced), else HMD.
    Vec3 fwd = hmdForward;
    if ( ( cfg.hipSource == "auto" || cfg.hipSource == "waist" )
         && haveWaistForward )
    {
        fwd = waistForward;
        out.hipFromWaist = true;
    }
    out.hipForwardRaw = fwd.normalizedOrZero();
    out.hmdForward = hmdForward;
    if ( cfg.hipYawOffsetDeg != 0.0 && fwd.length() > 0.5 )
    {
        double a = cfg.hipYawOffsetDeg * 3.14159265358979323846 / 180.0;
        double c = std::cos( a ), s = std::sin( a );
        fwd = Vec3{ c * fwd.x + s * fwd.z, 0.0, -s * fwd.x + c * fwd.z };
    }
    out.hipForward = fwd.normalizedOrZero();

    // --- center-of-mass estimate (weighted device positions) ---
    // The hip contribution uses the calibrated tracker-local offset so a
    // side-mounted tracker still reads as the body center.
    if ( out.hipPoseValid )
    {
        out.hipCorrected = transformPoint(
            out.hipPose,
            Vec3{ cfg.hipOffsetX, cfg.hipOffsetY, cfg.hipOffsetZ } );
    }
    {
        Vec3 sum;
        double wsum = 0.0;
        auto acc = [&]( bool valid, const Vec3& p, double w ) {
            if ( valid && w > 0.0 )
            {
                sum += p * w;
                wsum += w;
            }
        };
        acc( out.hipPoseValid, out.hipCorrected, cfg.comWeightHip );
        acc( out.headValid, out.headPos, cfg.comWeightHead );
        acc( out.handLeftValid, out.handLeft, cfg.comWeightHands );
        acc( out.handRightValid, out.handRight, cfg.comWeightHands );
        acc( out.footLeftValid, out.footLeft, cfg.comWeightFeet );
        acc( out.footRightValid, out.footRight, cfg.comWeightFeet );
        if ( wsum > 0.0 )
        {
            out.com = sum * ( 1.0 / wsum );
            out.comValid = true;
        }
    }
    return out;
}
