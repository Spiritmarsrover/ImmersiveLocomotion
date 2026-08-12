#include "board.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr double kPi = 3.14159265358979323846;

// billboard basis at pos facing the head; fallback reference for the
// looking-straight-down case
vr::HmdMatrix34_t billboardAt( const Vec3& pos, const BodyState& body )
{
    Vec3 n = ( body.headPos - pos ).normalizedOrZero();
    if ( n.length() < 0.5 )
        n = { 0.0, 1.0, 0.0 };
    Vec3 up = { 0.0, 1.0, 0.0 };
    Vec3 x = { up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z,
               up.x * n.y - up.y * n.x };
    if ( x.length() < 0.2 )
    {
        Vec3 r = body.hmdForward.length() > 0.5 ? body.hmdForward
                                                : Vec3{ 1.0, 0.0, 0.0 };
        x = { r.y * n.z - r.z * n.y, r.z * n.x - r.x * n.z,
              r.x * n.y - r.y * n.x };
    }
    x = x.normalizedOrZero();
    Vec3 y = { n.y * x.z - n.z * x.y, n.z * x.x - n.x * x.z,
               n.x * x.y - n.y * x.x };

    vr::HmdMatrix34_t raw = {};
    raw.m[0][0] = static_cast<float>( x.x );
    raw.m[1][0] = static_cast<float>( x.y );
    raw.m[2][0] = static_cast<float>( x.z );
    raw.m[0][1] = static_cast<float>( y.x );
    raw.m[1][1] = static_cast<float>( y.y );
    raw.m[2][1] = static_cast<float>( y.z );
    raw.m[0][2] = static_cast<float>( n.x );
    raw.m[1][2] = static_cast<float>( n.y );
    raw.m[2][2] = static_cast<float>( n.z );
    raw.m[0][3] = static_cast<float>( pos.x );
    raw.m[1][3] = static_cast<float>( pos.y );
    raw.m[2][3] = static_cast<float>( pos.z );
    return raw;
}

const char* phaseName( BoardPhase p )
{
    switch ( p )
    {
    case BoardPhase::Stowed: return "stowed";
    case BoardPhase::Held: return "held";
    case BoardPhase::Landed: return "landed";
    case BoardPhase::RearMounted: return "rear mounted";
    case BoardPhase::Active: return "ACTIVE";
    }
    return "?";
}
} // namespace

bool Board::init( const std::string& baseDir )
{
    if ( !vr::VROverlay() )
        return false;
    vr::EVROverlayError err = vr::VROverlay()->CreateOverlay(
        "titan.il.board", "IL Board", &m_overlay );
    if ( err != vr::VROverlayError_None )
    {
        std::fprintf( stderr, "Board overlay create failed: %d\n", err );
        return false;
    }
    std::string tex = baseDir + "\\manifest\\board.png";
    vr::VROverlay()->SetOverlayFromFile( m_overlay, tex.c_str() );
    vr::VROverlay()->SetOverlaySortOrder( m_overlay, 0 );

    // mini board: stowed-at-hip grab indicator, follows the hand while held
    err = vr::VROverlay()->CreateOverlay( "titan.il.board_mini",
                                          "IL Board (stowed)", &m_mini );
    if ( err == vr::VROverlayError_None )
    {
        vr::VROverlay()->SetOverlayFromFile( m_mini, tex.c_str() );
        vr::VROverlay()->SetOverlaySortOrder( m_mini, 1 );
    }
    return true;
}

void Board::shutdown()
{
    if ( vr::VROverlay() )
    {
        if ( m_overlay != vr::k_ulOverlayHandleInvalid )
            vr::VROverlay()->DestroyOverlay( m_overlay );
        if ( m_mini != vr::k_ulOverlayHandleInvalid )
            vr::VROverlay()->DestroyOverlay( m_mini );
    }
}

