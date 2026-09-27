# LumaCapture GUI (Qt 6 Widgets) on top of luma_core.
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Concurrent)

add_executable(LumaCapture WIN32
    main.cpp
    AppPaths.cpp
    AppSettings.cpp
    AudioMonitor.cpp
    CrashHandler.cpp
    DeviceScanner.cpp
    History.cpp
    HotkeyManager.cpp
    Icons.cpp
    LayoutDialog.cpp
    MainWindow.cpp
    OverlayRenderer.cpp
    RecordingController.cpp
    SettingsDialog.cpp
    Theme.cpp
    WebcamController.cpp
    WinUtil.cpp
    widgets/Widgets.cpp
    ${CMAKE_SOURCE_DIR}/resources/app.rc
    ${CMAKE_SOURCE_DIR}/resources/ui.qrc
)
set_target_properties(LumaCapture PROPERTIES AUTOMOC ON AUTORCC ON)
target_include_directories(LumaCapture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(LumaCapture PRIVATE LUMACAPTURE_VERSION="${PROJECT_VERSION}")
target_compile_options(LumaCapture PRIVATE /W4 /wd4127)
target_link_libraries(LumaCapture PRIVATE luma_core Qt6::Widgets Qt6::Concurrent dwmapi user32 gdi32 dbghelp)

# Make build/<preset>/bin runnable: Qt runtime (windeployqt) + FFmpeg DLLs.
get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
find_program(LUMA_WINDEPLOYQT NAMES windeployqt HINTS "${_qt_bin}" NO_DEFAULT_PATH REQUIRED)
add_custom_command(TARGET LumaCapture POST_BUILD
    COMMAND "${LUMA_WINDEPLOYQT}" --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler
            --no-opengl-sw --no-compiler-runtime --no-network --skip-plugin-types generic,networkinformation,tls
            --verbose 0 "$<TARGET_FILE:LumaCapture>"
    COMMENT "windeployqt (build tree)"
    VERBATIM)
foreach(_dll IN LISTS LUMA_FFMPEG_RUNTIME_DLLS)
    add_custom_command(TARGET LumaCapture POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:LumaCapture>"
        VERBATIM)
endforeach()
