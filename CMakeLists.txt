cmake_minimum_required(VERSION 3.16)
project(instant_scan C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(instant_scan SHARED
    src/classify.c
    src/detect.c
    src/geometry.c
    src/image_ops.c
)

target_include_directories(instant_scan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (UNIX AND NOT APPLE)
    target_link_libraries(instant_scan m)
endif()