void Board::setPhase( BoardPhase p )
{
    if ( p == m_phase )
        return;
    m_phase = p;
    if ( m_overlay == vr::k_ulOverlayHandleInvalid )
        return;
    switch ( p )
    {
    case BoardPhase::Stowed:
        vr::VROverlay()->HideOverlay( m_overlay );
        break;
    case BoardPhase::Held:
        vr::VROverlay()->SetOverlayColor( m_overlay, 1.0f, 1.0f, 1.0f );
        vr::VROverlay()->ShowOverlay( m_overlay );
        break;
    case BoardPhase::Landed:
        vr::VROverlay()->SetOverlayColor( m_overlay, 1.0f, 1.0f, 1.0f );
        vr::VROverlay()->ShowOverlay( m_overlay );
        break;
    case BoardPhase::RearMounted:
        vr::VROverlay()->SetOverlayColor( m_overlay, 1.0f, 0.85f, 0.35f );
        break;
    case BoardPhase::Active:
        vr::VROverlay()->SetOverlayColor( m_overlay, 0.45f, 1.0f, 0.55f );
        break;
    }
}

void Board::land( const BodyState& body, const Config& cfg )
{
    // Deterministic landing: throw_distance along the horizontal facing,
    // riding direction = facing at throw time. Floor height comes from the
    // standing universe (y = 0 plane) converted into raw space.
    Vec3 origin = body.hipPoseValid ? body.hipCorrected : body.headPos;
    Vec3 facing = body.hmdForward;
    if ( facing.length() < 0.5 )
        facing = { 0.0, 0.0, -1.0 };

    Vec3 center = origin.horizontal() + facing * cfg.throwDistance;

    vr::HmdMatrix34_t r2s
        = vr::VRSystem()->GetRawZeroPoseToStandingAbsoluteTrackingPose();
    // Solve raw y such that standing y == 0 at (x, z):
    // (r2s * p).y = m10 x + m11 y + m12 z + m13 = 0
    double m11 = r2s.m[1][1];
    if ( std::fabs( m11 ) < 0.5 )
        m11 = 1.0; // degenerate calibration; assume raw y up
    m_floorY = -( r2s.m[1][0] * center.x + r2s.m[1][2] * center.z
                  + r2s.m[1][3] )
               / m11;

    m_axis = facing;
    m_tail = { center.x - m_axis.x * cfg.boardLength * 0.5, m_floorY,
               center.z - m_axis.z * cfg.boardLength * 0.5 };
    m_pitch = cfg.restPitchDeg * kPi / 180.0;
}

bool Board::footInZone( const Vec3& foot, double alongMin, double alongMax,
                        const Config& cfg ) const
{
    Vec3 d = foot - m_tail;
    double along = d.x * m_axis.x + d.z * m_axis.z;
    double lat = d.x * -m_axis.z + d.z * m_axis.x;
    if ( along < alongMin || along > alongMax )
        return false;
    if ( std::fabs( lat ) > cfg.boardWidth * 0.5 + 0.10 )
        return false;
    // foot near the ground (raw-space height relative to the board floor)
    if ( foot.y - m_floorY > 0.30 )
        return false;
    return true;
}

void Board::updateMiniOverlay( const BodyState& body )
{
    if ( m_mini == vr::k_ulOverlayHandleInvalid )
        return;

    // permanent belt marker: at the hip tracker itself (where the board is
    // stowed on your belt), not the calibrated body-center point
    if ( !body.hipPoseValid || !body.headValid )
    {
        vr::VROverlay()->HideOverlay( m_mini );
        return;
    }
    vr::HmdMatrix34_t raw = billboardAt( positionOf( body.hipPose ), body );
    vr::HmdMatrix34_t r2s
        = vr::VRSystem()->GetRawZeroPoseToStandingAbsoluteTrackingPose();
    vr::HmdMatrix34_t standing = matMul34( r2s, raw );

    vr::VROverlay()->SetOverlayWidthInMeters( m_mini, 0.09f );
    vr::VROverlay()->SetOverlayTransformAbsolute(
        m_mini, vr::TrackingUniverseStanding, &standing );
    vr::VROverlay()->ShowOverlay( m_mini );
}

