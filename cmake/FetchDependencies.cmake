include(FetchContent)

# --- raylib ---
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        5.5
    GIT_SHALLOW    TRUE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# --- raygui (header-only) ---
FetchContent_Declare(raygui
    GIT_REPOSITORY https://github.com/raysan5/raygui.git
    GIT_TAG        4.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(raygui)
add_library(raygui INTERFACE)
target_include_directories(raygui INTERFACE ${raygui_SOURCE_DIR}/src)

# --- mINI (header-only INI parser) ---
FetchContent_Declare(mini
    GIT_REPOSITORY https://github.com/pulzed/mINI.git
    GIT_TAG        0.9.18
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(mini)
add_library(mini INTERFACE)
target_include_directories(mini INTERFACE ${mini_SOURCE_DIR}/src)

# --- ENet (multiplayer, only when CR_ENABLE_NETWORK) ---
if(CR_ENABLE_NETWORK)
    FetchContent_Declare(enet
        GIT_REPOSITORY https://github.com/lsalzman/enet.git
        GIT_TAG        v1.3.18
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(enet)
endif()
