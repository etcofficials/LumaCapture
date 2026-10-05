#include "session/CaptureLoop.h"

#include "capture/DesktopDuplicator.h"
#include "capture/WindowCapture.h"
#include "session/PreviewExchange.h"
#include "util/HResult.h"
#include "util/Log.h"
#include "util/ProcessMetrics.h"
#include "util/QpcClock.h"
#include "webcam/FrameExchange.h"

#include <windows.h>
#include <avrt.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace luma::session {
namespace {

// Registers the thread with MMCSS ("Capture" task) for more stable tick timing.
class MmcssScope {
public:
    MmcssScope()
    {
        DWORD index = 0;
        m_handle = AvSetMmThreadCharacteristicsW(L"Capture", &index);
        if (!m_handle)
            log::warn("MMCSS registration failed (error {}); capture thread runs at normal priority", GetLastError());
    }
    ~MmcssScope()
    {
        if (m_handle)
            AvRevertMmThreadCharacteristics(m_handle);
    }
    MmcssScope(const MmcssScope&) = delete;
    MmcssScope& operator=(const MmcssScope&) = delete;

private:
    HANDLE m_handle = nullptr;
};

void atomicMax(std::atomic<int64_t>& target, int64_t value)
{
    int64_t cur = target.load(std::memory_order_relaxed);
    while (value > cur && !target.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {
    }
}

constexpr double kClickSeconds = 0.5;

} // namespace

CaptureLoop::CaptureLoop(const SessionConfig& config, encode::FramePool& pool, BoundedQueue<FrameTicket>& queue,
                         SessionCounters& counters, std::vector<float>& captureLatencyMs, const PauseTimeline& pause,
                         const CompositionState& composition)
    : m_config(config), m_pool(pool), m_queue(queue), m_counters(counters), m_captureLatencyMs(captureLatencyMs),
      m_pause(pause), m_compositionState(composition)
{
}

CaptureLoop::~CaptureLoop()
{
    destroyPipeline();
}

void CaptureLoop::setStatus(const std::string& s)
{
    std::lock_guard lock(m_statusMutex);
    m_status = s;
}

std::string CaptureLoop::status() const
{
    std::lock_guard lock(m_statusMutex);
    return m_status;
}

void CaptureLoop::init()
{
    buildPipeline();
    // Wait for the first image so tick 0 already has a picture.
    const int64_t deadline = qpc::now() + qpc::fromSeconds(1.0);
    while (!m_source->hasImage() && qpc::now() < deadline)
        waitForSource(50);
    if (!m_source->hasImage())
        log::warn("No source image received within 1 s; early ticks will be dropped until one arrives");
}

void CaptureLoop::buildPipeline()
{
    const SourceConfig& sc = m_config.source;
    m_crop = sc.crop;
    if (sc.kind == SourceKind::Window) {
        if (!capture::WindowCapture::isSupported())
            throw std::runtime_error("Window capture (Windows.Graphics.Capture) is not supported on this system");
        m_device = std::make_unique<gpu::D3D11Device>(gpu::D3D11Device::ForWindowCapture{});
        m_source = std::make_unique<capture::WindowCapture>(*m_device, sc.window, sc.captureCursor);
    } else {
        const auto outputs = gpu::enumerateOutputs();
        if (outputs.empty())
            throw std::runtime_error("No display is attached to the desktop");
        unsigned index = sc.outputIndex;
        if (sc.kind == SourceKind::Region)
            index = gpu::outputIndexForRect(outputs, sc.region);
        if (index >= outputs.size())
            throw std::runtime_error("The selected display is no longer connected");
        const gpu::OutputInfo& out = outputs[index];
        if (sc.kind == SourceKind::Region) {
            // Region in source (monitor) pixels, clipped to that monitor.
            m_crop.left = std::max(sc.region.left, out.desktopRect.left) - out.desktopRect.left;
            m_crop.top = std::max(sc.region.top, out.desktopRect.top) - out.desktopRect.top;
            m_crop.right = std::min(sc.region.right, out.desktopRect.right) - out.desktopRect.left;
            m_crop.bottom = std::min(sc.region.bottom, out.desktopRect.bottom) - out.desktopRect.top;
        }
        m_device = std::make_unique<gpu::D3D11Device>(out);
        m_source = std::make_unique<capture::DesktopDuplicator>(*m_device, sc.captureCursor);
    }
    buildCompositor();
    m_dirty = true;
}

void CaptureLoop::buildCompositor()
{
    m_ring.reset();
    resetPreview();
    m_compositor.reset();
    const auto w = static_cast<unsigned>(m_config.width), h = static_cast<unsigned>(m_config.height);
    m_compositor = std::make_unique<gpu::Compositor>(m_device->device(), w, h, m_config.cpuConvert);
    if (m_config.previewOnly) {
        // Nothing is read back for encoding in preview-only mode.
    } else if (m_config.cpuConvert) {
        m_ring = std::make_unique<gpu::ReadbackRing>(
            m_device->device(), std::vector<ID3D11Texture2D*>{m_compositor->bgraTexture()}, m_config.readbackSlots);
        // Composition and scaling happen on the GPU; libswscale only converts BGRA -> NV12.
        m_sws.reset(sws_getContext(m_config.width, m_config.height, AV_PIX_FMT_BGRA, m_config.width, m_config.height,
                                   AV_PIX_FMT_NV12, SWS_POINT, nullptr, nullptr, nullptr));
        if (!m_sws)
            throw std::runtime_error("sws_getContext(BGRA -> NV12) failed");
        const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
        ff::check(sws_setColorspaceDetails(m_sws.get(), bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16),
                  "sws_setColorspaceDetails");
    } else {
        m_ring = std::make_unique<gpu::ReadbackRing>(
            m_device->device(),
            std::vector<ID3D11Texture2D*>{m_compositor->yTexture(), m_compositor->uvTexture()}, m_config.readbackSlots);
    }
    // Settings and webcam frame must be re-uploaded to the new compositor.
    m_compositionVersion = 0;
    m_webcamSeq = 0;
    m_webcamShown = false;
}

void CaptureLoop::destroyPipeline()
{
    m_ring.reset();
    resetPreview();
    m_compositor.reset();
    m_source.reset();
    m_device.reset();
}

int64_t CaptureLoop::tickTime(int64_t k) const
{
    return m_startQpc + k * qpc::frequency() / m_config.fps;
}

void CaptureLoop::run(const std::atomic<bool>& stop, int64_t startQpc)
{
    MmcssScope mmcss;
    m_startQpc = startQpc;
    int64_t next = 0;

    while (!stop.load(std::memory_order_relaxed)) {
        try {
            pollMouseButtons();
            const int64_t now = qpc::now();
            const int64_t due = tickTime(next);
            if (now < due) {
                auto timeoutMs = static_cast<unsigned>(qpc::toMs(due - now));
                if (timeoutMs > 0) {
                    // While a readback is in flight, wake every kReadbackPollMs to hand it
                    // to the encoder as soon as the GPU finishes, instead of at the next tick.
                    if (m_ring && m_ring->inFlight() > 0)
                        timeoutMs = std::min(timeoutMs, kReadbackPollMs);
                    else if (m_previewPending)
                        timeoutMs = std::min(timeoutMs, 10u);
                    waitForSource(timeoutMs);
                    drainPending(false);
                    if (m_previewPending)
                        servicePreview();
                    continue;
                }
            }

            const int64_t current = (now - m_startQpc) * m_config.fps / qpc::frequency();
            if (current > next) {
                // Ticks skipped while paused are not drops.
                int64_t late = 0;
                for (int64_t k = next; k < current; ++k)
                    if (!m_pause.isPausedAt(tickTime(k)))
                        ++late;
                m_counters.droppedLateTicks += late;
                next = current;
            }
            const int64_t tq = tickTime(next);
            ++next;
            if (m_pause.isPausedAt(tq)) {
                ++m_counters.pausedTicks;
                drainPending(false);
                continue;
            }
            // Output time = wall time since start minus the paused time before it.
            const int64_t media = tq - m_startQpc - m_pause.pausedBefore(tq);
            const auto pts = static_cast<int64_t>(
                std::llround(static_cast<double>(media) * m_config.fps / static_cast<double>(qpc::frequency())));
            if (pts <= m_lastPts)
                continue; // rounding collision right after a resume
            m_lastPts = pts;
            processTick(pts, tq);
            drainPending(false);
        } catch (const HResultError& e) {
            const bool lost = isDeviceLost(e.code()) || (m_device && FAILED(m_device->removedReason()));
            if (!lost)
                throw;
            handleDeviceLost(e);
        }
    }

    drainAll();
    if (m_lastSlot >= 0) {
        m_pool.release(m_lastSlot);
        m_lastSlot = -1;
    }
    m_threadCpuSeconds = currentThreadCpuSeconds();
}

void CaptureLoop::waitForSource(unsigned timeoutMs)
{
    if (!m_source->isOpen()) {
        tryReopen(timeoutMs);
        return;
    }
    const uint64_t before = m_source->imageUpdates();
    const auto result = m_source->acquire(timeoutMs);
    m_counters.desktopCopies += static_cast<int64_t>(m_source->imageUpdates() - before);
    if (result == capture::CaptureSource::Result::Lost) {
        ++m_counters.accessLostEvents;
        log::warn("Capture access lost (mode change, secure desktop or full-screen switch); reopening");
        tryReopen(timeoutMs);
    }
}

void CaptureLoop::tryReopen(unsigned timeoutMs)
{
    const int64_t now = qpc::now();
    if (m_lastReopenQpc != 0 && qpc::toMs(now - m_lastReopenQpc) < 250) {
        Sleep(std::min(timeoutMs, 50u));
        return;
    }
    m_lastReopenQpc = now;
    if (!m_source->reopen()) {
        setStatus(m_source->status());
        Sleep(std::min(timeoutMs, 50u));
        return;
    }
    log::info("Capture reopened ({}x{})", m_source->width(), m_source->height());
    m_dirty = true;
}

void CaptureLoop::pollMouseButtons()
{
    if (!m_composition.cursor.clicks)
        return;
    const int vks[2] = {VK_LBUTTON, VK_RBUTTON};
    for (int b = 0; b < 2; ++b) {
        const SHORT state = GetAsyncKeyState(vks[b]);
        const bool down = (state & 0x8000) != 0;
        const bool pressed = (down && !m_buttonDown[b]) || (!down && (state & 1));
        m_buttonDown[b] = down;
        if (!pressed)
            continue;
        POINT p{};
        GetCursorPos(&p);
        // Replace the oldest ring.
        auto oldest = std::min_element(m_clicks.begin(), m_clicks.end(),
                                       [](const Click& a, const Click& c) { return a.qpc < c.qpc; });
        *oldest = Click{qpc::now(), p, b + 1};
        m_dirty = true;
    }
}

void CaptureLoop::updateInputs()
{
    // Composition settings (UI changes while recording).
    if (m_compositionState.fetch(m_compositionVersion, m_composition)) {
        m_compositor->setSettings(m_device->context(), m_composition);
        m_dirty = true;
    }

    // Webcam: upload only when a new processed frame is available.
    if (m_config.webcamFrames && m_composition.webcam.enabled) {
        webcam::FrameExchange& ex = *m_config.webcamFrames;
        const uint64_t seq = ex.sequence();
        if (seq != m_webcamSeq) {
            const bool got = ex.read([&](const webcam::ImageBuffer& b) {
                m_compositor->setWebcamFrame(m_device->context(), b.pixels.data(), b.width, b.height);
            });
            m_webcamSeq = seq;
            if (got) {
                m_webcamShown = true;
                m_dirty = true;
            }
        } else if (m_webcamShown && !ex.hasFrame()) {
            m_compositor->clearWebcam(); // camera stopped or failed: hide the overlay
            m_webcamShown = false;
            m_dirty = true;
        }
    }

    // Cursor highlight follows the mouse.
    if (m_composition.cursor.highlight) {
        POINT p{};
        if (GetCursorPos(&p) && (p.x != m_lastMouse.x || p.y != m_lastMouse.y)) {
            m_lastMouse = p;
            m_dirty = true;
        }
        m_mouseValid = true;
    }
    // Click rings animate: keep producing new frames while any is active.
    if (m_composition.cursor.clicks) {
        const int64_t now = qpc::now();
        for (const Click& c : m_clicks)
            if (c.button && qpc::toSeconds(now - c.qpc) < kClickSeconds)
                m_dirty = true;
    }

    const std::string st = m_source->status();
    if (st != status())
        setStatus(st);
}

void CaptureLoop::processTick(int64_t pts, int64_t tickQpc)
{
    ++m_counters.ticks;
    if (!m_config.previewOnly) {
        const auto depth = static_cast<int64_t>(m_queue.size());
        m_counters.queueDepthSum += depth;
        ++m_counters.queueDepthSamples;
        atomicMax(m_counters.queueDepthMax, depth);
    }

    updateInputs();
    if (m_source->takeDirty()) {
        ++m_counters.desktopUpdates;
        m_dirty = true;
    }

    if (m_config.previewOnly) {
        // Nothing is encoded: compose only changed pictures, for the preview.
        if (m_dirty && m_source->hasImage()) {
            const gpu::FrameInputs in = frameInputs();
            m_compositor->render(m_device->context(), in);
            m_device->context()->Flush();
            m_dirty = false;
            m_previewDirty = true;
        }
        servicePreview();
        return;
    }

    if (!m_dirty || !m_source->hasImage()) {
        pushPending({false, -1, pts, tickQpc}); // repeat previous picture (or no image yet)
        servicePreview();
        return;
    }

    const int slot = m_pool.acquire();
    if (slot < 0) {
        ++m_counters.droppedPoolEmpty; // stays dirty: next tick retries
        return;
    }
    if (m_ring->full())
        drainPending(true); // the only place the capture thread may wait on the GPU

    const gpu::FrameInputs in = frameInputs();
    ID3D11DeviceContext* ctx = m_device->context();
    m_compositor->render(ctx, in);
    if (m_config.cpuConvert)
        m_ring->submit(ctx, {m_compositor->bgraTexture()});
    else
        m_ring->submit(ctx, {m_compositor->yTexture(), m_compositor->uvTexture()});
    m_previewDirty = true;
    servicePreview(); // queued behind the encoder readback; never waits on the GPU
    ctx->Flush();     // start GPU work now; it is read back shortly after
    pushPending({true, slot, pts, tickQpc});
    m_dirty = false;
}

gpu::FrameInputs CaptureLoop::frameInputs()
{
    gpu::FrameInputs in;
    in.source = m_source->srv();
    in.srcWidth = m_source->width();
    in.srcHeight = m_source->height();
    in.crop = m_crop;
    in.cursor = m_source->cursorOverlay();
    in.cursorPlacement = m_source->cursorPlacement();
    const POINT origin = m_source->screenOrigin();
    if (m_composition.cursor.highlight && m_mouseValid) {
        in.mouseValid = true;
        in.mouseX = static_cast<float>(m_lastMouse.x - origin.x);
        in.mouseY = static_cast<float>(m_lastMouse.y - origin.y);
    }
    if (m_composition.cursor.clicks) {
        const int64_t now = qpc::now();
        for (size_t i = 0; i < m_clicks.size(); ++i) {
            const Click& c = m_clicks[i];
            const double age = qpc::toSeconds(now - c.qpc) / kClickSeconds;
            if (c.button && age < 1.0)
                in.clicks[i] = gpu::ClickRing{static_cast<float>(c.pos.x - origin.x),
                                              static_cast<float>(c.pos.y - origin.y), static_cast<float>(age), c.button};
        }
    }
    return in;
}

void CaptureLoop::resetPreview()
{
    m_previewStaging.Reset();
    m_previewW = m_previewH = 0;
    m_previewPending = false;
    m_previewDirty = false;
    m_lastPreviewQpc = 0;
}

void CaptureLoop::servicePreview()
{
    if (!m_config.preview || m_config.cpuConvert || !m_compositor || !m_device)
        return;
    ID3D11DeviceContext* ctx = m_device->context();

    // 1) Hand over the copy started earlier, if the GPU has finished it.
    if (m_previewPending) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = ctx->Map(m_previewStaging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
            return; // not ready: try again on a later wake-up
        check(hr, "Map(preview staging)");
        m_config.preview->publish(static_cast<const uint8_t*>(mapped.pData), mapped.RowPitch, m_previewW, m_previewH);
        ctx->Unmap(m_previewStaging.Get(), 0);
        m_previewPending = false;
    }

    // 2) Start a new copy when there is a newer picture and the interval has passed.
    if (!m_previewDirty)
        return;
    const int64_t now = qpc::now();
    if (m_lastPreviewQpc != 0 && qpc::toMs(now - m_lastPreviewQpc) < m_config.previewIntervalMs * 0.8)
        return;
    unsigned reqW = 0, reqH = 0;
    m_config.preview->requestedSize(reqW, reqH);
    if (reqW < 16 || reqH < 16)
        return; // nobody is looking at the preview right now
    // Fit the requested box with the output aspect ratio; never larger than the output.
    const double scale = std::min({static_cast<double>(reqW) / m_config.width,
                                   static_cast<double>(reqH) / m_config.height, 1.0});
    const unsigned pw = std::max(16u, static_cast<unsigned>(m_config.width * scale) & ~1u);
    const unsigned ph = std::max(16u, static_cast<unsigned>(m_config.height * scale) & ~1u);
    if (!m_compositor->ensurePreview(pw, ph))
        return;
    if (!m_previewStaging || pw != m_previewW || ph != m_previewH) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = pw;
        desc.Height = ph;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        m_previewStaging.Reset();
        check(m_device->device()->CreateTexture2D(&desc, nullptr, &m_previewStaging), "CreateTexture2D(preview staging)");
        m_previewW = pw;
        m_previewH = ph;
    }
    m_compositor->renderPreview(ctx);
    ctx->CopyResource(m_previewStaging.Get(), m_compositor->previewTexture());
    m_previewPending = true;
    m_previewDirty = false;
    m_lastPreviewQpc = now;
}

