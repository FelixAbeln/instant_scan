# Starter Android NDK CMake file.
#
# In a real Android project this file usually lives under app/src/main/cpp/.
# Adjust INSTANT_SCAN_ROOT if this repository is included as a submodule.

cmake_minimum_required(VERSION 3.16)
project(instant_scan_android C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# When copied into an Android project, point this at the repository root.
set(INSTANT_SCAN_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

add_library(instant_scan SHARED
    ${INSTANT_SCAN_ROOT}/src/classify.c
    ${INSTANT_SCAN_ROOT}/src/detect.c
    ${INSTANT_SCAN_ROOT}/src/geometry.c
    ${INSTANT_SCAN_ROOT}/src/image_ops.c
)

target_include_directories(instant_scan PUBLIC
    ${INSTANT_SCAN_ROOT}/include
)

# Add your JNI wrapper as a separate library and link it to instant_scan.
# Example:
# add_library(instant_scan_jni SHARED instant_scan_jni.c)
# target_link_libraries(instant_scan_jni instant_scan log android)
