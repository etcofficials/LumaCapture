// Unit tests for pure logic (no devices). Run: luma-tests.exe

#include "TestMain.h"

#include "audio/AudioRing.h"
#include "encode/FramePool.h"
#include "session/BoundedQueue.h"
#include "session/PauseTimeline.h"
#include "session/RecordingState.h"
#include "webcam/FrameExchange.h"
#include "webcam/WebcamFilters.h"

#include <atomic>
#include <cmath>
#include <format>
#include <thread>
#include <vector>

using namespace luma;
using session::RecEvent;
using session::RecState;

TEST("unit", state_machine_happy_path)
{
    RecState s = RecState::Idle;
    s = session::nextState(s, RecEvent::StartRequested);
    CHECK(s == RecState::Starting);
    s = session::nextState(s, RecEvent::StartSucceeded);
    CHECK(s == RecState::Recording);
    s = session::nextState(s, RecEvent::PauseRequested);
    CHECK(s == RecState::Paused);
    s = session::nextState(s, RecEvent::ResumeRequested);
    CHECK(s == RecState::Recording);
    s = session::nextState(s, RecEvent::StopRequested);
    CHECK(s == RecState::Stopping);
    s = session::nextState(s, RecEvent::SessionStopped);
    CHECK(s == RecState::Finalizing);
    s = session::nextState(s, RecEvent::FinalizeDone);
    CHECK(s == RecState::Idle);
}

TEST("unit", state_machine_rejects_invalid_requests)
{
    // Double start, stop while starting, pause while idle, double stop, start while finalizing...
    CHECK(session::nextState(RecState::Starting, RecEvent::StartRequested) == RecState::Starting);
    CHECK(session::nextState(RecState::Starting, RecEvent::StopRequested) == RecState::Starting);
    CHECK(session::nextState(RecState::Idle, RecEvent::PauseRequested) == RecState::Idle);
    CHECK(session::nextState(RecState::Idle, RecEvent::StopRequested) == RecState::Idle);
    CHECK(session::nextState(RecState::Stopping, RecEvent::StopRequested) == RecState::Stopping);
    CHECK(session::nextState(RecState::Stopping, RecEvent::StartRequested) == RecState::Stopping);
    CHECK(session::nextState(RecState::Finalizing, RecEvent::StartRequested) == RecState::Finalizing);
    CHECK(session::nextState(RecState::Paused, RecEvent::PauseRequested) == RecState::Paused);
    CHECK(session::nextState(RecState::Recording, RecEvent::ResumeRequested) == RecState::Recording);
    // Stop is allowed while paused; errors return to a startable state.
    CHECK(session::nextState(RecState::Paused, RecEvent::StopRequested) == RecState::Stopping);
    CHECK(session::nextState(RecState::Starting, RecEvent::StartFailed) == RecState::Error);
    CHECK(session::nextState(RecState::Error, RecEvent::StartRequested) == RecState::Starting);
    CHECK(session::nextState(RecState::Finalizing, RecEvent::FinalizeFailed) == RecState::Error);
    CHECK(!session::isBusy(RecState::Error) && session::isBusy(RecState::Finalizing));
    // Exhaustive: every (state, event) either stays or moves to a state reachable in the diagram.
    for (int st = 0; st <= static_cast<int>(RecState::Error); ++st)
        for (int ev = 0; ev <= static_cast<int>(RecEvent::FinalizeFailed); ++ev) {
            const RecState n = session::nextState(static_cast<RecState>(st), static_cast<RecEvent>(ev));
            CHECK(static_cast<int>(n) >= 0 && static_cast<int>(n) <= static_cast<int>(RecState::Error));
        }
}

TEST("unit", pause_timeline_offsets)
{
    session::PauseTimeline t;
    t.pause(100);
    CHECK(t.paused());
    CHECK(t.isPausedAt(150));
    CHECK(!t.isPausedAt(50));
    t.resume(300);
    CHECK(!t.paused());
    CHECK(t.pausedBefore(299) == 0);
    CHECK(t.pausedBefore(300) == 200);
    t.pause(500);
    t.resume(550);
    CHECK(t.pausedBefore(1000) == 250);
    CHECK(t.totalPaused(1000) == 250);
    t.pause(900);
    CHECK(t.totalPaused(1000) == 350); // ongoing pause counted
    CHECK(t.isPausedAt(950));
}

TEST("unit", audio_ring_positions_and_silence)
{
    audio::AudioRing ring(1000);
    std::vector<float> in(2 * 100, 0.5f), out(2 * 300, -1.f);
    ring.write(200, in.data(), 100); // gap 0..199 stays silent
    ring.read(0, out.data(), 300);
    CHECK(out[0] == 0.f && out[2 * 199 + 1] == 0.f);
    CHECK(out[2 * 200] == 0.5f && out[2 * 299 + 1] == 0.5f);
    // Late data (before the read position) is dropped, not mixed into the future.
    CHECK(ring.write(100, in.data(), 100) == 0);
    // Data read once is cleared (no stale audio after wrap-around).
    ring.read(300, out.data(), 300);
    ring.read(1200, out.data(), 100);
    CHECK(out[0] == 0.f);
    // Writes too far ahead are clipped to the ring capacity.
    CHECK(ring.write(1300 + 950, in.data(), 100) == 50);
}

TEST("unit", bounded_queue_never_blocks_producer)
{
    session::BoundedQueue<int> q(3);
    CHECK(q.tryPush(1) && q.tryPush(2) && q.tryPush(3));
    CHECK(!q.tryPush(4)); // full: producer gets false immediately
    CHECK(q.size() == 3);
    int v = 0;
    CHECK(q.pop(v) && v == 1);
    CHECK(q.tryPush(4));
    q.close();
    int count = 0;
    while (q.pop(v))
        ++count;
    CHECK(count == 3);
    CHECK(!q.tryPush(5)); // closed
}

