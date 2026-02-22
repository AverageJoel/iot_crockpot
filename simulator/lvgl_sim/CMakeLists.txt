cmake_minimum_required(VERSION 3.20)
project(crockpot_sim C)

set(CMAKE_C_STANDARD 11)

# ── SDL2 ──────────────────────────────────────────────────────────────────────
# Prefer a locally-installed SDL2; fall back to FetchContent.
find_package(SDL2 QUIET CONFIG)
if(NOT SDL2_FOUND)
    message(STATUS "SDL2 not found locally, fetching via FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        SDL2
        URL      https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.zip
        URL_HASH MD5=a9e8c855e39d9f555f56f0c59aff00c2
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL2)
    set(SDL2_TARGET SDL2)
else()
    # find_package may expose SDL2::SDL2 or SDL2::SDL2-static
    if(TARGET SDL2::SDL2)
        set(SDL2_TARGET SDL2::SDL2)
    else()
        set(SDL2_TARGET SDL2)
    endif()
endif()

# ── LVGL v9 ───────────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.2.2
    GIT_SHALLOW    TRUE
)
# Disable LVGL's built-in examples and demos — they use widgets we've disabled
# (LV_USE_SLIDER etc.) and are not needed for the simulator.
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS    ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl)

# LVGL's own cmake sets LV_CONF_INCLUDE_SIMPLE, so it does #include "lv_conf.h".
# Add our simulator root so that include finds our lv_conf.h.
target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Exclude ARM-only assembly (Helium/MVE, NEON) — not valid on x86.
# LVGL's custom.cmake uses GLOB_RECURSE *.S which picks these up unconditionally.
# TARGET_DIRECTORY is required to set the property in the lvgl subdirectory scope.
set_source_files_properties(
    "${lvgl_SOURCE_DIR}/src/draw/sw/blend/helium/lv_blend_helium.S"
    "${lvgl_SOURCE_DIR}/src/draw/sw/blend/neon/lv_blend_neon.S"
    TARGET_DIRECTORY lvgl
    PROPERTIES HEADER_FILE_ONLY TRUE
)

# ── Simulator executable ──────────────────────────────────────────────────────
set(FIRMWARE_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/main")

add_executable(crockpot_sim
    main.c
    stubs/crockpot.c
    stubs/wifi.c
    stubs/display_driver.c
    stubs/esp_timer.c
    ${FIRMWARE_MAIN}/gui.c
)

# Include order matters:
#   1. simulator root    — lv_conf.h
#   2. stubs/            — shadow ESP-IDF headers (esp_log.h, wifi.h, etc.)
#   3. firmware/main/    — gui.h, crockpot.h, gui.c's own #includes
target_include_directories(crockpot_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${FIRMWARE_MAIN}
)

target_link_libraries(crockpot_sim PRIVATE
    lvgl
    ${SDL2_TARGET}
)

# On Windows, SDL2 normally renames main→SDL_main via SDL_main.h.
# We define SDL_MAIN_HANDLED in main.c to keep a plain main(), so SDL2main
# is not needed and -mconsole forces the console entry point.
if(WIN32 AND MINGW)
    target_link_options(crockpot_sim PRIVATE -mconsole)
endif()

# Provide Kconfig symbols that the firmware headers reference.
# display_driver.h uses CONFIG_CROCKPOT_LCD_WIDTH/HEIGHT for LCD_H_RES/LCD_V_RES.
# wifi.h uses CONFIG_CROCKPOT_WIFI_SSID/PASSWORD (not actually called in simulator).
target_compile_definitions(crockpot_sim PRIVATE
    CONFIG_CROCKPOT_LCD_WIDTH=480
    CONFIG_CROCKPOT_LCD_HEIGHT=320
    CONFIG_CROCKPOT_WIFI_SSID="sim_ssid"
    CONFIG_CROCKPOT_WIFI_PASSWORD="sim_pass"
)

# Windows: copy SDL2.dll next to the executable
if(WIN32 AND TARGET SDL2)
    add_custom_command(TARGET crockpot_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2>
            $<TARGET_FILE_DIR:crockpot_sim>
    )
endif()
