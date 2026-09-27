add_executable(luma-tests
    TestMain.cpp
    HardwareTests.cpp
    UnitTests.cpp
)
target_link_libraries(luma-tests PRIVATE luma_core)
target_compile_options(luma-tests PRIVATE /W4)
foreach(_dll IN LISTS LUMA_FFMPEG_RUNTIME_DLLS)
    add_custom_command(TARGET luma-tests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:luma-tests>"
        VERBATIM)
endforeach()
