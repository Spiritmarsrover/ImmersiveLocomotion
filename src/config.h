#pragma once
// Hot-reloadable INI configuration. Every tunable of the locomotion model
// lives here so feel can be iterated live in VR by editing the file.
//
// Defaults below are the maintainer's dialed-in "good feel" values, so a
// fresh install rides well out of the box. The three body-calibration fields
// (hipYawOffsetDeg, hipOffsetX/Y/Z) are deliberately left neutral — they are
// per-user and set by the in-dashboard calibrate buttons, not shipped.

#include <string>

struct Config
{
    // [boost]
    double gain = 1.03;           // artificial = gain * clamp01(input) * v_real
    double maxBoostSpeed = 5.0;   // m/s clamp on artificial velocity magnitude
    double attackTime = 0.02;     // s time-constant, boost ramping up (spin-up)
    double releaseTime = 0.41;    // s time-constant, boost ramping down (glide)
    double deadband = 0.09;       // m/s of real speed below which target is 0
    double hipFalloffExponent = 0.0; // cos^n directional weight; 0 = omni
    double hipYawOffsetDeg = 0.0; // mount correction for the hip device (calib)
    std::string hipSource = "auto"; // auto | waist | hmd

    // [stamina]  (stamina unit = meters of artificial distance)
    double staminaMax = 0.0;      // <= 0 means infinite
    double drainPerMeter = 1.0;   // stamina per meter of boost distance
    double regenPerMeter = 0.5;   // stamina per meter walked while not boosting
    double regenPerSecond = 0.0;  // passive regen
    double minToEngage = 5.0;     // hysteresis after hitting empty

    // [velocity]
    std::string filter = "envelope"; // ewma | alpha | oneeuro | split | envelope
    double directionTau = 0.15;   // split/envelope: EWMA tau for the direction
    double magAttackTau = 0.3;    // envelope: speed rise time-constant
    double magReleaseTau = 1.8;   // envelope: speed decay (coasts over dips)
    double stationaryTimeout = 0.23; // envelope: still this long -> fast decay
    double smoothingTau = 0.54;   // s EWMA time-constant on the velocity vector
    double alpha = 0.707;         // alpha mode: fixed per-frame blend factor
    double euroMinCutoff = 0.244; // one euro: Hz floor (lower = smoother)
    double euroBeta = 2.0;        // one euro: speed coefficient
    double euroDCutoff = 1.0;     // one euro: derivative low-pass cutoff, Hz
    bool useDriverVelocity = true; // pose vVelocity vs finite differencing
    double weightHmd = 1.24;
    double weightHands = 0.62;
    double weightWaist = 3.03;
    double weightFeet = 1.09;
    double weightOther = 0.37;

    // [mover]
    bool adjustBounds = true;     // counter-move chaperone walls to stay physical
    double maxTotalSpeed = 0.0;   // safety clamp on applied m/s, 0 = off
    bool enabled = true;
    bool resetOnStart = true;     // restore playspace to home on launch/exit

    // [body]  (center-of-mass model shared by board mode)
    double hipOffsetX = 0.0;      // hip-tracker-LOCAL offset to body center,
    double hipOffsetY = 0.0;      // set by the controllers-at-hips calibration
    double hipOffsetZ = 0.0;
    double comWeightHip = 0.60;   // per device
    double comWeightHead = 0.20;
    double comWeightHands = 0.05;
    double comWeightFeet = 0.15;

    // [board]
    bool boardEnabled = true;     // board mode on/off (grab, mount, ride)
    std::string stance = "regular"; // regular = right foot rear | goofy
    double throwDistance = 0.4;   // m in front of the user on throw-out
    double boardLength = 0.73;    // m
    double boardWidth = 0.28;     // m
    double dismountRaise = 0.07;  // foot above mount baseline -> dismount
    double pivotSpeed = 1.5;      // m/s; below = pivot regime (used later)
    double liftHeight = 0.04;     // m universe lift while riding
    double restPitchDeg = 25.0;   // nose-up pitch of the resting board
    double grabRadius = 0.15;     // m hand-to-hip distance to grab the board
    // ride physics: v' = accel_gain*lean - drag*v*|v|
    double accelGain = 10.0;      // m/s^2 at full (normalized) lean
    double dragCoeff = 0.12;      // quadratic drag
    double leanDeadzone = 0.01;   // normalized lean ignored around neutral
    // per-axis lean sensitivity: physical CoM shift is much smaller fore/aft
    // than laterally (same half-stance normalization), so each axis gets its
    // own multiplier to reach full control authority
    double leanGainAlong = 1.26;
    double leanGainLateral = 2.66;
    // response curve: out = sign(in)*|in|^gamma (>1 = finer near center,
    // punchier at the extremes; 1 = linear)
    double leanGamma = 1.36; // throttle (along)
    double turnGamma = 1.26; // turn (lateral)
    double boardMaxSpeed = 13.81; // m/s hard cap
    double turnRate = 180.0;      // deg/s at full lateral lean (negative flips)
    double turnDeadzone = 0.01;   // normalized lateral lean ignored
    double pivotFootSpeed = 0.1;  // m/s front-foot speed that opens the gate
    std::string rotationCenter = "feet"; // feet | com | hmd

    // [osc]  (drive a VRChat avatar over OSC)
    bool oscEnabled = false;      // off by default; enable for VRChat avatar
    std::string oscIp = "127.0.0.1";
    double oscPort = 9000;

    // [input]
    std::string source = "stick";       // stick | trigger | both
    std::string stickMode = "fullrange"; // fullrange: y -1..1 -> 0..1 (trackpad,
                                         // touch-gated); forward: clamp01(y)
    double stickFullAt = 0.76;    // stick input value that maps to full boost

    // -- not in the file --
    std::string path;
    long long lastWriteTime = 0;

    // Loads from `path`; creates the file with defaults + comments if missing.
    // Returns true if values were (re)loaded.
    bool loadOrCreate();
    // Cheap mtime poll; reloads when the file changed. Returns true on reload.
    bool pollReload();
    // Writes current values back to `path` (regenerates the commented file).
    bool save();
};
