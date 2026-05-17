# Per-platform settings applied to the cell_royale target.
# Mobile platforms (Android/iOS) wired up in Phase 9.

function(cr_apply_platform_settings target)
    # Compiler warnings.
    #
    # Three toolchains in play across the CI matrix:
    #   - MSVC cl.exe              (Windows / Visual Studio generator)
    #   - LLVM clang.exe           (Windows / Ninja generator)  -- regular clang
    #                              with the GNU-style driver, NOT clang-cl.
    #                              Simulates the MSVC ABI so links against the
    #                              MS C++ runtime, but the command-line syntax
    #                              is gcc-style (`-Wall`, not `/W4`).
    #   - gcc / AppleClang         (Linux / macOS)
    #
    # CMake's `MSVC` variable is TRUE for ALL THREE of cl.exe, clang-cl, AND
    # regular clang on Windows (because all three simulate the MSVC ABI). So
    # `if(MSVC)` alone would push `/W4 /permissive-` into regular-clang, which
    # rejects them with "unknown argument" errors. We split on
    # CMAKE_CXX_COMPILER_FRONTEND_VARIANT instead -- it's "MSVC" for cl.exe
    # and clang-cl (which take /W-style flags) and "GNU" for regular clang
    # (which wants -W-style flags). Falls back to the GNU branch if the
    # variant is unset on some exotic toolchain -- a safe default since gcc
    # syntax is the broader convention.
    if(MSVC AND NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        # cl.exe or clang-cl -- MSVC-style command line.
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        # gcc, AppleClang, regular clang anywhere (including LLVM clang on
        # Windows + Ninja), MinGW gcc -- all accept gcc-style flags.
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wno-missing-field-initializers)
    endif()

    # Platform-specific tweaks (stubs — filled in across later phases)
    if(WIN32)
        # Release builds will set SUBSYSTEM:WINDOWS later to hide the console.
    elseif(APPLE)
        # Universal binary + bundle config lands in Phase 9.
    elseif(UNIX)
        # AppImage packaging in Phase 9.
    endif()
endfunction()