TEST("unit", bounded_queue_close_wakes_consumer)
{
    session::BoundedQueue<int> q(2);
    std::atomic<bool> returned{false};
    std::thread consumer([&] {
        int v;
        q.pop(v); // blocks until close
        returned = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(!returned);
    q.close();
    consumer.join();
    CHECK(returned);
}

TEST("unit", frame_exchange_latest_frame_and_no_blocking)
{
    webcam::FrameExchange ex;
    CHECK(!ex.hasFrame());
    for (int i = 0; i < 10; ++i) {
        auto* b = ex.beginWrite(4, 2);
        CHECK(b != nullptr);
        b->pixels[0] = static_cast<uint32_t>(i);
        ex.endWrite(b);
    }
    uint32_t seen = 99;
    CHECK(ex.read([&](const webcam::ImageBuffer& b) { seen = b.pixels[0]; }));
    CHECK(seen == 9); // always the newest frame, never a backlog
    // While a reader holds the newest buffer the writer still gets a free one.
    bool wrote = false;
    ex.read([&](const webcam::ImageBuffer&) {
        auto* b = ex.beginWrite(4, 2);
        wrote = b != nullptr;
        if (b)
            ex.endWrite(b);
    });
    CHECK(wrote);
    ex.clear();
    CHECK(!ex.hasFrame());
}

TEST("unit", frame_pool_is_bounded_and_reusable)
{
    encode::FramePool pool(64, 32, 3);
    const int a = pool.acquire(), b = pool.acquire(), c = pool.acquire();
    CHECK(a >= 0 && b >= 0 && c >= 0);
    CHECK(pool.acquire() == -1); // bounded: no growth
    pool.release(b);
    CHECK(pool.acquire() == b);
}

namespace {
std::vector<uint32_t> makeFrame(unsigned w, unsigned h, uint8_t r, uint8_t g, uint8_t b)
{
    std::vector<uint32_t> px(static_cast<size_t>(w) * h, (uint32_t(r) << 16) | (uint32_t(g) << 8) | b);
    return px;
}
} // namespace

TEST("unit", webcam_processor_identity_and_fastpath)
{
    const unsigned w = 64, h = 48;
    auto src = makeFrame(w, h, 10, 120, 200);
    std::vector<uint32_t> dst(src.size());
    webcam::WebcamProcessor proc;
    webcam::WebcamFilterSettings s; // neutral
    proc.setSettings(s);
    proc.process(reinterpret_cast<const uint8_t*>(src.data()), static_cast<long>(w * 4), w, h, dst.data());
    CHECK(dst[0] == (src[0] | 0xFF000000u));
    s.brightness = 0.2f; // LUT path
    proc.setSettings(s);
    proc.process(reinterpret_cast<const uint8_t*>(src.data()), static_cast<long>(w * 4), w, h, dst.data());
    CHECK(((dst[0] >> 8) & 0xFF) > 120); // brighter green
    s.enabled = false;
    proc.setSettings(s);
    proc.process(reinterpret_cast<const uint8_t*>(src.data()), static_cast<long>(w * 4), w, h, dst.data());
    CHECK(dst[5] == (src[5] | 0xFF000000u)); // bypass is exact
}

TEST("unit", webcam_chroma_key_removes_green)
{
    const unsigned w = 16, h = 16;
    auto src = makeFrame(w, h, 0, 255, 0);
    src[0] = 0x00C08060; // a skin-ish pixel must stay opaque
    std::vector<uint32_t> dst(src.size());
    webcam::WebcamProcessor proc;
    webcam::WebcamFilterSettings s;
    s.chromaKey = true;
    proc.setSettings(s);
    proc.process(reinterpret_cast<const uint8_t*>(src.data()), static_cast<long>(w * 4), w, h, dst.data());
    CHECK((dst[5] >> 24) < 16);   // green -> transparent
    CHECK((dst[0] >> 24) == 255); // subject kept
}

TEST("unit", auto_adjust_is_deterministic_and_sensible)
{
    const unsigned w = 320, h = 240;
    // Dark, bluish scene like the measured C270 image (median ~0.24).
    std::vector<uint32_t> px(static_cast<size_t>(w) * h);
    for (unsigned i = 0; i < px.size(); ++i) {
        const int base = 30 + static_cast<int>(i % 97);
        const int r = base * 80 / 100, g = base, b = std::min(255, base * 115 / 100);
        px[i] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
    const auto st = webcam::analyzeFrame(reinterpret_cast<const uint8_t*>(px.data()), static_cast<long>(w * 4), w, h);
    CHECK(st.samples > 1000);
    CHECK(st.p50 < 0.35);
    webcam::WebcamFilterSettings cur;
    const auto a = webcam::suggestFilters(st, cur);
    const auto b = webcam::suggestFilters(st, a); // re-applying with the same statistics...
    CHECK(a.gamma == b.gamma && a.exposure == b.exposure && a.redGain == b.redGain); // ...changes nothing: no pumping
    CHECK(a.gamma > 1.0f);          // lifts dark mid-tones
    CHECK(a.redGain > 1.0f);        // corrects the blue cast
    CHECK(a.blueGain < 1.0f);
    CHECK(a.gamma <= 1.8f && a.contrast <= 1.25f); // bounded: no crushed shadows / artificial look
    luma::test::note(std::format("dark scene -> gamma {:.2f}, exposure {:.2f}, contrast {:.2f}, R {:.2f}, B {:.2f}",
                                 a.gamma, a.exposure, a.contrast, a.redGain, a.blueGain));
}
