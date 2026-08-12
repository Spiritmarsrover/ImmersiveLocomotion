#pragma once
// SteamVR input (IVRInput action system). Overlay apps receive mirrored
// action data alongside the focused scene app — same mechanism OVRAS uses
// for its motion features.

#include "config.h"

#include <openvr.h>
#include <string>

class InputManager
{
public:
    // actionManifestPath must be absolute.
    bool init( const std::string& actionManifestPath );

    // Call once per frame before reads.
    void update();

    // Boost input in [0,1] according to cfg.source.
    double boostInput( const Config& cfg );

    // Grip state per hand (board grab).
    bool grabLeft();
    bool grabRight();

private:
    vr::VRActionSetHandle_t m_set = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t m_stick = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t m_trigger = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t m_touch = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t m_grabLeft = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t m_grabRight = vr::k_ulInvalidActionHandle;

    bool digital( vr::VRActionHandle_t h );
};
