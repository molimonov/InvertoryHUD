cmake_minimum_required(VERSION 3.20)
project(InvertoryHUD LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT: the DLL is dropped next to the game and must not require a
# separately installed Visual C++ runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(HUD_SOURCES
    src/main/dllmain.cpp
    src/common/logger.cpp
    src/common/util.cpp
    src/config/config.cpp
    src/game/game.cpp
    src/game/inventory.cpp
    src/input/mouse_hook.cpp
    src/render/d3d9_hook.cpp
    src/render/dds_decrypt.cpp
    src/render/font_atlas.cpp
    src/render/game_texture.cpp
    src/render/hud.cpp
)

add_library(InvertoryHUD SHARED ${HUD_SOURCES})
target_include_directories(InvertoryHUD PRIVATE src)
target_compile_definitions(InvertoryHUD PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(InvertoryHUD PRIVATE /EHa /utf-8 /W3 /permissive-)
target_link_libraries(InvertoryHUD PRIVATE user32.lib gdi32.lib)
set_target_properties(InvertoryHUD PROPERTIES
    PREFIX ""
    OUTPUT_NAME "InvertoryHUD"
)

add_executable(Injector src/injector/main.cpp)
target_compile_definitions(Injector PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(Injector PRIVATE /utf-8 /W3)
set_target_properties(Injector PROPERTIES OUTPUT_NAME "Injector")

configure_file(src/launcher/launcher.rc.in ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc @ONLY)
add_executable(S2HUDLauncher WIN32 src/launcher/main.cpp ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc)
target_compile_definitions(S2HUDLauncher PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(S2HUDLauncher PRIVATE /utf-8 /W3)
target_link_libraries(S2HUDLauncher PRIVATE shell32.lib user32.lib)
set_target_properties(S2HUDLauncher PROPERTIES OUTPUT_NAME "S2_HUD")

add_executable(Probe src/tools/probe.cpp)
target_compile_definitions(Probe PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(Probe PRIVATE /utf-8 /W3)
target_link_libraries(Probe PRIVATE psapi.lib)
set_target_properties(Probe PROPERTIES OUTPUT_NAME "Probe")

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(PAYLOAD_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/payload_data.cpp)
add_custom_command(
    OUTPUT ${PAYLOAD_CPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_payload.py"
            "$<TARGET_FILE:InvertoryHUD>" "$<TARGET_FILE:S2HUDLauncher>"
            "${CMAKE_CURRENT_SOURCE_DIR}/config/InvertoryHUD.ini" "${PAYLOAD_CPP}"
    DEPENDS InvertoryHUD S2HUDLauncher "${CMAKE_CURRENT_SOURCE_DIR}/config/InvertoryHUD.ini"
    VERBATIM)

configure_file(src/installer/installer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/installer.rc @ONLY)
add_executable(InvertoryHUD_Setup WIN32
    src/installer/main.cpp
    ${PAYLOAD_CPP}
    ${CMAKE_CURRENT_BINARY_DIR}/installer.rc)
add_dependencies(InvertoryHUD_Setup InvertoryHUD S2HUDLauncher)
target_include_directories(InvertoryHUD_Setup PRIVATE src)
target_compile_definitions(InvertoryHUD_Setup PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(InvertoryHUD_Setup PRIVATE /utf-8 /W3)
target_link_libraries(InvertoryHUD_Setup PRIVATE shell32.lib ole32.lib uuid.lib user32.lib)
add_custom_command(TARGET InvertoryHUD_Setup POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:InvertoryHUD_Setup> "${CMAKE_SOURCE_DIR}/dist/InvertoryHUD_Setup.exe"
    VERBATIM)

# Optional convenience: copy both binaries into the game's bin directory.
# Pass -DGAME_BIN_DIR="C:/.../bin" when configuring. The build itself never
# writes into the game directory unless this variable is set.
if(GAME_BIN_DIR)
    add_custom_command(TARGET InvertoryHUD POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:InvertoryHUD> "${GAME_BIN_DIR}/InvertoryHUD.dll"
        VERBATIM)
    add_custom_command(TARGET Injector POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Injector> "${GAME_BIN_DIR}/Injector.exe"
        VERBATIM)
    add_custom_command(TARGET Probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Probe> "${GAME_BIN_DIR}/Probe.exe"
        VERBATIM)
    add_custom_command(TARGET S2HUDLauncher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:S2HUDLauncher> "${GAME_BIN_DIR}/S2_HUD.exe"
        VERBATIM)
endif()
