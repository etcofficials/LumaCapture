# luma_core: the recording engine. No Qt dependency - shared by the LumaCapture GUI and luma-bench.
add_library(luma_core STATIC
    util/Log.cpp
    util/HResult.cpp
    util/QpcClock.cpp
    util/ProcessMetrics.cpp
    util/GpuMetrics.cpp
    util/StringUtil.cpp
    gpu/D3D11Device.cpp
    gpu/CursorOverlay.cpp
    gpu/Compositor.cpp
    gpu/ReadbackRing.cpp
    capture/PointerShape.cpp
    capture/DesktopDuplicator.cpp
    capture/WindowCapture.cpp
    capture/Screenshot.cpp
    webcam/WebcamCapture.cpp
    webcam/WebcamFilters.cpp
    audio/AudioDevices.cpp
    audio/WasapiSource.cpp
    audio/MicProcessor.cpp
    audio/AudioEngine.cpp
    encode/FramePool.cpp
    encode/VideoEncoder.cpp
    encode/AudioEncoder.cpp
    mux/Muxer.cpp
    session/CaptureLoop.cpp
    session/EncodeLoop.cpp
    session/RecordingSession.cpp
)

set(_hlsl "${CMAKE_CURRENT_SOURCE_DIR}/gpu/shaders/Compose.hlsl")
luma_compile_shader(luma_core "${_hlsl}" VSMain       vs_4_0 g_VSMain)
luma_compile_shader(luma_core "${_hlsl}" PS_Y         ps_4_0 g_PS_Y)
luma_compile_shader(luma_core "${_hlsl}" PS_UV        ps_4_0 g_PS_UV)
luma_compile_shader(luma_core "${_hlsl}" PS_Composite ps_4_0 g_PS_Composite)

target_include_directories(luma_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(luma_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(luma_core PUBLIC /permissive- /utf-8 /Zc:__cplusplus PRIVATE /W4 /bigobj)
target_link_libraries(luma_core
    PUBLIC  FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample FFmpeg::avfilter
    PRIVATE d3d11 dxgi pdh avrt winmm dwmapi mfplat mfreadwrite mf mfuuid strmiids ole32 windowsapp)
