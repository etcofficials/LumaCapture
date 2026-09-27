#include "session/RecordingSession.h"

#include "audio/AudioEngine.h"
#include "encode/FramePool.h"
#include "encode/VideoEncoder.h"
#include "gpu/D3D11Device.h"
#include "mux/Muxer.h"
#include "session/BoundedQueue.h"
#include "session/CaptureLoop.h"
#include "session/EncodeLoop.h"
#include "session/PauseTimeline.h"
#include "util/ComScope.h"
#include "util/Log.h"
#include "util/QpcClock.h"
#include "util/StringUtil.h"

#include <windows.h>
#include <dwmapi.h>
#include <timeapi.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <future>

namespace luma::session {
namespace {

std::filesystem::path markerPath(const std::filesystem::path& file)
{
    std::filesystem::path p = file;
    p += L".lumarec";
    return p;
}

} // namespace

RecordingSession::RecordingSession(SessionConfig config) : m_config(std::move(config))
{
    if (m_config.fps <= 0 || m_config.queueCapacity == 0 || m_config.readbackSlots < 2)
        throw std::invalid_argument("Invalid fps / queue capacity / readback slots");
    m_pause = std::make_unique<PauseTimeline>();
    m_composition = std::make_unique<CompositionState>();
}

RecordingSession::~RecordingSession()
{
    if (m_running) {
        try {
            stop();
        } catch (const std::exception& e) {
            log::error("Stopping session failed: {}", e.what());
        }
    }
}

std::pair<int, int> RecordingSession::nativeSize(const SourceConfig& s)
{
    int w = 0, h = 0;
    if (s.crop.right > s.crop.left && s.crop.bottom > s.crop.top && s.kind != SourceKind::Region) {
        w = s.crop.right - s.crop.left;
        h = s.crop.bottom - s.crop.top;
    } else if (s.kind == SourceKind::Window) {
        RECT r{};
        if (FAILED(DwmGetWindowAttribute(s.window, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
            GetWindowRect(s.window, &r);
        w = r.right - r.left;
        h = r.bottom - r.top;
    } else {
        const auto outputs = gpu::enumerateOutputs();
        if (outputs.empty())
            throw std::runtime_error("No display is attached to the desktop");
        if (s.kind == SourceKind::Region) {
            const RECT d = outputs[gpu::outputIndexForRect(outputs, s.region)].desktopRect;
            w = std::min(s.region.right, d.right) - std::max(s.region.left, d.left);
            h = std::min(s.region.bottom, d.bottom) - std::max(s.region.top, d.top);
        } else {
            const RECT d = outputs[std::min<size_t>(s.outputIndex, outputs.size() - 1)].desktopRect;
            w = d.right - d.left;
            h = d.bottom - d.top;
        }
    }
    w = std::max(16, w) & ~1;
    h = std::max(16, h) & ~1;
    return {w, h};
}

void RecordingSession::setError(const std::string& message)
{
    std::lock_guard lock(m_errorMutex);
    if (m_error.empty())
        m_error = message;
    m_failed = true;
}

std::string RecordingSession::errorMessage() const
{
    std::lock_guard lock(m_errorMutex);
    return m_error;
}

size_t RecordingSession::queueDepth() const
{
    return m_queue ? m_queue->size() : 0;
}

double RecordingSession::captureThreadCpuSeconds() const
{
    return m_capture ? m_capture->threadCpuSeconds() : 0;
}

double RecordingSession::encoderFlushMs() const
{
    return m_encode ? m_encode->flushMs() : 0;
}

void RecordingSession::writeRecoveryMarker()
{
    std::ofstream f(markerPath(m_config.outputFile), std::ios::trunc);
    SYSTEMTIME t;
    GetLocalTime(&t);
    f << "LumaCapture recording in progress. If this file remains, the recording was interrupted.\n"
      << "file=" << toUtf8(m_config.outputFile.wstring()) << "\n"
      << std::format("started={:04}-{:02}-{:02} {:02}:{:02}:{:02}\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                     t.wSecond);
}

void RecordingSession::removeRecoveryMarker()
{
    std::error_code ec;
    std::filesystem::remove(markerPath(m_config.outputFile), ec);
}

void RecordingSession::start()
{
    std::lock_guard lifecycle(m_lifecycle);
    if (m_running)
        throw std::logic_error("RecordingSession::start() called twice");
    if (m_config.width <= 0 || m_config.height <= 0) {
        const auto [w, h] = nativeSize(m_config.source);
        m_config.width = w;
        m_config.height = h;
    }
    m_config.width &= ~1;
    m_config.height &= ~1;
    if (m_config.width < 16 || m_config.height < 16)
        throw std::invalid_argument("Output size is too small");

    // 1 ms scheduler granularity so capture timeouts land on the tick grid.
    m_timerPeriodSet = timeBeginPeriod(1) == TIMERR_NOERROR;
    struct TimerGuard {
        RecordingSession* s;
        bool armed = true;
        ~TimerGuard()
        {
            if (armed && s->m_timerPeriodSet) {
                timeEndPeriod(1);
                s->m_timerPeriodSet = false;
            }
        }
    } timerGuard{this};

    encode::VideoEncoderConfig enc = m_config.encoder;
    enc.width = m_config.width;
    enc.height = m_config.height;
    enc.fps = m_config.fps;

    // Pool: every queue entry, every in-flight readback, the held "last picture", one spare.
    const unsigned poolSize = m_config.queueCapacity + m_config.readbackSlots + 2;
    m_pool = std::make_unique<encode::FramePool>(m_config.width, m_config.height, poolSize);
    m_queue = std::make_unique<BoundedQueue<FrameTicket>>(m_config.queueCapacity);
    m_latency.captureMs.reserve(m_config.expectedFrames);
    m_latency.encodeMs.reserve(m_config.expectedFrames);
    m_composition->set(m_config.composition);

    m_encoder = std::make_unique<encode::VideoEncoder>(enc);
    if (m_config.recordAudio && (m_config.audio.system || m_config.audio.mic))
        m_audio = std::make_unique<audio::AudioEngine>(m_config.audio);

    m_muxer = std::make_unique<mux::Muxer>(m_config.outputFile, m_config.container.c_str());
    const int videoStream = m_muxer->addStream(m_encoder->context(), "Video");
    if (m_audio)
        for (auto& t : m_audio->tracks())
            t.stream = m_muxer->addStream(t.encoder->context(), t.encoder->title());
    m_muxer->writeHeader();
    writeRecoveryMarker();

    m_capture = std::make_unique<CaptureLoop>(m_config, *m_pool, *m_queue, m_counters, m_latency.captureMs, *m_pause,
                                              *m_composition);
    m_encode = std::make_unique<EncodeLoop>(*m_pool, *m_queue, *m_encoder, *m_muxer, videoStream, m_counters,
                                            m_latency.encodeMs);

    // D3D objects are created and used only on the capture thread.
    std::promise<void> ready;
    auto readyFuture = ready.get_future();
    m_captureThread = std::thread([this, &ready] {
        SetThreadDescription(GetCurrentThread(), L"luma-capture");
        ComScope com; // Windows.Graphics.Capture needs an apartment on this thread
        try {
            m_capture->init();
        } catch (...) {
            ready.set_exception(std::current_exception());
            return;
        }
        m_startQpc = qpc::now();
        ready.set_value();
        try {
            m_capture->run(m_stop, m_startQpc);
        } catch (const std::exception& e) {
            log::error("Capture thread failed: {}", e.what());
            setError(std::string("Capture failed: ") + e.what());
        } catch (...) {
            // Nothing may escape a std::thread (that would terminate the process).
            log::error("Capture thread failed with a non-standard exception");
            setError("Capture failed (unexpected error)");
        }
    });
    try {
        readyFuture.get();
    } catch (...) {
        m_captureThread.join();
        m_muxer.reset(); // closes the (empty) file
        std::error_code ec;
        std::filesystem::remove(m_config.outputFile, ec);
        removeRecoveryMarker();
        throw;
    }

    m_info.adapterName = m_capture->adapterName();
    m_info.sourceWidth = m_capture->sourceWidth();
    m_info.sourceHeight = m_capture->sourceHeight();
    m_info.desktopWidth = m_info.sourceWidth;
    m_info.desktopHeight = m_info.sourceHeight;
    m_info.width = m_config.width;
    m_info.height = m_config.height;
    m_info.fps = m_config.fps;
    m_info.featureLevel = static_cast<int>(m_capture->featureLevel());
    m_info.encoderDescription = m_encoder->description();
    m_info.audioTracks = m_audio ? static_cast<int>(m_audio->tracks().size()) : 0;
    m_info.framePoolSize = poolSize;
    m_info.framePoolBytes = poolSize * m_pool->bytesPerFrame();
    if (m_config.source.kind != SourceKind::Window) {
        const auto outputs = gpu::enumerateOutputs();
        const unsigned idx = m_config.source.kind == SourceKind::Region
                                 ? gpu::outputIndexForRect(outputs, m_config.source.region)
                                 : m_config.source.outputIndex;
        if (idx < outputs.size()) {
            m_info.outputName = outputs[idx].deviceName;
            m_info.refreshHz = outputs[idx].refreshHz;
        }
    }

    try {
        if (m_audio)
            m_audio->start(m_startQpc, m_pause.get(), m_muxer.get());

        m_encodeThread = std::thread([this] {
            SetThreadDescription(GetCurrentThread(), L"luma-encode");
            try {
                m_encode->run();
            } catch (const std::exception& e) {
                log::error("Encoder thread failed: {}", e.what());
                setError(std::string("Encoding/writing failed: ") + e.what());
            } catch (...) {
                log::error("Encoder thread failed with a non-standard exception");
                setError("Encoding/writing failed (unexpected error)");
            }
            // After a failure keep consuming so the capture side is never wedged;
            // frames are released unencoded. (A no-op after a normal end.)
            FrameTicket t;
            while (m_queue->pop(t))
                m_pool->release(t.slot);
        });
    } catch (...) {
        // Partial start: undo everything that is already running, then report.
        log::error("Recording start failed after capture began; cleaning up");
        m_stop = true;
        m_captureThread.join();
        m_queue->close();
        if (m_encodeThread.joinable())
            m_encodeThread.join();
        if (m_audio)
            m_audio->stop(qpc::now());
        m_muxer.reset();
        std::error_code ec;
        std::filesystem::remove(m_config.outputFile, ec);
        removeRecoveryMarker();
        throw;
    }
    m_running = true;
    timerGuard.armed = false;
    log::info("Recording started: {}x{} @ {} fps -> {}", m_config.width, m_config.height, m_config.fps,
              toUtf8(m_config.outputFile.wstring()));
}

void RecordingSession::stop()
{
    std::lock_guard lifecycle(m_lifecycle);
    if (!m_running)
        return;
    const int64_t t0 = qpc::now();
    m_stop = true;
    m_captureThread.join(); // pushes its remaining readbacks into the queue
    const int64_t stopQpc = qpc::now();
    m_queue->close();
    m_encodeThread.join();  // drains the queue, flushes x264
    if (m_audio) {
        m_audio->stop(stopQpc); // mixes up to the stop time, flushes AAC
        if (m_audio->failed())
            setError("Audio recording failed: " + m_audio->error());
    }
    try {
        m_muxer->finish();
        removeRecoveryMarker();
    } catch (const std::exception& e) {
        setError(std::string("Finalising the file failed: ") + e.what());
    }
    m_running = false;
    m_stopMs = qpc::toMs(qpc::now() - t0);
    if (m_timerPeriodSet) {
        timeEndPeriod(1);
        m_timerPeriodSet = false;
    }
    log::info("Recording stopped: {} frames encoded, {} dropped{}", m_counters.encoded.load(), m_counters.dropped(),
              m_failed ? " (with errors: " + errorMessage() + ")" : "");
}

void RecordingSession::pause()
{
    m_pause->pause(qpc::now());
}

void RecordingSession::resume()
{
    m_pause->resume(qpc::now());
}

bool RecordingSession::paused() const
{
    return m_pause->paused();
}

void RecordingSession::setMicMuted(bool muted)
{
    if (m_audio)
        m_audio->setMicMuted(muted);
}

void RecordingSession::setAudioVolumes(float system, float mic)
{
    if (m_audio)
        m_audio->setVolumes(system, mic);
}

void RecordingSession::setMicFilters(const audio::MicFilterSettings& s)
{
    if (m_audio)
        m_audio->setMicFilters(s);
}

void RecordingSession::setComposition(const gpu::CompositionSettings& s)
{
    m_composition->set(s);
}

SessionStatus RecordingSession::status()
{
    SessionStatus st;
    const int64_t now = qpc::now();
    const int64_t start = m_startQpc.load();
    if (start > 0)
        st.elapsedSeconds = qpc::toSeconds(now - start - m_pause->totalPaused(now));
    st.paused = m_pause->paused();
    st.capturedFrames = m_counters.enqueued;
    st.droppedFrames = m_counters.dropped();
    st.encodedFrames = m_counters.encoded;
    st.bytes = static_cast<uint64_t>(m_counters.encodedBytes.load()) + (m_audio ? m_audio->encodedBytes() : 0);
    st.queueDepth = queueDepth();
    if (m_capture)
        st.sourceStatus = m_capture->status();
    if (m_audio) {
        st.audioStatus = m_audio->status();
        st.systemPeak = m_audio->takeSystemPeak();
        st.micPeak = m_audio->takeMicPeak();
        st.micMuted = m_audio->micMuted();
        if (m_audio->failed())
            setError("Audio recording failed: " + m_audio->error());
    }
    st.failed = m_failed;
    st.error = errorMessage();
    return st;
}

} // namespace luma::session
