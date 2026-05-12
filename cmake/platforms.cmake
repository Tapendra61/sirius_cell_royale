# Per-platform settings applied to the cell_royale target.
# Mobile platforms (Android/iOS) wired up in Phase 9.

function(cr_apply_platform_settings target)
    # Compiler warnings
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
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
