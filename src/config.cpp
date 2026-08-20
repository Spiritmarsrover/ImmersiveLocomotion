#include "config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>

namespace
{
std::string trim( const std::string& s )
{
    size_t a = s.find_first_not_of( " \t\r\n" );
    if ( a == std::string::npos )
        return "";
    size_t b = s.find_last_not_of( " \t\r\n" );
    return s.substr( a, b - a + 1 );
}

std::string lower( std::string s )
{
    std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) {
        return static_cast<char>( std::tolower( c ) );
    } );
    return s;
}

long long mtimeOf( const std::string& path )
{
    struct _stat64 st;
    if ( _stat64( path.c_str(), &st ) != 0 )
        return 0;
    return static_cast<long long>( st.st_mtime );
}

const char* kDefaultIni = R"INI(; ImmersiveLocomotion configuration.
; Hot-reloaded: save this file while the overlay runs and values apply live.
; Units: meters, seconds, m/s. Stamina unit = meters of artificial distance.

[boost]
; artificial velocity = gain * clamp01(input) * real velocity  (so total = (1+gain)x)
gain = 1.0
; hard clamp on artificial speed, m/s
max_boost_speed = 5.0
; momentum: first-order lag time-constants for the boost vector.
; attack = spin-up while engaging, release = glide when input/real speed drops.
attack_time = 1.0
release_time = 0.35
; real speed below this contributes no boost target (idle sway rejection)
deadband = 0.12
; directional weight = max(0, dot(velocity_dir, hip_forward))^exponent
; 0 disables direction weighting entirely (omnidirectional)
hip_falloff_exponent = 2.0
; yaw correction in degrees if the waist tracker is not mounted facing forward
hip_yaw_offset_deg = 0
; auto = waist tracker if present else HMD; or force: waist | hmd
hip_source = auto

[stamina]
; maximum stamina in meters of boosted distance; 0 or less = infinite
max = 0
; stamina spent per meter of artificial distance
drain_per_meter = 1.0
; stamina recovered per meter physically walked while not boosting
regen_per_meter = 0.5
; passive recovery per second
regen_per_second = 0
; after running empty, this much must recover before boosting re-engages
min_to_engage = 5.0

[velocity]
; smoothing filter for the weighted body velocity:
;   ewma    = exponential moving average with a TIME constant (frame-rate
;             independent), tuned by smoothing_tau
;   alpha   = classic fixed per-frame blend, tuned by alpha (note: response
;             depends on frame rate)
;   oneeuro = One Euro filter: heavy smoothing while the signal changes
;             slowly (flat boost through redirects), fast tracking on big
;             changes; tuned by oneeuro_* below
;   split   = magnitude and direction smoothed SEPARATELY: speed goes
;             through the One Euro filter (stays flat through redirects by
;             construction), direction is a fast EWMA (direction_tau) and
;             holds when nearly stationary
;   envelope = MRDW mode: speed is an asymmetric envelope follower — rises
;             with mag_attack_tau, decays with mag_release_tau so corner
;             dips in a redirect lap are coasted over entirely. Relies on
;             releasing the boost input to stop (that is the intent
;             signal); standing still for stationary_timeout secs forces a
;             fast decay as a safety net. Direction = fast EWMA.
filter = ewma
; split/envelope: direction EWMA time-constant (smaller = faster turns)
direction_tau = 0.15
; envelope: speed rise time-constant
mag_attack_tau = 0.3
; envelope: speed decay time-constant — set well above the redirect cycle
mag_release_tau = 5.0
; envelope: raw speed below deadband for this long forces fast decay
stationary_timeout = 1.0
; EWMA time-constant
smoothing_tau = 0.30
; alpha mode blend factor per frame (0..1)
alpha = 0.05
; one euro: cutoff floor in Hz — LOWER = smoother/flatter but laggier
oneeuro_min_cutoff = 0.5
; one euro: how strongly the cutoff opens up with rate of change —
; HIGHER = quicker response to real speed changes
oneeuro_beta = 0.3
; one euro: derivative low-pass cutoff in Hz (rarely needs changing)
oneeuro_d_cutoff = 1.0
; true: use driver-reported velocities; false: finite-difference positions
use_driver_velocity = true
; 6-point weighted average weights (devices missing are simply excluded)
weight_hmd = 1.0
weight_hands = 0.25
weight_waist = 3.0
weight_feet = 1.5
weight_other = 1.0

