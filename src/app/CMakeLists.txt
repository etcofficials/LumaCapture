# LumaCapture GUI (Qt 6 Widgets) on top of luma_core.
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Concurrent)

add_executable(LumaCapture WIN32
    main.cpp
    AppPaths.cpp
    AppSettings.cpp
    AudioMonitor.cpp
    CrashHandler.cpp
    DeviceScanner.cpp
    DiskWorker.cpp
    History.cpp
    HotkeyManager.cpp
    Icons.cpp
    LayoutDialog.cpp
    MainWindow.cpp
    MediaImporter.cpp
    OverlayRenderer.cpp
    PreviewController.cpp
    RecordingController.cpp
    SettingsStore.cpp
    Theme.cpp
    ThumbnailCache.cpp
    WebcamController.cpp
    WinUtil.cpp
    ui/AboutPage.cpp
    ui/ContextPanels.cpp
    ui/LibraryPage.cpp
    ui/RecordingDelegate.cpp
    ui/RecordPage.cpp
    ui/SettingsPage.cpp
    ui/UiKit.cpp
    ui/WebcamPanel.cpp
    widgets/Widgets.cpp
    ${CMAKE_SOURCE_DIR}/resources/app.rc
    ${CMAKE_SOURCE_DIR}/resources/ui.qrc
)
set_target_properties(LumaCapture PROPERTIES AUTOMOC ON AUTORCC ON)
target_include_directories(LumaCapture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# Build identifier shown on the About page: the short git commit when available.
set(LUMA_BUILD_ID "local")
find_program(LUMA_GIT git HINTS "G:/Git/cmd")
if(LUMA_GIT)
    execute_process(COMMAND "${LUMA_GIT}" rev-parse --short HEAD
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                    OUTPUT_VARIABLE _rev OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _rc)
    if(_rc EQUAL 0 AND _rev)
        set(LUMA_BUILD_ID "${_rev}")
    endif()
endif()
target_compile_definitions(LumaCapture PRIVATE LUMACAPTURE_VERSION="${PROJECT_VERSION}" LUMACAPTURE_BUILD_ID="${LUMA_BUILD_ID}")
target_compile_options(LumaCapture PRIVATE /W4 /wd4127)
target_link_libraries(LumaCapture PRIVATE luma_core Qt6::Widgets Qt6::Concurrent dwmapi user32 gdi32 shell32 dbghelp)

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
