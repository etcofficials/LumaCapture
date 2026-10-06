cmake_minimum_required(VERSION 3.28)

project(LumaCapture
    VERSION 2.0.0
    DESCRIPTION "Lightweight screen recorder for low-end Windows PCs"
    LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "LumaCapture targets Windows with MSVC (Direct3D 11 / DXGI / WASAPI).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

set(LUMA_FFMPEG_DIR "G:/DevTools/ffmpeg" CACHE PATH "FFmpeg shared dev build (include/, lib/, bin/)")
set(LUMA_FXC "" CACHE FILEPATH "fxc.exe shader compiler (auto-detected from the Windows SDK when empty)")

include(cmake/LumaFFmpeg.cmake)
include(cmake/LumaShaders.cmake)

add_subdirectory(src)
