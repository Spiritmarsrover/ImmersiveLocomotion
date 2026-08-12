#include "overlay_ui.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"

#include <cfloat>
#include <cstdio>

namespace
{
const char* kOverlayKey = "spiritmarsrover.immersive_locomotion";

// double-backed ImGui slider helper
bool sliderD( const char* label, double& v, float lo, float hi,
              const char* fmt = "%.2f", ImGuiSliderFlags flags = 0 )
{
    float f = static_cast<float>( v );
    bool changed = ImGui::SliderFloat( label, &f, lo, hi, fmt, flags );
    if ( changed )
        v = static_cast<double>( f );
    return changed;
}

// One Euro parameter block, shared by the oneeuro and split filter pages.
bool euroSliders( Config& cfg )
{
    bool changed = false;
    changed |= sliderD( "min cutoff Hz (lower = flatter)", cfg.euroMinCutoff,
                        0.005f, 5.0f, "%.3f", ImGuiSliderFlags_Logarithmic );
    ImGui::TextDisabled( "   ~ %.1f s time constant at rest",
                         1.0 / ( 6.2832 * ( cfg.euroMinCutoff > 0.005
                                                ? cfg.euroMinCutoff
                                                : 0.005 ) ) );
    changed |= sliderD( "beta (higher = snappier)", cfg.euroBeta, 0.0f, 2.0f,
                        "%.3f", ImGuiSliderFlags_Logarithmic );
    changed |= sliderD( "derivative cutoff Hz", cfg.euroDCutoff, 0.05f, 5.0f,
                        "%.2f", ImGuiSliderFlags_Logarithmic );
    return changed;
}

bool comboStr( const char* label, std::string& value, const char* const* items,
               int count )
{
    int cur = 0;
    for ( int i = 0; i < count; ++i )
        if ( value == items[i] )
            cur = i;
    bool changed = ImGui::Combo( label, &cur, items, count );
    if ( changed )
        value = items[cur];
    return changed;
}
} // namespace