void CaptureLoop::pushPending(const Pending& p)
{
    while (m_pendingCount == kMaxPending)
        drainPending(true);
    m_pending[(m_pendingHead + m_pendingCount) % kMaxPending] = p;
    ++m_pendingCount;
}

void CaptureLoop::drainPending(bool forceOne)
{
    while (m_pendingCount > 0) {
        const Pending p = m_pending[m_pendingHead];
        if (p.readback) {
            gpu::ReadbackRing::Mapped mapped;
            const auto r = m_ring->mapOldest(m_device->context(), forceOne, mapped);
            if (!r.mapped)
                break; // GPU not finished; try again later
            if (r.stalled) {
                ++m_counters.gpuStalls;
                m_counters.gpuStallMicros += static_cast<int64_t>(r.stallMs * 1000.0);
            }
            copyToFrame(p.slot, mapped);
            m_ring->unmapOldest(m_device->context());
            forceOne = false;

            if (m_lastSlot >= 0)
                m_pool.release(m_lastSlot);
            m_lastSlot = p.slot; // the reservation reference becomes the "last picture" hold
            enqueue(p.slot, p.pts, p.tickQpc, true);
        } else if (m_lastSlot >= 0) {
            enqueue(m_lastSlot, p.pts, p.tickQpc, false);
        } else {
            ++m_counters.droppedNoImage;
        }
        m_pendingHead = (m_pendingHead + 1) % kMaxPending;
        --m_pendingCount;
    }
}

