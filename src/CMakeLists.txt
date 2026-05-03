# The driver DLL must be named "driver_padvr.dll" and placed in
# driver/padvr/bin/win64/ for SteamVR to load it.
add_library(driver_padvr SHARED
    DriverFactory.cpp
    PadVRProvider.cpp
    PadVRProvider.h
    PadVRController.cpp
    PadVRController.h
    TriggerIPC.cpp
    TriggerIPC.h
    MathUtil.h
)

target_include_directories(driver_padvr PRIVATE
    "${OPENVR_INCLUDE_DIR}"
)

target_link_libraries(driver_padvr PRIVATE
    "${OPENVR_LIB}"
)

target_compile_definitions(driver_padvr PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

set_target_properties(driver_padvr PROPERTIES
    OUTPUT_NAME "driver_padvr"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/driver/padvr/bin/win64"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/driver/padvr/bin/win64"
)

# Multi-config generators (Visual Studio) tack on the config name; force every
# configuration to drop the DLL straight into bin/win64/ where SteamVR looks.
foreach(_cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(driver_padvr PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_SOURCE_DIR}/driver/padvr/bin/win64"
        LIBRARY_OUTPUT_DIRECTORY_${_cfg} "${CMAKE_SOURCE_DIR}/driver/padvr/bin/win64"
    )
endforeach()
