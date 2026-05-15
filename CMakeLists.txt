cmake_minimum_required(VERSION 3.24)
project(CellRoyale CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CR_ENABLE_NETWORK    "Build with ENet for multiplayer"     ON)
option(CR_ENABLE_DEV_TOOLS  "Build with dev console + replay UI"  ON)
option(CR_ENABLE_TELEMETRY  "Send anonymized metrics (opt-in)"    OFF)

include(cmake/FetchDependencies.cmake)
include(cmake/platforms.cmake)

# --- cell_royale_core: sim/transport/core. No raylib, no I/O dependencies.
#     Linked by both the game binary and the test binary.
file(GLOB_RECURSE CR_CORE_SOURCES CONFIGURE_DEPENDS
    src/ai/*.cpp
    src/core/*.cpp
    src/meta/*.cpp
    src/sim/*.cpp
    src/transport/*.cpp)
add_library(cell_royale_core STATIC ${CR_CORE_SOURCES})
target_include_directories(cell_royale_core PUBLIC src)
target_link_libraries(cell_royale_core PUBLIC mini)
cr_apply_platform_settings(cell_royale_core)

# NetworkTransport lives in core/transport/, so the CR_NETWORK define + ENet link
# need to apply to the core lib too -- not just the cell_royale executable.
if(CR_ENABLE_NETWORK)
    target_compile_definitions(cell_royale_core PUBLIC CR_NETWORK)
    target_link_libraries(cell_royale_core PUBLIC enet)
endif()

# --- cell_royale: main executable (links raylib + core, includes client/)
file(GLOB_RECURSE CR_CLIENT_SOURCES CONFIGURE_DEPENDS
    src/client/*.cpp
    src/feel/*.cpp
    src/platform/*.cpp)
add_executable(cell_royale src/main.cpp ${CR_CLIENT_SOURCES})
target_link_libraries(cell_royale PRIVATE cell_royale_core raylib raygui)

if(CR_ENABLE_DEV_TOOLS)
    target_compile_definitions(cell_royale PRIVATE CR_DEV_TOOLS)
endif()

# CR_NETWORK + enet propagate from cell_royale_core since they're declared PUBLIC there
# (NetworkTransport.cpp lives in the core lib, so the executable inherits both).

cr_apply_platform_settings(cell_royale)

# Copy tuning.ini next to the executable so the game finds it at runtime.
add_custom_command(TARGET cell_royale POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/tuning.ini
        $<TARGET_FILE_DIR:cell_royale>/tuning.ini)

# --- cell_royale_test: determinism check, no raylib.
add_executable(cell_royale_test test/determinism_test.cpp)
target_link_libraries(cell_royale_test PRIVATE cell_royale_core)
cr_apply_platform_settings(cell_royale_test)

enable_testing()
add_test(NAME determinism COMMAND cell_royale_test)
