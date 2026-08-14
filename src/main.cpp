// ImmersiveLocomotion — SteamVR overlay that boosts real walking with a
// proportional artificial velocity ("motorized rollerblades"), applied as a
// playspace offset that composes with OVR Advanced Settings.

#include "board.h"
#include "boost_model.h"
#include "config.h"
#include "osc.h"
#include "input.h"
#include "mover.h"
#include "overlay_ui.h"
#include "velocity.h"
#include "vr_math.h"

#include <openvr.h>

#include <windows.h>

#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

namespace
{
volatile bool g_running = true;

void onSignal( int )
{
    g_running = false;
}

std::string exeDir()
{
    char buf[MAX_PATH] = {};
    GetModuleFileNameA( nullptr, buf, MAX_PATH );
    std::string s = buf;
    size_t slash = s.find_last_of( "\\/" );
    return slash == std::string::npos ? "." : s.substr( 0, slash );
}

bool initVR()
{
    // Keep retrying so the overlay can be started before SteamVR (or
    // autostarted) and simply waits for the runtime to come up.
    vr::EVRInitError err = vr::VRInitError_None;
    while ( g_running )
    {
        vr::VR_Init( &err, vr::VRApplication_Overlay );
        if ( err == vr::VRInitError_None )
            return true;
        std::printf( "Waiting for SteamVR (%s)...\r",
                     vr::VR_GetVRInitErrorAsEnglishDescription( err ) );
        std::fflush( stdout );
        std::this_thread::sleep_for( std::chrono::seconds( 2 ) );
    }
    return false;
}
} // namespace