[mover]
; master enable for applying motion (estimation/HUD keeps running when false)
enabled = true
; counter-translate chaperone collision bounds so the drawn walls stay aligned
; with the physical room while the universe moves
adjust_bounds = true
; safety clamp on total applied artificial speed, m/s; 0 = off
max_total_speed = 0
; restore the playspace to its home (un-offset) position on launch and exit,
; so a session never starts displaced by leftover offset
reset_on_start = true

[body]
; hip-tracker-local offset to the body center, set from the UI calibration
; (hold both controllers pointing inward at your hips, click the button)
hip_offset_x = 0
hip_offset_y = 0
hip_offset_z = 0
; center-of-mass weights (per device; missing devices are excluded)
com_weight_hip = 0.60
com_weight_head = 0.20
com_weight_hands = 0.05
com_weight_feet = 0.15

[board]
; board mode on/off (grab, mount, ride)
board_enabled = true
; regular = right foot rear, goofy = left foot rear
stance = regular
; where the board lands in front of you on throw-out
throw_distance = 0.9
board_length = 0.8
board_width = 0.28
; raising a foot this far above its mount height dismounts
dismount_raise = 0.15
; below this sim speed the pivot regime applies (used by later stages)
pivot_speed = 1.5
; universe lift applied while riding (activation cue / deck height)
lift_height = 0.04
; nose-up pitch of the board resting on its tail
rest_pitch_deg = 25
; hand within this distance of the hip grabs the stowed board
grab_radius = 0.35
; ride physics: v' = accel_gain * lean - drag * v * |v|
accel_gain = 3.0
drag = 0.12
lean_deadzone = 0.08
; per-axis lean sensitivity multipliers (fore/aft shift reads smaller than
; lateral, so along usually needs a higher gain to reach full authority)
lean_gain_along = 3.0
lean_gain_lateral = 2.0
; response curve exponent (gamma): out = sign(in)*|in|^gamma
; >1 = finer control near center, punchier at the extremes; 1 = linear
lean_gamma = 1.0
turn_gamma = 1.0
board_max_speed = 6.0
; deg/s of artificial yaw at full lateral lean (negative to flip direction)
turn_rate = 70
turn_deadzone = 0.12
; front-foot speed that opens the low-speed pivot gate
pivot_foot_speed = 0.6
; artificial yaw rotates about: feet | com | hmd
rotation_center = feet

[osc]
; drive a VRChat avatar over OSC (sends /avatar/parameters/IL_BoardActive
; [bool] and /avatar/parameters/IL_BoardSpeed [float -1..1])
enabled = false
ip = 127.0.0.1
port = 9000

[input]
; stick = thumbstick/trackpad axis, trigger = analog trigger,
; both = max of the two
source = stick
; fullrange: vertical axis -1..1 maps to 0..1 (trackpad style, gated on
; touch so an untouched pad reads 0). forward: only 0..1 (thumbstick style)
stick_mode = fullrange
; stick value that already counts as full boost (rescales 0..this to 0..1
; so you don't have to reach the very top of the pad)
stick_full_at = 0.8
)INI";
} // namespace

