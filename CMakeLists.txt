cmake_minimum_required(VERSION 3.20)
project(ImmersiveLocomotion CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENVR_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/openvr)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)

add_executable(immersive_locomotion
    src/main.cpp
    src/config.cpp
    src/velocity.cpp
    src/boost_model.cpp
    src/mover.cpp
    src/input.cpp
    src/board.cpp
    src/osc.cpp
    src/overlay_ui.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(immersive_locomotion PRIVATE
    ${OPENVR_DIR}/headers
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(immersive_locomotion PRIVATE
    ${OPENVR_DIR}/lib/win64/openvr_api.lib
    winmm
    d3d11
    d3dcompiler
    ws2_32
)

# No console window: run as a Windows GUI app but keep main() as the entry
# point. Status output goes to the in-VR dashboard, not a terminal.
if(MSVC)
    target_link_options(immersive_locomotion PRIVATE
        /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# Runtime layout: exe + openvr_api.dll + manifest/ side by side.
add_custom_command(TARGET immersive_locomotion POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENVR_DIR}/bin/win64/openvr_api.dll
        $<TARGET_FILE_DIR:immersive_locomotion>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/manifest
        $<TARGET_FILE_DIR:immersive_locomotion>/manifest
)