void Board::updateOverlay( const BodyState& body, const Config& cfg,
                           double dt )
{
    if ( m_overlay == vr::k_ulOverlayHandleInvalid )
        return;
    if ( m_phase == BoardPhase::Stowed )
        return;

    if ( m_phase == BoardPhase::Held )
    {
        // in the hand: mid-size billboard following the grabbing hand
        const Vec3& hand = m_grabHand == 1 ? body.handLeft : body.handRight;
        bool valid = m_grabHand == 1 ? body.handLeftValid
                                     : body.handRightValid;
        if ( !valid || !body.headValid )
            return;
        vr::HmdMatrix34_t raw = billboardAt( hand, body );
        vr::HmdMatrix34_t r2s
            = vr::VRSystem()->GetRawZeroPoseToStandingAbsoluteTrackingPose();
        vr::HmdMatrix34_t standing = matMul34( r2s, raw );
        vr::VROverlay()->SetOverlayWidthInMeters( m_overlay, 0.14f );
        vr::VROverlay()->SetOverlayTransformAbsolute(
            m_overlay, vr::TrackingUniverseStanding, &standing );
        return;
    }

    // visual pitch: rest angle while waiting, flat while ridden
    double targetPitch
        = ( m_phase == BoardPhase::Active ) ? 0.0
                                            : cfg.restPitchDeg * kPi / 180.0;
    double a = 1.0 - std::exp( -dt / 0.08 );
    m_pitch += ( targetPitch - m_pitch ) * a;

    // Basis in raw space. Overlay quad: local +x = width, local +y = length
    // (texture top = nose), normal +z = up out of the deck.
    double c = std::cos( m_pitch ), s = std::sin( m_pitch );
    Vec3 axisP = { m_axis.x * c, s, m_axis.z * c };  // tail->nose, pitched
    Vec3 up = { -m_axis.x * s, c, -m_axis.z * s };   // deck normal
    Vec3 perp = { -m_axis.z, 0.0, m_axis.x };        // width direction

    Vec3 center = m_tail + axisP * ( cfg.boardLength * 0.5 );
    center.y += 0.015; // keep the quad just above the floor

    vr::HmdMatrix34_t raw = {};
    raw.m[0][0] = static_cast<float>( perp.x );
    raw.m[1][0] = static_cast<float>( perp.y );
    raw.m[2][0] = static_cast<float>( perp.z );
    raw.m[0][1] = static_cast<float>( axisP.x );
    raw.m[1][1] = static_cast<float>( axisP.y );
    raw.m[2][1] = static_cast<float>( axisP.z );
    raw.m[0][2] = static_cast<float>( up.x );
    raw.m[1][2] = static_cast<float>( up.y );
    raw.m[2][2] = static_cast<float>( up.z );
    raw.m[0][3] = static_cast<float>( center.x );
    raw.m[1][3] = static_cast<float>( center.y );
    raw.m[2][3] = static_cast<float>( center.z );

    // Raw -> standing so SteamVR renders it room-fixed regardless of our
    // universe offsets.
    vr::HmdMatrix34_t r2s
        = vr::VRSystem()->GetRawZeroPoseToStandingAbsoluteTrackingPose();
    vr::HmdMatrix34_t standing = matMul34( r2s, raw );

    vr::VROverlay()->SetOverlayWidthInMeters(
        m_overlay, static_cast<float>( cfg.boardWidth ) );
    vr::VROverlay()->SetOverlayTransformAbsolute(
        m_overlay, vr::TrackingUniverseStanding, &standing );
}