int main()
{
    std::signal( SIGINT, onSignal );
    timeBeginPeriod( 1 );

    std::printf( "ImmersiveLocomotion - rollerblade boost overlay\n" );

    if ( !g_running || !initVR() )
        return 1;

    const std::string dir = exeDir();

    // Register the application manifest so SteamVR remembers bindings and can
    // autostart the overlay. Failure is non-fatal (e.g. first run elsewhere).
    const std::string appManifest = dir + "\\manifest\\immersive_locomotion.vrmanifest";
    vr::VRApplications()->AddApplicationManifest( appManifest.c_str() );
    vr::VRApplications()->IdentifyApplication(
        GetCurrentProcessId(), "spiritmarsrover.immersive_locomotion" );

    Config cfg;
    cfg.path = dir + "\\immersive_locomotion.ini";
    cfg.loadOrCreate();
    std::printf( "Config: %s (hot-reloaded on save)\n", cfg.path.c_str() );

    InputManager input;
    if ( !input.init( dir + "\\manifest\\action_manifest.json" ) )
    {
        std::fprintf( stderr, "Input init failed; boost input will read 0.\n" );
    }

    VelocityEstimator estimator;
    BoostModel model;
    Mover mover;
    mover.initOffset( dir + "\\playspace_offset.dat", cfg.resetOnStart );

    OverlayUI ui;
    if ( !ui.init( dir ) )
        std::fprintf( stderr,
                      "Dashboard overlay UI unavailable; ini editing still "
                      "works.\n" );

    Board board;
    if ( !board.init( dir ) )
        std::fprintf( stderr, "Board overlay unavailable.\n" );

    OscSender osc;

    float displayHz = vr::VRSystem()->GetFloatTrackedDeviceProperty(
        vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float );
    if ( displayHz < 30.0f || displayHz > 500.0f )
        displayHz = 90.0f;
    const double targetDt = 1.0 / static_cast<double>( displayHz );
    std::printf( "Update rate: %.0f Hz\n", displayHz );

    auto lastTime = std::chrono::steady_clock::now();
    auto lastStatus = lastTime;
    auto lastCfgPoll = lastTime;
    double statAccumBoost = 0.0; // meters moved artificially (session)

    while ( g_running )
    {
        auto frameStart = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>( frameStart - lastTime ).count();
        lastTime = frameStart;
        dt = clamp( dt, 1e-4, 0.1 );

        // --- SteamVR events ---
        vr::VREvent_t ev;
        while ( vr::VRSystem()->PollNextEvent( &ev, sizeof( ev ) ) )
        {
            if ( ev.eventType == vr::VREvent_Quit )
            {
                vr::VRSystem()->AcknowledgeQuit_Exiting();
                g_running = false;
            }
            else if ( ev.eventType == vr::VREvent_DriverRequestedQuit )
            {
                g_running = false;
            }
            else if ( ev.eventType == vr::VREvent_ChaperoneUniverseHasChanged
                      || ev.eventType == vr::VREvent_StandingZeroPoseReset
                      || ev.eventType == vr::VREvent_SeatedZeroPoseReset )
            {
                // Absorb an EXTERNAL chaperone change (e.g. OVRAS floor fix)
                // into our base so reset/dismount don't revert it. The mover
                // gates this so our own commits are ignored.
                mover.onUniverseChanged();
            }
        }
        if ( !g_running )
            break;

        // --- config hot reload (1 Hz poll) ---
        if ( std::chrono::duration<double>( frameStart - lastCfgPoll ).count()
             > 1.0 )
        {
            lastCfgPoll = frameStart;
            if ( cfg.pollReload() )
                std::printf( "\n[config reloaded]\n" );
        }

        // --- poses in RAW space (unaffected by playspace offsets) ---
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
        vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseRawAndUncalibrated,
            0.0f,
            poses,
            vr::k_unMaxTrackedDeviceCount );

        BodyState body = estimator.update( poses, dt, cfg );

        input.update();
        double in = input.boostInput( cfg );

        BoardStatus bs = board.update( body, input.grabLeft(),
                                       input.grabRight(), cfg, dt );
        if ( bs.suspendBoost )
            in = 0.0; // mounting the board suspends the rollerblade path

        BoostResult boost = model.update( body, in, dt, cfg );

        // --- apply ---
        Vec3 applied = boost.velocity;
        if ( cfg.maxTotalSpeed > 0.0 )
        {
            double allowed = cfg.maxTotalSpeed - body.speed;
            allowed = allowed < 0.0 ? 0.0 : allowed;
            if ( boost.speed > allowed && boost.speed > 1e-9 )
                applied = applied * ( allowed / boost.speed );
        }
        double appliedSpeed = applied.length();
        Vec3 frameDelta;
        // cfg.enabled is the BOOST (rollerblade) enable
        if ( cfg.enabled && appliedSpeed > 1e-4 )
        {
            frameDelta = applied * dt;
            statAccumBoost += appliedSpeed * dt;
        }
        // Board deltas apply whenever the board produced them; board.update
        // already returns zeros when board mode is disabled.
        double boardSpeed = bs.rideVelocity.length();
        frameDelta.y += bs.liftDeltaY; // board ride lift
        if ( boardSpeed > 1e-4 )
        {
            frameDelta += bs.rideVelocity * dt;
            statAccumBoost += boardSpeed * dt;
        }
        double yaw = bs.yawDelta;
        mover.update( frameDelta, yaw, bs.yawPivot, dt, cfg.adjustBounds );

        // --- OSC avatar output: board active + wheel speed (every frame, so
        // the avatar's motor pitch/volume gets a smooth signal, not steps) ---
        if ( cfg.oscEnabled )
        {
            osc.setDest( cfg.oscIp, static_cast<int>( cfg.oscPort ) );
            bool boardActive;
            double norm;
            if ( ui.oscDebug() )
            {
                boardActive = ui.oscDebugActive();
                norm = ui.oscDebugSpeed();
            }
            else
            {
                boardActive = ( bs.phase == BoardPhase::Active );
                norm = cfg.boardMaxSpeed > 0.01
                           ? bs.simSpeed / cfg.boardMaxSpeed
                           : 0.0;
                norm = clamp( norm, -1.0, 1.0 );
            }
            osc.sendBool( "/avatar/parameters/IL_BoardActive", boardActive );
            osc.sendFloat( "/avatar/parameters/IL_BoardSpeed",
                           static_cast<float>( norm ) );
        }

        // --- dashboard UI ---
        UiStatus st;
        st.realSpeed = body.speed;
        st.rawSpeed = body.rawSpeed;
        st.input = in;
        st.hipWeight = boost.hipWeight;
        st.hipFromWaist = body.hipFromWaist;
        st.boostSpeed = appliedSpeed + boardSpeed;
        st.stamina = boost.stamina;
        st.exhausted = boost.exhausted;
        st.boostDistance = statAccumBoost;
        st.devices = body.devicesUsed;
        st.boardPhase = bs.phaseName;
        st.leanAlong = bs.leanAlong;
        st.leanLateral = bs.leanLateral;
        st.leanValid = bs.leanValid;
        st.boardSpeed = bs.simSpeed;
        st.boardPivoting = bs.pivoting;
        st.hipOffsetSet = ( cfg.hipOffsetX != 0.0 || cfg.hipOffsetY != 0.0
                            || cfg.hipOffsetZ != 0.0 );
        {
            Vec3 off = mover.offsetXYZ();
            st.offsetX = off.x;
            st.offsetY = off.y;
            st.offsetZ = off.z;
        }
        ui.update( cfg, st, dt );

        if ( ui.consumeResetHome() )
            mover.resetOffset();
        if ( ui.consumeQuit() )
            g_running = false;
        if ( ui.consumeOpenBindings() )
            vr::VRInput()->OpenBindingUI( "spiritmarsrover.immersive_locomotion",
                                          vr::k_ulInvalidActionSetHandle,
                                          vr::k_ulInvalidInputValueHandle,
                                          false );

        if ( ui.consumeHipOffsetCalibrate() )
        {
            if ( body.hipPoseValid && body.headValid )
            {
                // Body center = the head center dropped straight down onto
                // the horizontal plane at hip height. The HMD sits at the
                // front of the face, ~10 cm ahead of the head's center, so
                // back the reference off along the (horizontal) look
                // direction first — otherwise the body-center estimate is
                // biased forward and a balanced stance reads a false forward
                // lean. Stand in your riding stance and click.
                const double kHeadCenterBack = 0.10; // m, HMD -> head center
                Vec3 hipPos = positionOf( body.hipPose );
                Vec3 headCenter
                    = body.headPos - body.hmdForward * kHeadCenterBack;
                Vec3 projected = { headCenter.x, hipPos.y, headCenter.z };
                Vec3 local = rotateInverse( body.hipPose, projected - hipPos );
                cfg.hipOffsetX = local.x;
                cfg.hipOffsetY = local.y;
                cfg.hipOffsetZ = local.z;
                cfg.save();
                std::printf(
                    "\n[hip offset calibrated: %.3f %.3f %.3f (local)]\n",
                    local.x, local.y, local.z );
            }
            else
            {
                std::printf( "\n[hip offset calibration needs hip tracker + "
                             "HMD tracked]\n" );
            }
        }

        if ( ui.consumeCalibrate() )
        {
            // Yaw offset that rotates the hip device's raw forward onto the
            // HMD forward: theta = signed angle around +Y from hip to hmd.
            const Vec3& hip = body.hipForwardRaw;
            const Vec3& hmd = body.hmdForward;
            if ( hip.length() > 0.5 && hmd.length() > 0.5 )
            {
                double deg = std::atan2( hip.z * hmd.x - hip.x * hmd.z,
                                         hip.x * hmd.x + hip.z * hmd.z )
                             * 180.0 / 3.14159265358979323846;
                cfg.hipYawOffsetDeg = deg;
                cfg.save();
                std::printf( "\n[hip calibrated: yaw offset %.1f deg]\n",
                             deg );
            }
        }

        // --- status line 2 Hz ---
        if ( std::chrono::duration<double>( frameStart - lastStatus ).count()
             > 0.5 )
        {
            lastStatus = frameStart;
            char stamina[32];
            if ( boost.stamina < 0.0 )
                std::snprintf( stamina, sizeof( stamina ), "inf" );
            else
                std::snprintf( stamina, sizeof( stamina ), "%5.1fm%s",
                               boost.stamina,
                               boost.exhausted ? " EXHAUSTED" : "" );
            std::printf(
                "\rreal %4.2f m/s | in %4.2f | hip %4.2f%s | boost %4.2f m/s "
                "| stamina %s | dist %6.1fm | dev %d   ",
                body.speed,
                in,
                boost.hipWeight,
                body.hipFromWaist ? "(waist)" : "(hmd)",
                appliedSpeed,
                stamina,
                statAccumBoost,
                body.devicesUsed );
            std::fflush( stdout );
        }

        // --- frame pacing: sync to the compositor; fall back to a timed
        // sleep if the sync returns early (e.g. HMD standby) ---
        vr::EVROverlayError syncErr = vr::VROverlayError_None;
        if ( vr::VROverlay() )
            syncErr = vr::VROverlay()->WaitFrameSync( 100 );
        double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - frameStart )
                             .count();
        double remain = targetDt - elapsed;
        if ( ( syncErr != vr::VROverlayError_None
               || elapsed < targetDt * 0.25 )
             && remain > 0.0 )
            std::this_thread::sleep_for(
                std::chrono::duration<double>( remain ) );
    }

    std::printf( "\nShutting down.\n" );
    // Exit clean: undo our accumulated offset so the next session starts
    // where this one began (unless the user wants offsets to persist).
    // resetOffset only previews; finish() forces the pending commit before we
    // exit (no update loop left to settle it).
    if ( cfg.resetOnStart )
    {
        mover.resetOffset();
        mover.finish( cfg.adjustBounds );
    }
    else
    {
        mover.finish( cfg.adjustBounds );
    }
    board.shutdown();
    ui.shutdown();
    vr::VR_Shutdown();
    timeEndPeriod( 1 );
    return 0;
}
