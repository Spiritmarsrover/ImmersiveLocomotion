#pragma once
// SteamVR dashboard overlay UI: Dear ImGui rendered into a shared D3D11
// texture, driven by the overlay laser-pointer mouse events. Lets every
// config value be edited in VR; auto-saves the ini (debounced).

#include "config.h"

#include <openvr.h>

#include <d3d11.h>
#include <string>
#include <vector>

struct UiStatus
{
    double realSpeed = 0.0;
    double rawSpeed = 0.0; // weighted average before smoothing
    double input = 0.0;
    double hipWeight = 1.0;
    bool hipFromWaist = false;
    double boostSpeed = 0.0;
    double stamina = -1.0; // < 0 = infinite
    bool exhausted = false;
    double boostDistance = 0.0;
    int devices = 0;
    // board mode
    const char* boardPhase = "stowed";
    double leanAlong = 0.0;
    double leanLateral = 0.0;
    bool leanValid = false;
    bool hipOffsetSet = false;
    double boardSpeed = 0.0; // signed sim speed
    bool boardPivoting = false;
    double offsetX = 0.0; // playspace offset we have applied, raw meters
    double offsetY = 0.0;
    double offsetZ = 0.0;
};

class OverlayUI
{
public:
    // baseDir: folder containing manifest/ (for the dashboard icon).
    bool init( const std::string& baseDir );
    void shutdown();

    // Call every frame. Handles overlay events, renders when the dashboard
    // is visible, and auto-saves cfg when edited in the UI.
    void update( Config& cfg, const UiStatus& status, double dt );

    // True once after the user clicked "Calibrate hip direction".
    bool consumeCalibrate();
    // True once after the user clicked "Calibrate hip offset".
    bool consumeHipOffsetCalibrate();
    // True once after the user clicked "Reset playspace offset".
    bool consumeResetHome();
    // True once after the user clicked "Exit" (quit the overlay).
    bool consumeQuit();
    // True once after the user clicked "Open binding menu".
    bool consumeOpenBindings();

    // OSC debug override: when on, the overlay sends these values instead of
    // the live board state (for testing the avatar without riding).
    bool oscDebug() const { return m_oscDebug; }
    bool oscDebugActive() const { return m_oscDebugActive; }
    double oscDebugSpeed() const { return m_oscDebugSpeed; }

private:
    void pumpEvents();
    void buildUi( Config& cfg, const UiStatus& status );
    void drawGraph();
    void render();

    struct Sample
    {
        float t, raw, smooth, boost;
    };
    std::vector<Sample> m_samples;
    double m_time = 0.0;

    vr::VROverlayHandle_t m_overlay = vr::k_ulOverlayHandleInvalid;
    vr::VROverlayHandle_t m_thumbnail = vr::k_ulOverlayHandleInvalid;

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ID3D11Texture2D* m_texture = nullptr;
    ID3D11RenderTargetView* m_rtv = nullptr;

    bool m_imguiReady = false;
    bool m_calibrate = false;
    bool m_hipOffsetCalibrate = false;
    bool m_resetHome = false;
    bool m_quit = false;
    bool m_openBindings = false;
    bool m_oscDebug = false;
    bool m_oscDebugActive = false;
    float m_oscDebugSpeed = 0.0f;
    double m_dirtyTimer = -1.0; // >= 0 while a save is pending
    static constexpr int kWidth = 1500;
    static constexpr int kHeight = 900;
};
