# Compiles HLSL at build time with fxc.exe into C++ headers holding bytecode,
# so the program needs no runtime shader compiler (d3dcompiler_47.dll).

if(NOT LUMA_FXC)
    set(_sdk_bin "")
    if(DEFINED ENV{WindowsSdkVerBinPath})
        set(_sdk_bin "$ENV{WindowsSdkVerBinPath}/x64")
    endif()
    find_program(LUMA_FXC_FOUND fxc HINTS "${_sdk_bin}" REQUIRED)
    set(LUMA_FXC "${LUMA_FXC_FOUND}" CACHE FILEPATH "" FORCE)
endif()
message(STATUS "fxc: ${LUMA_FXC}")

# luma_compile_shader(<target> <hlsl> <entry> <profile> <var>)
# Produces ${CMAKE_CURRENT_BINARY_DIR}/shaders/<var>.h and adds it to <target>.
function(luma_compile_shader target hlsl entry profile var)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(out "${out_dir}/${var}.h")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
        COMMAND "${LUMA_FXC}" /nologo /O3 /WX /T ${profile} /E ${entry} /Vn ${var} /Fh "${out}" "${hlsl}"
        DEPENDS "${hlsl}"
        COMMENT "fxc ${entry} (${profile}) -> ${var}.h"
        VERBATIM)
    target_sources(${target} PRIVATE "${out}")
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()
