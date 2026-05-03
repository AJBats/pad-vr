cmake_minimum_required(VERSION 3.20)
project(padVR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "padVR currently targets Windows only.")
endif()

# OpenVR headers + import lib expected at third_party/openvr (clone from
# https://github.com/ValveSoftware/openvr).
set(OPENVR_ROOT "${CMAKE_SOURCE_DIR}/third_party/openvr" CACHE PATH "OpenVR SDK root")
set(OPENVR_INCLUDE_DIR "${OPENVR_ROOT}/headers")
set(OPENVR_LIB "${OPENVR_ROOT}/lib/win64/openvr_api.lib")

if(NOT EXISTS "${OPENVR_INCLUDE_DIR}/openvr_driver.h")
    message(WARNING
        "OpenVR SDK not found at ${OPENVR_ROOT}.\n"
        "Run: git clone https://github.com/ValveSoftware/openvr third_party/openvr")
endif()

add_subdirectory(src)
add_subdirectory(companion)