BoardStatus Board::update( const BodyState& body, bool grabLeft,
                           bool grabRight, const Config& cfg, double dt )
{
    BoardStatus st;

    // Board mode disabled: put the board away, hide the overlays, decay any
    // residual ride speed, and report nothing.
    if ( !cfg.boardEnabled )
    {
        if ( m_phase != BoardPhase::Stowed )
            setPhase( BoardPhase::Stowed );
        if ( m_mini != vr::k_ulOverlayHandleInvalid && vr::VROverlay() )
            vr::VROverlay()->HideOverlay( m_mini );
        m_simSpeed = 0.0;
        m_grabWasDown = false;
        return st;
    }

    bool goofy = cfg.stance == "goofy";
    const Vec3& footRear = goofy ? body.footLeft : body.footRight;
    const Vec3& footFront = goofy ? body.footRight : body.footLeft;
    bool rearValid = goofy ? body.footLeftValid : body.footRightValid;
    bool frontValid = goofy ? body.footRightValid : body.footLeftValid;

    bool grabDown = grabLeft || grabRight;
    bool grabPressed = grabDown && !m_grabWasDown;
    bool grabReleased = !grabDown && m_grabWasDown;
    m_grabWasDown = grabDown;

    // hand near the hip tracker (where the stowed mini board is) = valid grab
    auto handNearHip = [&]( const Vec3& hand, bool valid ) {
        if ( !valid || !body.hipPoseValid )
            return false;
        return ( hand - positionOf( body.hipPose ) ).length() < cfg.grabRadius;
    };

    // --- lean (computed before the state machine so the sim can use it) ---
    if ( body.comValid && rearValid && frontValid )
    {
        Vec3 mid = ( footRear + footFront ) * 0.5;
        Vec3 axis;
        if ( m_phase == BoardPhase::Active
             || m_phase == BoardPhase::RearMounted )
            axis = m_axis;
        else
            axis = ( footFront - footRear ).horizontal().normalizedOrZero();
        if ( axis.length() > 0.5 )
        {
            double halfStance
                = 0.5 * ( footFront - footRear ).horizontal().length();
            if ( halfStance > 0.05 )
            {
                Vec3 d = ( body.com - mid ).horizontal();
                Vec3 perp = { -axis.z, 0.0, axis.x };
                st.leanAlong = Vec3::dot( d, axis ) / halfStance;
                st.leanLateral = Vec3::dot( d, perp ) / halfStance;
                st.leanValid = true;
            }
        }
    }

    switch ( m_phase )
    {
    case BoardPhase::Stowed:
        if ( grabPressed )
        {
            if ( grabLeft && handNearHip( body.handLeft, body.handLeftValid ) )
            {
                m_grabHand = 1;
                setPhase( BoardPhase::Held );
            }
            else if ( grabRight
                      && handNearHip( body.handRight, body.handRightValid ) )
            {
                m_grabHand = 2;
                setPhase( BoardPhase::Held );
            }
        }
        break;

    case BoardPhase::Held:
        if ( grabReleased )
        {
            // released while still at the belt = put it back, not a throw
            const Vec3& hand = m_grabHand == 1 ? body.handLeft
                                               : body.handRight;
            bool handValid = m_grabHand == 1 ? body.handLeftValid
                                             : body.handRightValid;
            if ( handNearHip( hand, handValid ) )
            {
                setPhase( BoardPhase::Stowed );
            }
            else
            {
                land( body, cfg );
                setPhase( BoardPhase::Landed );
            }
        }
        break;

    case BoardPhase::Landed:
    {
        // re-grab to stow; walk far away to auto-stow
        if ( grabPressed )
        {
            if ( grabLeft && handNearHip( body.handLeft, body.handLeftValid ) )
            {
                m_grabHand = 1;
                setPhase( BoardPhase::Held );
                break;
            }
            if ( grabRight
                 && handNearHip( body.handRight, body.handRightValid ) )
            {
                m_grabHand = 2;
                setPhase( BoardPhase::Held );
                break;
            }
        }
        if ( body.headValid )
        {
            Vec3 boardCenter
                = m_tail + m_axis * ( cfg.boardLength * 0.5 );
            if ( ( body.headPos.horizontal() - boardCenter.horizontal() )
                     .length()
                 > 3.0 )
            {
                setPhase( BoardPhase::Stowed );
                break;
            }
        }
        if ( rearValid
             && footInZone( footRear, -0.10, cfg.boardLength * 0.45, cfg ) )
        {
            setPhase( BoardPhase::RearMounted );
        }
        break;
    }

    case BoardPhase::RearMounted:
        // the belt board stays grabbable any time except Active — grabbing
        // recalls the single board instance to the hand
        if ( grabPressed )
        {
            if ( grabLeft && handNearHip( body.handLeft, body.handLeftValid ) )
            {
                m_grabHand = 1;
                setPhase( BoardPhase::Held );
                break;
            }
            if ( grabRight
                 && handNearHip( body.handRight, body.handRightValid ) )
            {
                m_grabHand = 2;
                setPhase( BoardPhase::Held );
                break;
            }
        }
        if ( !rearValid
             || !footInZone( footRear, -0.15, cfg.boardLength * 0.50, cfg ) )
        {
            setPhase( BoardPhase::Landed );
            break;
        }
        if ( frontValid
             && footInZone( footFront, cfg.boardLength * 0.45,
                            cfg.boardLength + 0.15, cfg ) )
        {
            m_footBaseRearY = footRear.y;
            m_footBaseFrontY = footFront.y;
            m_raiseTimer = 0.0;
            m_mountGrace = 0.7;
            setPhase( BoardPhase::Active );
        }
        break;

    case BoardPhase::Active:
    {
        // settle the baselines to the planted heights (activation may have
        // captured a foot mid-air, which made the dismount slider feel dead)
        if ( rearValid && footRear.y < m_footBaseRearY )
            m_footBaseRearY = footRear.y;
        if ( frontValid && footFront.y < m_footBaseFrontY )
            m_footBaseFrontY = footFront.y;

        // grace period right after mounting: the settling step must not
        // count as a dismount raise
        m_mountGrace -= dt;
        bool raise = false;
        if ( m_mountGrace <= 0.0 )
        {
            if ( rearValid
                 && footRear.y - m_footBaseRearY > cfg.dismountRaise )
                raise = true;
            if ( frontValid
                 && footFront.y - m_footBaseFrontY > cfg.dismountRaise )
                raise = true;
        }
        m_raiseTimer = raise ? m_raiseTimer + dt : 0.0;

        // Horizontal bail only above pivot speed — below it, horizontal
        // front-foot motion is the pivot gesture, not a dismount.
        bool bail = false;
        if ( std::fabs( m_simSpeed ) > cfg.pivotSpeed )
        {
            bool rearOn = rearValid
                          && footInZone( footRear, -0.25,
                                         cfg.boardLength * 0.6, cfg );
            bool frontOn = frontValid
                           && footInZone( footFront, cfg.boardLength * 0.35,
                                          cfg.boardLength + 0.25, cfg );
            bail = !rearOn || !frontOn;
        }

        if ( m_raiseTimer > 0.10 || bail )
            setPhase( BoardPhase::Stowed ); // auto-return to belt
        break;
    }
    }

    // --- universe lift toward target ---
    double liftTarget
        = ( m_phase == BoardPhase::Active ) ? cfg.liftHeight : 0.0;
    double la = 1.0 - std::exp( -dt / 0.06 );
    double newLift = m_lift + ( liftTarget - m_lift ) * la;
    if ( std::fabs( newLift - liftTarget ) < 1e-4 )
        newLift = liftTarget;
    st.liftDeltaY = newLift - m_lift;
    m_lift = newLift;

    // --- ride simulation ---
    if ( m_phase == BoardPhase::Active && rearValid && frontValid )
    {
        // board follows the feet: fast axis tracking during a pivot, slow
        // re-alignment otherwise; position stays under the stance
        Vec3 feetLine
            = ( footFront - footRear ).horizontal().normalizedOrZero();
        double atau = m_pivotGate > 0.0 ? 0.10 : 1.5;
        if ( feetLine.length() > 0.5 )
        {
            double aa = 1.0 - std::exp( -dt / atau );
            m_axis += ( feetLine - m_axis ) * aa;
            m_axis = m_axis.horizontal().normalizedOrZero();
        }
        Vec3 mid = ( ( footRear + footFront ) * 0.5 ).horizontal();
        m_tail = { mid.x - m_axis.x * cfg.boardLength * 0.5, m_floorY,
                   mid.z - m_axis.z * cfg.boardLength * 0.5 };

        // low-speed pivot gate: fast front-foot motion means the rider is
        // physically rotating — gate the lean input, freeze the speed
        const Vec3& frontVel = goofy ? body.footRightVel : body.footLeftVel;
        if ( std::fabs( m_simSpeed ) < cfg.pivotSpeed
             && frontVel.length() > cfg.pivotFootSpeed )
            m_pivotGate = 0.20;
        else
            m_pivotGate -= dt;
        st.pivoting = m_pivotGate > 0.0;

        if ( !st.pivoting && st.leanValid && m_mountGrace <= 0.0 )
        {
            auto deadzoned = []( double v, double dz ) {
                if ( std::fabs( v ) <= dz )
                    return 0.0;
                double s = v > 0.0 ? 1.0 : -1.0;
                return s * ( std::fabs( v ) - dz ) / ( 1.0 - dz );
            };
            // deadzone on the physical lean, then per-axis gain, clamp, then
            // gamma response curve
            auto gamma = []( double v, double g ) {
                if ( g == 1.0 )
                    return v;
                double s = v < 0.0 ? -1.0 : 1.0;
                return s * std::pow( std::fabs( v ), g );
            };
            double li = clamp( deadzoned( st.leanAlong, cfg.leanDeadzone )
                                   * cfg.leanGainAlong,
                               -1.0, 1.0 );
            li = gamma( li, cfg.leanGamma );
            double accel = cfg.accelGain * li
                           - cfg.dragCoeff * m_simSpeed
                                 * std::fabs( m_simSpeed );
            m_simSpeed += accel * dt;
            m_simSpeed = clamp( m_simSpeed, -cfg.boardMaxSpeed,
                                cfg.boardMaxSpeed );

            // lateral lean = carve: artificial yaw, ramping in around the
            // pivot speed so the two turn regimes hand off smoothly. Yaw
            // direction follows the SIGN of travel — a carve steers the
            // opposite way rolling backward (omega proportional to signed
            // velocity), matching a real board.
            double lt = clamp( deadzoned( st.leanLateral, cfg.turnDeadzone )
                                   * cfg.leanGainLateral,
                               -1.0, 1.0 );
            lt = gamma( lt, cfg.turnGamma );
            double ramp = clamp( ( std::fabs( m_simSpeed )
                                   - 0.7 * cfg.pivotSpeed )
                                     / ( 0.6 * cfg.pivotSpeed ),
                                 0.0, 1.0 );
            double travelSign = m_simSpeed >= 0.0 ? 1.0 : -1.0;
            st.yawDelta
                = cfg.turnRate * kPi / 180.0 * lt * ramp * travelSign * dt;
        }

        // rotation center for the artificial yaw
        if ( cfg.rotationCenter == "hmd" && body.headValid )
            st.yawPivot = body.headPos;
        else if ( cfg.rotationCenter == "com" && body.comValid )
            st.yawPivot = body.com;
        else
            st.yawPivot = ( footRear + footFront ) * 0.5;
    }
    else
    {
        // not riding: decay any remaining speed quickly (dismount glide-out)
        m_simSpeed *= std::exp( -dt / 0.15 );
        if ( std::fabs( m_simSpeed ) < 0.02 )
            m_simSpeed = 0.0;
    }
    st.simSpeed = m_simSpeed;
    st.rideVelocity = m_axis * m_simSpeed;

    updateOverlay( body, cfg, dt );
    updateMiniOverlay( body );

    st.phase = m_phase;
    st.phaseName = phaseName( m_phase );
    st.suspendBoost = ( m_phase == BoardPhase::RearMounted
                        || m_phase == BoardPhase::Active );
    return st;
}
