add_executable(padvr_companion
    main.cpp
)

target_compile_definitions(padvr_companion PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(padvr_companion PRIVATE
    XInput
)

# Silent operation: link as a Windows-subsystem app so no console window
# appears at launch. Keep main() as the entry point (vs. WinMain) by telling
# the linker to use mainCRTStartup explicitly. Process is invisible until
# the user looks in Task Manager.
if(MSVC)
    target_link_options(padvr_companion PRIVATE
        "/SUBSYSTEM:WINDOWS"
        "/ENTRY:mainCRTStartup"
    )
endif()