bool Config::loadOrCreate()
{
    {
        std::ifstream test( path );
        if ( !test.good() )
        {
            std::ofstream out( path );
            out << kDefaultIni;
        }
    }

    std::ifstream in( path );
    if ( !in.good() )
        return false;

    std::map<std::string, std::string> kv;
    std::string line, section;
    while ( std::getline( in, line ) )
    {
        line = trim( line );
        if ( line.empty() || line[0] == ';' || line[0] == '#' )
            continue;
        if ( line.front() == '[' && line.back() == ']' )
        {
            section = lower( trim( line.substr( 1, line.size() - 2 ) ) );
            continue;
        }
        size_t eq = line.find( '=' );
        if ( eq == std::string::npos )
            continue;
        std::string key = lower( trim( line.substr( 0, eq ) ) );
        std::string val = trim( line.substr( eq + 1 ) );
        size_t comment = val.find( ';' );
        if ( comment != std::string::npos )
            val = trim( val.substr( 0, comment ) );
        kv[section + "." + key] = val;
    }

    auto num = [&]( const char* k, double& out ) {
        auto it = kv.find( k );
        if ( it != kv.end() )
        {
            try
            {
                out = std::stod( it->second );
            }
            catch ( ... )
            {
            }
        }
    };
    auto boolean = [&]( const char* k, bool& out ) {
        auto it = kv.find( k );
        if ( it != kv.end() )
        {
            std::string v = lower( it->second );
            out = ( v == "1" || v == "true" || v == "yes" || v == "on" );
        }
    };
    auto str = [&]( const char* k, std::string& out ) {
        auto it = kv.find( k );
        if ( it != kv.end() )
            out = lower( it->second );
    };
    // case-preserving variant: OSC parameter names are case-sensitive
    auto strRaw = [&]( const char* k, std::string& out ) {
        auto it = kv.find( k );
        if ( it != kv.end() )
            out = it->second;
    };

    num( "boost.gain", gain );
    num( "boost.max_boost_speed", maxBoostSpeed );
    num( "boost.attack_time", attackTime );
    num( "boost.release_time", releaseTime );
    num( "boost.deadband", deadband );
    num( "boost.hip_falloff_exponent", hipFalloffExponent );
    num( "boost.hip_yaw_offset_deg", hipYawOffsetDeg );
    str( "boost.hip_source", hipSource );

    num( "stamina.max", staminaMax );
    num( "stamina.drain_per_meter", drainPerMeter );
    num( "stamina.regen_per_meter", regenPerMeter );
    num( "stamina.regen_per_second", regenPerSecond );
    num( "stamina.min_to_engage", minToEngage );

    str( "velocity.filter", filter );
    num( "velocity.direction_tau", directionTau );
    num( "velocity.mag_attack_tau", magAttackTau );
    num( "velocity.mag_release_tau", magReleaseTau );
    num( "velocity.stationary_timeout", stationaryTimeout );
    num( "velocity.smoothing_tau", smoothingTau );
    num( "velocity.alpha", alpha );
    num( "velocity.oneeuro_min_cutoff", euroMinCutoff );
    num( "velocity.oneeuro_beta", euroBeta );
    num( "velocity.oneeuro_d_cutoff", euroDCutoff );
    boolean( "velocity.use_driver_velocity", useDriverVelocity );
    num( "velocity.weight_hmd", weightHmd );
    num( "velocity.weight_hands", weightHands );
    num( "velocity.weight_waist", weightWaist );
    num( "velocity.weight_feet", weightFeet );
    num( "velocity.weight_other", weightOther );

    boolean( "mover.enabled", enabled );
    boolean( "mover.adjust_bounds", adjustBounds );
    num( "mover.max_total_speed", maxTotalSpeed );
    boolean( "mover.reset_on_start", resetOnStart );

    num( "body.hip_offset_x", hipOffsetX );
    num( "body.hip_offset_y", hipOffsetY );
    num( "body.hip_offset_z", hipOffsetZ );
    num( "body.com_weight_hip", comWeightHip );
    num( "body.com_weight_head", comWeightHead );
    num( "body.com_weight_hands", comWeightHands );
    num( "body.com_weight_feet", comWeightFeet );

    boolean( "board.board_enabled", boardEnabled );
    str( "board.stance", stance );
    num( "board.throw_distance", throwDistance );
    num( "board.board_length", boardLength );
    num( "board.board_width", boardWidth );
    num( "board.dismount_raise", dismountRaise );
    num( "board.pivot_speed", pivotSpeed );
    num( "board.lift_height", liftHeight );
    num( "board.rest_pitch_deg", restPitchDeg );
    num( "board.grab_radius", grabRadius );
    num( "board.accel_gain", accelGain );
    num( "board.drag", dragCoeff );
    num( "board.lean_deadzone", leanDeadzone );
    num( "board.lean_gain_along", leanGainAlong );
    num( "board.lean_gain_lateral", leanGainLateral );
    num( "board.lean_gamma", leanGamma );
    num( "board.turn_gamma", turnGamma );
    num( "board.board_max_speed", boardMaxSpeed );
    num( "board.turn_rate", turnRate );
    boolean( "board.lean_turn", boardLeanTurn );
    num( "board.turn_deadzone", turnDeadzone );
    num( "board.pivot_foot_speed", pivotFootSpeed );
    num( "board.axis_tau", axisTau );
    num( "board.lean_neutral_along", leanNeutralAlong );
    num( "board.lean_neutral_lateral", leanNeutralLateral );
    str( "board.rotation_center", rotationCenter );

    boolean( "osc.enabled", oscEnabled );
    str( "osc.ip", oscIp );
    num( "osc.port", oscPort );

    str( "input.source", source );
    str( "input.stick_mode", stickMode );
    num( "input.stick_full_at", stickFullAt );

    boolean( "osc_in.enabled", beaconEnabled );
    num( "osc_in.port", beaconPort );

    boolean( "collision.detect", collisionDetect );
    strRaw( "collision.ray_param", rayParam );
    num( "collision.ray_count", rayCount );
    num( "collision.ray_threshold", rayThreshold );
    num( "collision.hold", collisionHold );
    num( "collision.startup_ignore", collisionStartupIgnore );

    strRaw( "space.backend", spaceBackend );
    strRaw( "space.ovr_ip", ovrIp );
    num( "space.ovr_port", ovrPort );

    lastWriteTime = mtimeOf( path );
    return true;
}

