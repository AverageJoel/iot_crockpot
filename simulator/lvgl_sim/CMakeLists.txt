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
FetchContent_MakeAvailable(lvgl)

# Tell LVGL where our lv_conf.h is (absolute path, works from any build dir)
target_compile_definitions(lvgl PUBLIC
    "LV_CONF_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h\""
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

# Windows: copy SDL2.dll next to the executable
if(WIN32 AND TARGET SDL2)
    add_custom_command(TARGET crockpot_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2>
            $<TARGET_FILE_DIR:crockpot_sim>
    )
endif()
