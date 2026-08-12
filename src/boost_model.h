#pragma once
// The locomotion model: boost = gain * clamp01(input) * real velocity,
// weighted by hip-forward direction, shaped by an asymmetric attack/release
// envelope (momentum), gated by a stamina tank measured in meters of
// artificial distance.

#include "config.h"
#include "velocity.h"
#include "vr_math.h"

struct BoostResult
{
    Vec3 velocity;          // artificial velocity to apply this frame, raw space
    double speed = 0.0;     // |velocity|
    double hipWeight = 1.0; // directional falloff actually applied
    double stamina = 0.0;   // current tank (meters); < 0 when infinite
    bool exhausted = false; // true while locked out after running empty
};

class BoostModel
{
public:
    BoostResult update( const BodyState& body, double input, double dt,
                        const Config& cfg );

private:
    Vec3 m_boost;             // current boost vector (the momentum state)
    double m_stamina = -1.0;  // meters; -1 = uninitialized
    double m_staminaMaxSeen = 0.0;
    bool m_exhausted = false;
};