bool Config::pollReload()
{
    long long t = mtimeOf( path );
    if ( t != 0 && t != lastWriteTime )
        return loadOrCreate();
    return false;
}

bool Config::save()
{
    std::ofstream out( path );
    if ( !out.good() )
        return false;
    auto b = []( bool v ) { return v ? "true" : "false"; };
    out << "; ImmersiveLocomotion configuration.\n"
           "; Hot-reloaded: save this file while the overlay runs and values "
           "apply live.\n"
           "; Also editable in VR from the SteamVR dashboard overlay.\n"
           "; Units: meters, seconds, m/s. Stamina unit = meters of "
           "artificial distance.\n\n";
    out << "[boost]\n";
    out << "gain = " << gain << "\n";
    out << "max_boost_speed = " << maxBoostSpeed << "\n";
    out << "attack_time = " << attackTime << "\n";
    out << "release_time = " << releaseTime << "\n";
    out << "deadband = " << deadband << "\n";
    out << "hip_falloff_exponent = " << hipFalloffExponent << "\n";
    out << "hip_yaw_offset_deg = " << hipYawOffsetDeg << "\n";
    out << "hip_source = " << hipSource << "\n\n";
    out << "[stamina]\n";
    out << "max = " << staminaMax << "\n";
    out << "drain_per_meter = " << drainPerMeter << "\n";
    out << "regen_per_meter = " << regenPerMeter << "\n";
    out << "regen_per_second = " << regenPerSecond << "\n";
    out << "min_to_engage = " << minToEngage << "\n\n";
    out << "[velocity]\n";
    out << "filter = " << filter << "\n";
    out << "direction_tau = " << directionTau << "\n";
    out << "mag_attack_tau = " << magAttackTau << "\n";
    out << "mag_release_tau = " << magReleaseTau << "\n";
    out << "stationary_timeout = " << stationaryTimeout << "\n";
    out << "smoothing_tau = " << smoothingTau << "\n";
    out << "alpha = " << alpha << "\n";
    out << "oneeuro_min_cutoff = " << euroMinCutoff << "\n";
    out << "oneeuro_beta = " << euroBeta << "\n";
    out << "oneeuro_d_cutoff = " << euroDCutoff << "\n";
    out << "use_driver_velocity = " << b( useDriverVelocity ) << "\n";
    out << "weight_hmd = " << weightHmd << "\n";
    out << "weight_hands = " << weightHands << "\n";
    out << "weight_waist = " << weightWaist << "\n";
    out << "weight_feet = " << weightFeet << "\n";
    out << "weight_other = " << weightOther << "\n\n";
    out << "[mover]\n";
    out << "enabled = " << b( enabled ) << "\n";
    out << "adjust_bounds = " << b( adjustBounds ) << "\n";
    out << "max_total_speed = " << maxTotalSpeed << "\n";
    out << "reset_on_start = " << b( resetOnStart ) << "\n\n";
    out << "[body]\n";
    out << "hip_offset_x = " << hipOffsetX << "\n";
    out << "hip_offset_y = " << hipOffsetY << "\n";
    out << "hip_offset_z = " << hipOffsetZ << "\n";
    out << "com_weight_hip = " << comWeightHip << "\n";
    out << "com_weight_head = " << comWeightHead << "\n";
    out << "com_weight_hands = " << comWeightHands << "\n";
    out << "com_weight_feet = " << comWeightFeet << "\n\n";
    out << "[board]\n";
    out << "board_enabled = " << b( boardEnabled ) << "\n";
    out << "stance = " << stance << "\n";
    out << "throw_distance = " << throwDistance << "\n";
    out << "board_length = " << boardLength << "\n";
    out << "board_width = " << boardWidth << "\n";
    out << "dismount_raise = " << dismountRaise << "\n";
    out << "pivot_speed = " << pivotSpeed << "\n";
    out << "lift_height = " << liftHeight << "\n";
    out << "rest_pitch_deg = " << restPitchDeg << "\n";
    out << "grab_radius = " << grabRadius << "\n";
    out << "accel_gain = " << accelGain << "\n";
    out << "drag = " << dragCoeff << "\n";
    out << "lean_deadzone = " << leanDeadzone << "\n";
    out << "lean_gain_along = " << leanGainAlong << "\n";
    out << "lean_gain_lateral = " << leanGainLateral << "\n";
    out << "lean_gamma = " << leanGamma << "\n";
    out << "turn_gamma = " << turnGamma << "\n";
    out << "board_max_speed = " << boardMaxSpeed << "\n";
    out << "turn_rate = " << turnRate << "\n";
    out << "lean_turn = " << b( boardLeanTurn ) << "\n";
    out << "turn_deadzone = " << turnDeadzone << "\n";
    out << "pivot_foot_speed = " << pivotFootSpeed << "\n";
    out << "axis_tau = " << axisTau << "\n";
    out << "lean_neutral_along = " << leanNeutralAlong << "\n";
    out << "lean_neutral_lateral = " << leanNeutralLateral << "\n";
    out << "rotation_center = " << rotationCenter << "\n\n";
    out << "[osc]\n";
    out << "enabled = " << b( oscEnabled ) << "\n";
    out << "ip = " << oscIp << "\n";
    out << "port = " << oscPort << "\n\n";
    out << "[input]\n";
    out << "source = " << source << "\n";
    out << "stick_mode = " << stickMode << "\n";
    out << "stick_full_at = " << stickFullAt << "\n\n";
    out << "[osc_in]\n";
    out << "enabled = " << b( beaconEnabled ) << "\n";
    out << "port = " << beaconPort << "\n\n";
    out << "[collision]\n";
    out << "detect = " << b( collisionDetect ) << "\n";
    out << "ray_param = " << rayParam << "\n";
    out << "ray_count = " << rayCount << "\n";
    out << "ray_threshold = " << rayThreshold << "\n";
    out << "hold = " << collisionHold << "\n";
    out << "startup_ignore = " << collisionStartupIgnore << "\n\n";
    out << "[space]\n";
    out << "backend = " << spaceBackend << "\n";
    out << "ovr_ip = " << ovrIp << "\n";
    out << "ovr_port = " << ovrPort << "\n";
    out.close();
    lastWriteTime = mtimeOf( path );
    return true;
}