bool OverlayUI::init( const std::string& baseDir )
{
    if ( !vr::VROverlay() )
        return false;

    vr::EVROverlayError err = vr::VROverlay()->CreateDashboardOverlay(
        kOverlayKey, "Immersive Locomotion", &m_overlay, &m_thumbnail );
    if ( err != vr::VROverlayError_None )
    {
        std::fprintf( stderr, "CreateDashboardOverlay failed: %d\n", err );
        return false;
    }

    vr::VROverlay()->SetOverlayInputMethod( m_overlay,
                                            vr::VROverlayInputMethod_Mouse );
    vr::HmdVector2_t scale = { static_cast<float>( kWidth ),
                               static_cast<float>( kHeight ) };
    vr::VROverlay()->SetOverlayMouseScale( m_overlay, &scale );
    vr::VROverlay()->SetOverlayFlag(
        m_overlay, vr::VROverlayFlags_SendVRSmoothScrollEvents, true );
    vr::VROverlay()->SetOverlayWidthInMeters( m_overlay, 2.9f );

    std::string icon = baseDir + "\\manifest\\icon.png";
    vr::VROverlay()->SetOverlayFromFile( m_thumbnail, icon.c_str() );

    // D3D11 device + shared render target the compositor can open.
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDevice( nullptr,
                                    D3D_DRIVER_TYPE_HARDWARE,
                                    nullptr,
                                    0,
                                    nullptr,
                                    0,
                                    D3D11_SDK_VERSION,
                                    &m_device,
                                    &level,
                                    &m_context );
    if ( FAILED( hr ) )
    {
        std::fprintf( stderr, "D3D11CreateDevice failed: 0x%08lx\n, ", hr );
        return false;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = kWidth;
    desc.Height = kHeight;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = m_device->CreateTexture2D( &desc, nullptr, &m_texture );
    if ( FAILED( hr ) )
    {
        std::fprintf( stderr, "CreateTexture2D failed: 0x%08lx\n", hr );
        return false;
    }
    m_device->CreateRenderTargetView( m_texture, nullptr, &m_rtv );

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // no imgui.ini clutter
    ImFontConfig fontCfg;
    fontCfg.SizePixels = 26.0f; // VR-readable
    io.Fonts->AddFontDefault( &fontCfg );
    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes( 1.8f );
    ImGui_ImplDX11_Init( m_device, m_context );
    m_imguiReady = true;
    return true;
}

void OverlayUI::shutdown()
{
    if ( m_imguiReady )
    {
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext();
        m_imguiReady = false;
    }
    if ( m_rtv )
        m_rtv->Release();
    if ( m_texture )
        m_texture->Release();
    if ( m_context )
        m_context->Release();
    if ( m_device )
        m_device->Release();
    if ( m_overlay != vr::k_ulOverlayHandleInvalid && vr::VROverlay() )
        vr::VROverlay()->DestroyOverlay( m_overlay );
}

void OverlayUI::pumpEvents()
{
    ImGuiIO& io = ImGui::GetIO();
    vr::VREvent_t ev;
    while ( vr::VROverlay()->PollNextOverlayEvent( m_overlay, &ev,
                                                   sizeof( ev ) ) )
    {
        switch ( ev.eventType )
        {
        case vr::VREvent_MouseMove:
            // overlay mouse space is bottom-left origin; ImGui is top-left
            io.AddMousePosEvent( ev.data.mouse.x,
                                 kHeight - ev.data.mouse.y );
            break;
        case vr::VREvent_MouseButtonDown:
            io.AddMouseButtonEvent( 0, true );
            break;
        case vr::VREvent_MouseButtonUp:
            io.AddMouseButtonEvent( 0, false );
            break;
        case vr::VREvent_ScrollSmooth:
            io.AddMouseWheelEvent( ev.data.scroll.xdelta,
                                   ev.data.scroll.ydelta );
            break;
        case vr::VREvent_FocusLeave:
            io.AddMousePosEvent( -FLT_MAX, -FLT_MAX );
            break;
        default:
            break;
        }
    }
}

void OverlayUI::buildUi( Config& cfg, const UiStatus& s )
{
    ImGui::SetNextWindowPos( ImVec2( 0, 0 ) );
    ImGui::SetNextWindowSize( ImVec2( kWidth, kHeight ) );
    ImGui::Begin( "ImmersiveLocomotion", nullptr,
                  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                      | ImGuiWindowFlags_NoMove
                      | ImGuiWindowFlags_NoCollapse );

    bool changed = false;

    // --- live status + exit ---
    ImGui::Text( "Immersive Locomotion" );

    // Exit button, far right of the top row.
    ImGui::SameLine( ImGui::GetWindowWidth() - 150 );
    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0.55f, 0.15f, 0.15f, 1 ) );
    if ( ImGui::Button( "Exit", ImVec2( 120, 0 ) ) )
        m_quit = true;
    ImGui::PopStyleColor();

    ImGui::SameLine( ImGui::GetWindowWidth() - 470 );
    ImGui::Text( "real %.2f  boost %.2f m/s", s.realSpeed, s.boostSpeed );

    if ( s.stamina >= 0.0 && cfg.staminaMax > 0.0 )
    {
        float frac = static_cast<float>( s.stamina / cfg.staminaMax );
        char label[64];
        std::snprintf( label, sizeof( label ), "stamina %.0f / %.0f m%s",
                       s.stamina, cfg.staminaMax,
                       s.exhausted ? "  EXHAUSTED" : "" );
        ImGui::PushStyleColor( ImGuiCol_PlotHistogram,
                               s.exhausted
                                   ? ImVec4( 0.9f, 0.3f, 0.2f, 1.0f )
                                   : ImVec4( 0.2f, 0.7f, 0.9f, 1.0f ) );
        ImGui::ProgressBar( frac, ImVec2( -1, 0 ), label );
        ImGui::PopStyleColor();
    }
    ImGui::Text( "input %.2f | hip weight %.2f (%s) | devices %d | "
                 "boosted %.0f m",
                 s.input, s.hipWeight, s.hipFromWaist ? "waist" : "hmd",
                 s.devices, s.boostDistance );
    ImGui::Separator();

    bool inSettings = false;
    bool inTabBar = ImGui::BeginTabBar( "pages" );
    if ( inTabBar )
        inSettings = ImGui::BeginTabItem( "Settings" );

    // ---- Settings page (flat scope to keep the section blocks readable) ----
    if ( inSettings
         && ImGui::CollapsingHeader( "Boost" ) )
    {
        ImGui::PushStyleColor( ImGuiCol_Text,
                               cfg.enabled ? ImVec4( 0.3f, 1.0f, 0.4f, 1.0f )
                                           : ImVec4( 1.0f, 0.4f, 0.3f, 1.0f ) );
        changed |= ImGui::Checkbox( cfg.enabled ? "boost ENABLED"
                                                : "boost DISABLED",
                                    &cfg.enabled );
        ImGui::PopStyleColor();
        changed |= sliderD( "gain (x real velocity)", cfg.gain, 0.0f, 4.0f );
        changed |= sliderD( "max boost speed (m/s)", cfg.maxBoostSpeed, 0.5f,
                            15.0f );
        changed |= sliderD( "attack time (s)", cfg.attackTime, 0.0f, 5.0f );
        changed |= sliderD( "release time (s)", cfg.releaseTime, 0.0f, 5.0f );
        changed |= sliderD( "deadband (m/s)", cfg.deadband, 0.0f, 0.5f );
        changed |= sliderD( "hip falloff exponent (0=omni)",
                            cfg.hipFalloffExponent, 0.0f, 8.0f, "%.1f" );
        changed |= sliderD( "hip yaw offset (deg)", cfg.hipYawOffsetDeg,
                            -180.0f, 180.0f, "%.0f" );
        static const char* hipItems[] = { "auto", "waist", "hmd" };
        changed |= comboStr( "hip source", cfg.hipSource, hipItems, 3 );
        if ( ImGui::Button( "Calibrate hip direction" ) )
            m_calibrate = true;
        ImGui::SameLine();
        ImGui::TextDisabled( "face head & hips the same way, then click" );
    }

    if ( inSettings
         && ImGui::CollapsingHeader( "Stamina" ) )
    {
        changed |= sliderD( "max (m of boost, 0 = infinite)", cfg.staminaMax,
                            0.0f, 500.0f, "%.0f" );
        changed |= sliderD( "drain per boosted meter", cfg.drainPerMeter,
                            0.0f, 5.0f );
        changed |= sliderD( "regen per walked meter", cfg.regenPerMeter, 0.0f,
                            5.0f );
        changed |= sliderD( "regen per second", cfg.regenPerSecond, 0.0f,
                            10.0f );
        changed |= sliderD( "min to re-engage", cfg.minToEngage, 0.0f,
                            100.0f, "%.0f" );
    }

    if ( inSettings && ImGui::CollapsingHeader( "Velocity estimation" ) )
    {
        static const char* filterItems[] = { "ewma", "alpha", "oneeuro",
                                             "split", "envelope" };
        changed |= comboStr( "filter", cfg.filter, filterItems, 5 );
        if ( cfg.filter == "envelope" )
        {
            ImGui::TextDisabled(
                "speed coasts over redirect dips; release pad to stop" );
            changed |= sliderD( "rise tau (s)", cfg.magAttackTau, 0.05f,
                                2.0f, "%.2f", ImGuiSliderFlags_Logarithmic );
            changed |= sliderD( "decay tau (s) - above redirect cycle",
                                cfg.magReleaseTau, 0.5f, 15.0f, "%.1f",
                                ImGuiSliderFlags_Logarithmic );
            changed |= sliderD( "stationary timeout (s)",
                                cfg.stationaryTimeout, 0.0f, 3.0f );
            changed |= sliderD( "direction tau (s)", cfg.directionTau, 0.0f,
                                1.0f );
        }
        else if ( cfg.filter == "split" )
        {
            ImGui::TextDisabled(
                "speed: one euro (below) | direction: fast EWMA" );
            changed |= euroSliders( cfg );
            changed |= sliderD( "direction tau (s)", cfg.directionTau, 0.0f,
                                1.0f );
        }
        else if ( cfg.filter == "alpha" )
        {
            changed |= sliderD( "alpha (per frame)", cfg.alpha, 0.0f, 1.0f,
                                "%.3f", ImGuiSliderFlags_Logarithmic );
        }
        else if ( cfg.filter == "oneeuro" )
        {
            changed |= euroSliders( cfg );
        }
        else
        {
            changed |= sliderD( "smoothing tau (s)", cfg.smoothingTau, 0.0f,
                                5.0f, "%.2f", ImGuiSliderFlags_Logarithmic );
        }
        changed |= ImGui::Checkbox( "use driver-reported velocity",
                                    &cfg.useDriverVelocity );
        changed |= sliderD( "weight: HMD", cfg.weightHmd, 0.0f, 5.0f );
        changed |= sliderD( "weight: hands", cfg.weightHands, 0.0f, 5.0f );
        changed |= sliderD( "weight: waist/chest", cfg.weightWaist, 0.0f,
                            5.0f );
        changed |= sliderD( "weight: feet", cfg.weightFeet, 0.0f, 5.0f );
        changed |= sliderD( "weight: other trackers", cfg.weightOther, 0.0f,
                            5.0f );
    }

    if ( inSettings && ImGui::CollapsingHeader( "Board" ) )
    {
        ImGui::PushStyleColor( ImGuiCol_Text,
                               cfg.boardEnabled
                                   ? ImVec4( 0.3f, 1.0f, 0.4f, 1.0f )
                                   : ImVec4( 1.0f, 0.4f, 0.3f, 1.0f ) );
        changed |= ImGui::Checkbox( cfg.boardEnabled ? "board ENABLED"
                                                     : "board DISABLED",
                                    &cfg.boardEnabled );
        ImGui::PopStyleColor();
        ImGui::Text( "phase: %s%s | speed %+.2f m/s", s.boardPhase,
                     s.boardPivoting ? " (pivot)" : "", s.boardSpeed );
        if ( s.leanValid )
            ImGui::Text( "lean  along %+.2f   lateral %+.2f", s.leanAlong,
                         s.leanLateral );
        else
            ImGui::TextDisabled( "lean: needs CoM + both feet tracked" );

        if ( ImGui::Button( "Calibrate hip offset" ) )
            m_hipOffsetCalibrate = true;
        ImGui::SameLine();
        ImGui::TextDisabled( s.hipOffsetSet
                                 ? "set - stand upright facing fwd to redo"
                                 : "stand upright facing forward, then click" );

        static const char* stanceItems[] = { "regular", "goofy" };
        changed |= comboStr( "stance (regular = right foot rear)",
                             cfg.stance, stanceItems, 2 );
        changed |= sliderD( "throw distance (m)", cfg.throwDistance, 0.4f,
                            2.0f );
        changed |= sliderD( "board length (m)", cfg.boardLength, 0.5f, 1.2f );
        changed |= sliderD( "board width (m)", cfg.boardWidth, 0.15f, 0.45f );
        changed |= sliderD( "dismount foot raise (m)", cfg.dismountRaise,
                            0.05f, 0.40f );
        changed |= sliderD( "pivot speed (m/s)", cfg.pivotSpeed, 0.5f, 3.0f );
        changed |= sliderD( "ride lift (m)", cfg.liftHeight, 0.0f, 0.10f,
                            "%.3f" );
        changed |= sliderD( "rest pitch (deg)", cfg.restPitchDeg, 0.0f,
                            45.0f, "%.0f" );
        changed |= sliderD( "grab radius (m)", cfg.grabRadius, 0.15f, 0.6f );
        ImGui::SeparatorText( "ride physics" );
        changed |= sliderD( "accel gain (m/s^2 at full lean)", cfg.accelGain,
                            0.5f, 10.0f );
        changed |= sliderD( "drag (quadratic)", cfg.dragCoeff, 0.0f, 0.5f,
                            "%.3f" );
        changed |= sliderD( "lean deadzone", cfg.leanDeadzone, 0.0f, 0.3f );
        changed |= sliderD( "lean gain: along (accel)", cfg.leanGainAlong,
                            1.0f, 8.0f );
        changed |= sliderD( "lean gain: lateral (turn)", cfg.leanGainLateral,
                            1.0f, 8.0f );
        changed |= sliderD( "throttle gamma (>1 = finer near center)",
                            cfg.leanGamma, 0.3f, 4.0f );
        changed |= sliderD( "turn gamma", cfg.turnGamma, 0.3f, 4.0f );
        changed |= sliderD( "max speed (m/s)", cfg.boardMaxSpeed, 1.0f,
                            15.0f );
        changed |= sliderD( "turn rate (deg/s, +/- flips)", cfg.turnRate,
                            -180.0f, 180.0f, "%.0f" );
        changed |= sliderD( "turn deadzone", cfg.turnDeadzone, 0.0f, 0.4f );
        changed |= sliderD( "pivot foot speed (m/s)", cfg.pivotFootSpeed,
                            0.1f, 2.0f );
        static const char* rotItems[] = { "feet", "com", "hmd" };
        changed |= comboStr( "rotation center", cfg.rotationCenter, rotItems,
                             3 );
    }

    if ( inSettings && ImGui::CollapsingHeader( "Playspace offset" ) )
    {
        ImGui::Text( "applied offset (m):  x %+.2f   y %+.2f   z %+.2f",
                     s.offsetX, s.offsetY, s.offsetZ );
        if ( ImGui::Button( "Reset playspace offset" ) )
            m_resetHome = true;
        ImGui::TextDisabled( "undone automatically on launch/exit" );
        changed |= ImGui::Checkbox( "reset offset on launch/exit",
                                    &cfg.resetOnStart );
    }

    if ( inSettings && ImGui::CollapsingHeader( "Mover / Input" ) )
    {
        changed |= ImGui::Checkbox( "keep chaperone walls physical",
                                    &cfg.adjustBounds );
        changed |= sliderD( "max total speed (m/s, 0 = off)",
                            cfg.maxTotalSpeed, 0.0f, 20.0f );
        static const char* srcItems[] = { "stick", "trigger", "both" };
        changed |= comboStr( "boost input source", cfg.source, srcItems, 3 );
        static const char* modeItems[] = { "fullrange", "forward" };
        changed |= comboStr( "stick mode (fullrange = trackpad -1..1)",
                             cfg.stickMode, modeItems, 2 );
        changed |= sliderD( "stick full boost at", cfg.stickFullAt, 0.1f,
                            1.0f );
        if ( ImGui::Button( "Open binding menu" ) )
            m_openBindings = true;
        ImGui::SameLine();
        ImGui::TextDisabled( "rebind boost / grab controls in SteamVR" );
    }

    if ( inSettings )
    {
        ImGui::Separator();
        if ( ImGui::Button( "Save now" ) )
        {
            cfg.save();
            m_dirtyTimer = -1.0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled( m_dirtyTimer >= 0.0 ? "unsaved changes..."
                                                 : "saved  |  %s",
                             cfg.path.c_str() );
        ImGui::EndTabItem();
    }

    if ( inTabBar )
    {
        if ( ImGui::BeginTabItem( "Velocity graph" ) )
        {
            drawGraph();
            ImGui::EndTabItem();
        }
        if ( ImGui::BeginTabItem( "OSC" ) )
        {
            ImGui::PushStyleColor(
                ImGuiCol_Text, cfg.oscEnabled
                                   ? ImVec4( 0.3f, 1.0f, 0.4f, 1.0f )
                                   : ImVec4( 1.0f, 0.4f, 0.3f, 1.0f ) );
            changed |= ImGui::Checkbox( cfg.oscEnabled ? "OSC ENABLED"
                                                       : "OSC DISABLED",
                                        &cfg.oscEnabled );
            ImGui::PopStyleColor();
            ImGui::TextDisabled( "drives a VRChat avatar (board mode)" );
            ImGui::Spacing();

            ImGui::Text( "sending to  %s : %d", cfg.oscIp.c_str(),
                         static_cast<int>( cfg.oscPort ) );
            float port = static_cast<float>( cfg.oscPort );
            if ( ImGui::SliderFloat( "port", &port, 9000.0f, 9100.0f,
                                     "%.0f" ) )
            {
                cfg.oscPort = port;
                changed = true;
            }
            ImGui::TextDisabled( "IP is editable in the .ini (default "
                                 "127.0.0.1 = this PC)" );
            ImGui::Spacing();
            ImGui::SeparatorText( "avatar parameters sent" );
            ImGui::BulletText( "IL_BoardActive  (Bool) - board is active" );
            ImGui::BulletText(
                "IL_BoardSpeed   (Float, -1..1) - wheel speed" );
            ImGui::Spacing();
            double norm = cfg.boardMaxSpeed > 0.01
                              ? s.boardSpeed / cfg.boardMaxSpeed
                              : 0.0;
            norm = norm > 1.0 ? 1.0 : ( norm < -1.0 ? -1.0 : norm );
            ImGui::Text( "live:  board %s   speed(norm) %+.2f", s.boardPhase,
                         norm );

            ImGui::SeparatorText( "debug override" );
            ImGui::Checkbox( "override board state (test avatar w/o riding)",
                             &m_oscDebug );
            if ( m_oscDebug )
            {
                ImGui::Checkbox( "board active", &m_oscDebugActive );
                ImGui::SliderFloat( "speed (-1..1)", &m_oscDebugSpeed, -1.0f,
                                    1.0f, "%.2f" );
                if ( ImGui::Button( "stop (speed 0)" ) )
                    m_oscDebugSpeed = 0.0f;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    if ( changed )
        m_dirtyTimer = 0.0;
}

void OverlayUI::drawGraph()
{
    constexpr float kWindowSec = 10.0f;
    const ImU32 colRaw = IM_COL32( 150, 150, 160, 255 );
    const ImU32 colSmooth = IM_COL32( 86, 182, 255, 255 );
    const ImU32 colBoost = IM_COL32( 80, 220, 120, 255 );

    // legend
    ImGui::TextColored( ImColor( colRaw ), "raw weighted avg" );
    ImGui::SameLine();
    ImGui::TextColored( ImColor( colSmooth ), "  smoothed" );
    ImGui::SameLine();
    ImGui::TextColored( ImColor( colBoost ), "  boost (applied)" );
    ImGui::SameLine();
    ImGui::TextDisabled( "  last %.0f s", kWindowSec );

    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 size( avail.x, avail.y > 120.0f ? avail.y - 10.0f : 120.0f );
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled( p0, ImVec2( p0.x + size.x, p0.y + size.y ),
                       IM_COL32( 16, 18, 26, 255 ) );

    float tNow = static_cast<float>( m_time );
    float t0 = tNow - kWindowSec;

    // y scale: max value in window, min ceiling 1 m/s
    float ymax = 1.0f;
    size_t first = m_samples.size();
    for ( size_t i = 0; i < m_samples.size(); ++i )
    {
        if ( m_samples[i].t < t0 )
            continue;
        if ( first == m_samples.size() )
            first = i;
        float m = m_samples[i].raw;
        if ( m_samples[i].smooth > m )
            m = m_samples[i].smooth;
        if ( m_samples[i].boost > m )
            m = m_samples[i].boost;
        if ( m > ymax )
            ymax = m;
    }
    ymax *= 1.1f;

    // horizontal grid lines at a "nice" step
    float step = ymax < 3.0f ? 0.5f : ( ymax < 8.0f ? 1.0f : 2.0f );
    for ( float v = step; v < ymax; v += step )
    {
        float y = p0.y + size.y * ( 1.0f - v / ymax );
        dl->AddLine( ImVec2( p0.x, y ), ImVec2( p0.x + size.x, y ),
                     IM_COL32( 70, 74, 88, 120 ) );
        char label[16];
        std::snprintf( label, sizeof( label ), "%.1f", v );
        dl->AddText( ImVec2( p0.x + 6, y - 24 ), IM_COL32( 140, 144, 158, 160 ),
                     label );
    }

    if ( first < m_samples.size() )
    {
        auto mapPoint = [&]( float t, float v ) {
            return ImVec2( p0.x + ( t - t0 ) / kWindowSec * size.x,
                           p0.y + size.y * ( 1.0f - v / ymax ) );
        };
        static std::vector<ImVec2> pts; // scratch, reused across frames
        auto polyline = [&]( ImU32 col, float Sample::*field,
                             float thickness ) {
            pts.clear();
            for ( size_t i = first; i < m_samples.size(); ++i )
                pts.push_back(
                    mapPoint( m_samples[i].t, m_samples[i].*field ) );
            if ( pts.size() > 1 )
                dl->AddPolyline( pts.data(), static_cast<int>( pts.size() ),
                                 col, ImDrawFlags_None, thickness );
        };
        polyline( colRaw, &Sample::raw, 1.5f );
        polyline( colSmooth, &Sample::smooth, 3.0f );
        polyline( colBoost, &Sample::boost, 3.0f );
    }

    ImGui::Dummy( size );
}

void OverlayUI::render()
{
    ImGui::Render();
    const float clear[4] = { 0.06f, 0.06f, 0.08f, 0.98f };
    m_context->OMSetRenderTargets( 1, &m_rtv, nullptr );
    m_context->ClearRenderTargetView( m_rtv, clear );
    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>( kWidth );
    vp.Height = static_cast<float>( kHeight );
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports( 1, &vp );
    ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );
    m_context->Flush();

    vr::Texture_t tex = { m_texture, vr::TextureType_DirectX,
                          vr::ColorSpace_Gamma };
    vr::VROverlay()->SetOverlayTexture( m_overlay, &tex );
}

bool OverlayUI::consumeCalibrate()
{
    bool c = m_calibrate;
    m_calibrate = false;
    return c;
}

bool OverlayUI::consumeHipOffsetCalibrate()
{
    bool c = m_hipOffsetCalibrate;
    m_hipOffsetCalibrate = false;
    return c;
}

bool OverlayUI::consumeResetHome()
{
    bool c = m_resetHome;
    m_resetHome = false;
    return c;
}

bool OverlayUI::consumeQuit()
{
    bool c = m_quit;
    m_quit = false;
    return c;
}

bool OverlayUI::consumeOpenBindings()
{
    bool c = m_openBindings;
    m_openBindings = false;
    return c;
}

void OverlayUI::update( Config& cfg, const UiStatus& status, double dt )
{
    if ( !m_imguiReady )
        return;

    // Always sample so history exists the moment the dashboard is opened.
    m_time += dt;
    m_samples.push_back( { static_cast<float>( m_time ),
                           static_cast<float>( status.rawSpeed ),
                           static_cast<float>( status.realSpeed ),
                           static_cast<float>( status.boostSpeed ) } );
    if ( m_samples.size() > 8192 )
        m_samples.erase( m_samples.begin(), m_samples.begin() + 4096 );

    pumpEvents();

    // debounced auto-save after UI edits
    if ( m_dirtyTimer >= 0.0 )
    {
        m_dirtyTimer += dt;
        if ( m_dirtyTimer > 1.0 )
        {
            cfg.save();
            m_dirtyTimer = -1.0;
        }
    }

    if ( !vr::VROverlay()->IsOverlayVisible( m_overlay ) )
        return;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2( kWidth, kHeight );
    io.DeltaTime = static_cast<float>( dt > 1e-4 ? dt : 1e-4 );

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    buildUi( cfg, status );
    render();
}
