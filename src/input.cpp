#include "input.h"

#include "vr_math.h"

#include <cstdio>

bool InputManager::init( const std::string& actionManifestPath )
{
    vr::EVRInputError err
        = vr::VRInput()->SetActionManifestPath( actionManifestPath.c_str() );
    if ( err != vr::VRInputError_None )
    {
        std::fprintf( stderr,
                      "SetActionManifestPath failed: %d (%s)\n",
                      err,
                      actionManifestPath.c_str() );
        return false;
    }
    vr::VRInput()->GetActionSetHandle( "/actions/main", &m_set );
    vr::VRInput()->GetActionHandle( "/actions/main/in/boost_stick", &m_stick );
    vr::VRInput()->GetActionHandle( "/actions/main/in/boost_trigger",
                                    &m_trigger );
    vr::VRInput()->GetActionHandle( "/actions/main/in/boost_touch",
                                    &m_touch );
    vr::VRInput()->GetActionHandle( "/actions/main/in/grab_left",
                                    &m_grabLeft );
    vr::VRInput()->GetActionHandle( "/actions/main/in/grab_right",
                                    &m_grabRight );
    return true;
}

void InputManager::update()
{
    vr::VRActiveActionSet_t active = {};
    active.ulActionSet = m_set;
    vr::VRInput()->UpdateActionState(
        &active, sizeof( vr::VRActiveActionSet_t ), 1 );
}

double InputManager::boostInput( const Config& cfg )
{
    double stick = 0.0, trigger = 0.0;

    vr::InputAnalogActionData_t data = {};
    if ( vr::VRInput()->GetAnalogActionData( m_stick,
                                             &data,
                                             sizeof( data ),
                                             vr::k_ulInvalidInputValueHandle )
             == vr::VRInputError_None
         && data.bActive )
    {
        if ( cfg.stickMode == "fullrange" )
        {
            // Trackpad style: bottom = 0, top = 1 — but an untouched pad
            // reports (0,0) which would read as half boost, so gate on the
            // touch action (fall back to "any deflection" if unbound).
            bool touched = false;
            vr::InputDigitalActionData_t dd = {};
            if ( vr::VRInput()->GetDigitalActionData(
                     m_touch, &dd, sizeof( dd ),
                     vr::k_ulInvalidInputValueHandle )
                     == vr::VRInputError_None
                 && dd.bActive )
                touched = dd.bState;
            else
                touched = data.x != 0.0f || data.y != 0.0f;

            if ( touched )
                stick = clamp( ( static_cast<double>( data.y ) + 1.0 ) / 2.0,
                               0.0, 1.0 );
        }
        else
        {
            stick = clamp( static_cast<double>( data.y ), 0.0, 1.0 );
        }

        // Rescale so stick_full_at (e.g. 0.8) already counts as full boost —
        // no need to reach the very top edge of the pad.
        double fullAt = clamp( cfg.stickFullAt, 0.1, 1.0 );
        stick = clamp( stick / fullAt, 0.0, 1.0 );
    }
    if ( vr::VRInput()->GetAnalogActionData( m_trigger,
                                             &data,
                                             sizeof( data ),
                                             vr::k_ulInvalidInputValueHandle )
             == vr::VRInputError_None
         && data.bActive )
    {
        trigger = clamp( static_cast<double>( data.x ), 0.0, 1.0 );
    }

    if ( cfg.source == "trigger" )
        return trigger;
    if ( cfg.source == "both" )
        return stick > trigger ? stick : trigger;
    return stick;
}

bool InputManager::digital( vr::VRActionHandle_t h )
{
    vr::InputDigitalActionData_t dd = {};
    return vr::VRInput()->GetDigitalActionData(
               h, &dd, sizeof( dd ), vr::k_ulInvalidInputValueHandle )
               == vr::VRInputError_None
           && dd.bActive && dd.bState;
}

bool InputManager::grabLeft()
{
    return digital( m_grabLeft );
}

bool InputManager::grabRight()
{
    return digital( m_grabRight );
}
