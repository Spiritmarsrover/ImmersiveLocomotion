#include "boost_model.h"

#include <cmath>

BoostResult BoostModel::update( const BodyState& body, double input, double dt,
                                const Config& cfg )
{
    BoostResult out;

    bool infinite = cfg.staminaMax <= 0.0;
    if ( m_stamina < 0.0 || cfg.staminaMax != m_staminaMaxSeen )
    {
        // (re)initialize tank on start or when the config value changes
        m_stamina = infinite ? 0.0 : cfg.staminaMax;
        m_staminaMaxSeen = cfg.staminaMax;
        m_exhausted = false;
    }

    // --- target boost velocity ---
    input = clamp( input, 0.0, 1.0 );
    Vec3 target;
    double hipWeight = 1.0;
    double realSpeed = body.speed;
    if ( realSpeed >= cfg.deadband && input > 0.0 && !m_exhausted )
    {
        Vec3 dir = body.velocity.normalizedOrZero();
        if ( cfg.hipFalloffExponent > 0.0 && body.hipForward.length() > 0.5 )
        {
            double d = Vec3::dot( dir, body.hipForward );
            hipWeight = d > 0.0 ? std::pow( d, cfg.hipFalloffExponent ) : 0.0;
        }
        target = body.velocity * ( cfg.gain * input * hipWeight );
        double mag = target.length();
        if ( mag > cfg.maxBoostSpeed && mag > 1e-9 )
            target = target * ( cfg.maxBoostSpeed / mag );
    }
    out.hipWeight = hipWeight;

    // --- asymmetric momentum envelope on the boost vector itself ---
    // Rising toward a larger boost uses attack_time (spin-up); falling uses
    // release_time (glide) — glide persists even after real velocity hits 0.
    double tau = target.length() >= m_boost.length() ? cfg.attackTime
                                                     : cfg.releaseTime;
    double alpha = tau > 1e-4 ? 1.0 - std::exp( -dt / tau ) : 1.0;
    m_boost += ( target - m_boost ) * alpha;
    if ( m_boost.length() < 1e-3 && target.length() == 0.0 )
        m_boost = {};

    // --- stamina (meters of artificial distance) ---
    double boostDist = m_boost.length() * dt;
    if ( !infinite )
    {
        if ( boostDist > 1e-6 )
        {
            m_stamina -= boostDist * cfg.drainPerMeter;
        }
        else
        {
            m_stamina += realSpeed * dt * cfg.regenPerMeter;
        }
        m_stamina += cfg.regenPerSecond * dt;
        m_stamina = clamp( m_stamina, 0.0, cfg.staminaMax );

        if ( !m_exhausted && m_stamina <= 0.0 )
        {
            m_exhausted = true;
        }
        if ( m_exhausted
             && m_stamina
                    >= ( cfg.minToEngage > 0.0
                             ? std::fmin( cfg.minToEngage, cfg.staminaMax )
                             : cfg.staminaMax ) )
        {
            m_exhausted = false;
        }
    }

    out.velocity = m_boost;
    out.speed = m_boost.length();
    out.stamina = infinite ? -1.0 : m_stamina;
    out.exhausted = m_exhausted;
    return out;
}
