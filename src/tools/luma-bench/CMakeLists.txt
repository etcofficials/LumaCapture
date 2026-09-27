add_executable(luma-bench
    main.cpp
    BenchArgs.cpp
    BenchReport.cpp
)
target_link_libraries(luma-bench PRIVATE luma_core)

# Run from the build tree: put the FFmpeg DLLs next to the exe.
foreach(_dll IN LISTS LUMA_FFMPEG_RUNTIME_DLLS)
    add_custom_command(TARGET luma-bench POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:luma-bench>"
        VERBATIM)
endforeach()
