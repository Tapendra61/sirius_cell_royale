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
    # ENet's CMake only sets include_directories for the enet target's own .c files;
    # downstream consumers (cell_royale_core) need the headers too. Promote include/
    # to a PUBLIC interface include directory on the target so target_link_libraries
    # downstream just works.
    target_include_directories(enet PUBLIC ${enet_SOURCE_DIR}/include)
    # Windows system libraries ENet needs at link time:
    #   - ws2_32: Winsock 2 (WSAStartup, gethostbyname, htons, socket, ...)
    #   - winmm:  multimedia timer (timeGetTime, used by enet_time_get)
    # ENet 1.3.18's CMakeLists.txt already does
    # `target_link_libraries(enet ws2_32 winmm)` but the link interface
    # doesn't always propagate cleanly through FetchContent_MakeAvailable
    # to downstream consumers under every generator. The Ninja generator
    # with LLVM clang on Windows in particular ends up with NEITHER lib on
    # the final executable's link line, producing ~20 lld-link "undefined
    # symbol" errors on WSAStartup / gethostbyname / htons / etc. MSVC's
    # cl.exe escapes this because ENet's win32.c relies on the linker's
    # `/DEFAULTLIB`-style lib auto-detection, which lld-link in GNU-driver
    # mode doesn't honour.
    #
    # Promoting these libs to the enet target's PUBLIC link interface here
    # makes them unconditionally available to anything that links enet,
    # regardless of generator. Duplicate-link is a no-op for any platform
    # linker so this is safe even when ENet's CMake already added them.
    if(WIN32)
        target_link_libraries(enet PUBLIC ws2_32 winmm)
    endif()
endif()
