# Imports the prebuilt FFmpeg shared libraries (BtbN win64-gpl-shared layout).
# Creates one imported target per library, e.g. FFmpeg::avcodec, and the list
# LUMA_FFMPEG_RUNTIME_DLLS for copying next to executables.

set(_ff_libs avcodec avformat avutil swscale swresample avfilter postproc)

if(NOT EXISTS "${LUMA_FFMPEG_DIR}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg headers not found under LUMA_FFMPEG_DIR='${LUMA_FFMPEG_DIR}'")
endif()

set(LUMA_FFMPEG_RUNTIME_DLLS "")
foreach(_lib IN LISTS _ff_libs)
    file(GLOB _dll "${LUMA_FFMPEG_DIR}/bin/${_lib}-*.dll")
    if(NOT _dll OR NOT EXISTS "${LUMA_FFMPEG_DIR}/lib/${_lib}.lib")
        message(FATAL_ERROR "FFmpeg library '${_lib}' missing in ${LUMA_FFMPEG_DIR}")
    endif()
    add_library(FFmpeg::${_lib} SHARED IMPORTED GLOBAL)
    set_target_properties(FFmpeg::${_lib} PROPERTIES
        IMPORTED_IMPLIB "${LUMA_FFMPEG_DIR}/lib/${_lib}.lib"
        IMPORTED_LOCATION "${_dll}"
        INTERFACE_INCLUDE_DIRECTORIES "${LUMA_FFMPEG_DIR}/include")
    list(APPEND LUMA_FFMPEG_RUNTIME_DLLS "${_dll}")
endforeach()

file(STRINGS "${LUMA_FFMPEG_DIR}/include/libavcodec/version_major.h" _ff_ver REGEX "#define LIBAVCODEC_VERSION_MAJOR")
message(STATUS "FFmpeg: ${LUMA_FFMPEG_DIR} (${_ff_ver})")