void CaptureLoop::drainAll()
{
    while (m_pendingCount > 0)
        drainPending(true);
}

void CaptureLoop::copyToFrame(int slot, const gpu::ReadbackRing::Mapped& mapped)
{
    AVFrame* f = m_pool.frame(slot);
    const int64_t t0 = qpc::now();
    if (m_config.cpuConvert) {
        const uint8_t* src[1] = {mapped.data[0]};
        const int stride[1] = {static_cast<int>(mapped.pitch[0])};
        sws_scale(m_sws.get(), src, stride, 0, m_config.height, f->data, f->linesize);
        m_counters.cpuConvertMicros += static_cast<int64_t>(qpc::toMs(qpc::now() - t0) * 1000.0);
        return;
    }
    const size_t rowBytes = static_cast<size_t>(m_config.width); // Y: 1 byte/px; UV: 2 bytes per 2 px
    for (int y = 0; y < m_config.height; ++y)
        std::memcpy(f->data[0] + static_cast<ptrdiff_t>(y) * f->linesize[0],
                    mapped.data[0] + static_cast<size_t>(y) * mapped.pitch[0], rowBytes);
    for (int y = 0; y < m_config.height / 2; ++y)
        std::memcpy(f->data[1] + static_cast<ptrdiff_t>(y) * f->linesize[1],
                    mapped.data[1] + static_cast<size_t>(y) * mapped.pitch[1], rowBytes);
    m_counters.readbackCopyMicros += static_cast<int64_t>(qpc::toMs(qpc::now() - t0) * 1000.0);
}

