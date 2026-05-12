cmake_minimum_required(VERSION 3.24)
project(CellRoyale CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CR_ENABLE_NETWORK    "Build with ENet for multiplayer"     OFF)
option(CR_ENABLE_DEV_TOOLS  "Build with dev console + replay UI"  ON)
option(CR_ENABLE_TELEMETRY  "Send anonymized metrics (opt-in)"    OFF)

include(cmake/FetchDependencies.cmake)
include(cmake/platforms.cmake)

file(GLOB_RECURSE CR_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(cell_royale ${CR_SOURCES})
target_include_directories(cell_royale PRIVATE src)
target_link_libraries(cell_royale PRIVATE raylib raygui mini)

if(CR_ENABLE_DEV_TOOLS)
    target_compile_definitions(cell_royale PRIVATE CR_DEV_TOOLS)
endif()

if(CR_ENABLE_NETWORK)
    target_compile_definitions(cell_royale PRIVATE CR_NETWORK)
    target_link_libraries(cell_royale PRIVATE enet)
endif()

cr_apply_platform_settings(cell_royale)

# Copy tuning.ini next to the executable so the game finds it at runtime.
add_custom_command(TARGET cell_royale POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/tuning.ini
        $<TARGET_FILE_DIR:cell_royale>/tuning.ini)