void CaptureLoop::enqueue(int slot, int64_t pts, int64_t tickQpc, bool unique)
{
    m_pool.addRef(slot); // before push: the encoder may release it immediately
    const int64_t now = qpc::now();
    if (!m_queue.tryPush(FrameTicket{slot, pts, tickQpc, now})) {
        m_pool.release(slot);
        ++m_counters.droppedQueueFull;
        return;
    }
    ++m_counters.enqueued;
    ++(unique ? m_counters.uniqueFrames : m_counters.repeatedFrames);
    const double latencyMs = qpc::toMs(now - tickQpc);
    m_counters.lastCaptureLatencyUs.store(static_cast<int64_t>(latencyMs * 1000.0), std::memory_order_relaxed);
    if (m_captureLatencyMs.size() < m_captureLatencyMs.capacity())
        m_captureLatencyMs.push_back(static_cast<float>(latencyMs));
}

void CaptureLoop::handleDeviceLost(const std::exception& e)
{
    ++m_counters.deviceLostEvents;
    const HRESULT reason = m_device ? m_device->removedReason() : E_FAIL;
    log::error("GPU device lost: {} (removed reason {})", e.what(), hresultToString(reason));
    setStatus("The graphics device was reset; recovering capture...");

    // Frames still waiting on the old device are gone.
    while (m_pendingCount > 0) {
        const Pending& p = m_pending[m_pendingHead];
        if (p.readback)
            m_pool.release(p.slot);
        ++m_counters.droppedDeviceLost;
        m_pendingHead = (m_pendingHead + 1) % kMaxPending;
        --m_pendingCount;
    }
    m_pendingHead = 0;
    destroyPipeline();

    for (int attempt = 1;; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        try {
            buildPipeline();
            log::info("GPU pipeline recreated after device loss (attempt {})", attempt);
            setStatus("");
            return;
        } catch (const std::exception& retry) {
            if (attempt >= 10)
                throw std::runtime_error(std::string("Could not recreate GPU pipeline after device loss: ") + retry.what());
            log::warn("Recreating GPU pipeline failed (attempt {}): {}", attempt, retry.what());
            destroyPipeline();
        }
    }
}

} // namespace luma::session
