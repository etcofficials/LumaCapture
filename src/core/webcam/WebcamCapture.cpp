#include "webcam/WebcamCapture.h"

#include "util/ComScope.h"
#include "util/HResult.h"
#include "util/Log.h"
#include "util/QpcClock.h"
#include "util/StringUtil.h"

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <strmif.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <tuple>

namespace luma::webcam {

using Microsoft::WRL::ComPtr;

namespace {

// MFStartup/MFShutdown are reference counted; one guard per using thread.
class MfScope {
public:
    MfScope() { m_hr = MFStartup(MF_VERSION, MFSTARTUP_LITE); }
    ~MfScope()
    {
        if (SUCCEEDED(m_hr))
            MFShutdown();
    }
    bool ok() const { return SUCCEEDED(m_hr); }

private:
    HRESULT m_hr;
};

// Receives asynchronous source-reader results; the camera thread waits on `ready`.
class ReaderCallback final : public IMFSourceReaderCallback {
public:
    ReaderCallback()
    {
        m_ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_flushed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    ~ReaderCallback()
    {
        CloseHandle(m_ready);
        CloseHandle(m_flushed);
    }

    STDMETHODIMP QueryInterface(REFIID iid, void** out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReaderCallback)) {
            *out = static_cast<IMFSourceReaderCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return r;
    }
    STDMETHODIMP OnReadSample(HRESULT hr, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override
    {
        {
            std::lock_guard lock(m_mutex);
            m_hr = hr;
            m_flags = flags;
            m_sample = sample;
            m_has = true;
        }
        SetEvent(m_ready);
        return S_OK;
    }
    STDMETHODIMP OnFlush(DWORD) override
    {
        SetEvent(m_flushed);
        return S_OK;
    }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }

    HANDLE readyEvent() const { return m_ready; }
    HANDLE flushedEvent() const { return m_flushed; }
    bool take(HRESULT& hr, DWORD& flags, ComPtr<IMFSample>& sample)
    {
        std::lock_guard lock(m_mutex);
        if (!m_has)
            return false;
        hr = m_hr;
        flags = m_flags;
        sample = std::move(m_sample);
        m_has = false;
        return true;
    }

private:
    LONG m_ref = 1;
    HANDLE m_ready = nullptr;
    HANDLE m_flushed = nullptr;
    std::mutex m_mutex;
    HRESULT m_hr = S_OK;
    DWORD m_flags = 0;
    ComPtr<IMFSample> m_sample;
    bool m_has = false;
};

std::string subtypeName(const GUID& g)
{
    if (g == MFVideoFormat_NV12) return "NV12";
    if (g == MFVideoFormat_YUY2) return "YUY2";
    if (g == MFVideoFormat_MJPG) return "MJPG";
    if (g == MFVideoFormat_RGB24) return "RGB24";
    if (g == MFVideoFormat_RGB32) return "RGB32";
    if (g == MFVideoFormat_I420) return "I420";
    if (g == MFVideoFormat_H264) return "H264";
    char buf[8] = {};
    const DWORD fcc = g.Data1;
    std::memcpy(buf, &fcc, 4);
    return buf;
}

// Lower is better: uncompressed formats cost less CPU than MJPEG decoding.
int formatRank(const std::string& f)
{
    if (f == "NV12") return 0;
    if (f == "YUY2") return 1;
    if (f == "I420") return 2;
    if (f == "RGB32" || f == "RGB24") return 3;
    if (f == "MJPG") return 4;
    return 9; // H264 etc.: supported via decoder, least preferred
}

ComPtr<IMFMediaSource> createSource(const std::wstring& link)
{
    ComPtr<IMFAttributes> attr;
    check(MFCreateAttributes(&attr, 2), "MFCreateAttributes");
    check(attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), "SetGUID");
    check(attr->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link.c_str()), "SetString");
    ComPtr<IMFMediaSource> source;
    check(MFCreateDeviceSource(attr.Get(), &source), "MFCreateDeviceSource");
    return source;
}

bool readMode(IMFMediaType* type, WebcamMode& m)
{
    GUID major{}, sub{};
    if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) || major != MFMediaType_Video)
        return false;
    type->GetGUID(MF_MT_SUBTYPE, &sub);
    UINT32 w = 0, h = 0, num = 0, den = 0;
    if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h)))
        return false;
    MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den);
    m.width = w;
    m.height = h;
    m.fpsNum = num;
    m.fpsDen = den ? den : 1;
    m.format = subtypeName(sub);
    return w > 0 && h > 0;
}

std::string friendlyError(HRESULT hr)
{
    switch (hr) {
    case E_ACCESSDENIED:
        return "Windows denied camera access. Check Settings > Privacy > Camera (allow desktop apps).";
    case MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED:
        return "The camera was disconnected.";
    case MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED:
        return "The camera is being used by another application.";
    case MF_E_HW_MFT_FAILED_START_STREAMING:
        return "The camera could not start streaming (it may be in use by another application).";
    case HRESULT_FROM_WIN32(ERROR_TIMEOUT):
        return "The camera stopped delivering frames.";
    default:
        return hresultToString(hr);
    }
}

// --- Hardware controls ------------------------------------------------------

struct PropName {
    long id;
    const char* name;
};
const PropName kProcAmp[] = {
    {VideoProcAmp_Brightness, "Brightness"}, {VideoProcAmp_Contrast, "Contrast"}, {VideoProcAmp_Hue, "Hue"},
    {VideoProcAmp_Saturation, "Saturation"}, {VideoProcAmp_Sharpness, "Sharpness"}, {VideoProcAmp_Gamma, "Gamma"},
    {VideoProcAmp_WhiteBalance, "White balance"}, {VideoProcAmp_BacklightCompensation, "Backlight compensation"},
    {VideoProcAmp_Gain, "Gain"},
};
const PropName kCamCtl[] = {
    {CameraControl_Exposure, "Exposure"}, {CameraControl_Focus, "Focus"}, {CameraControl_Zoom, "Zoom"},
    {CameraControl_Pan, "Pan"}, {CameraControl_Tilt, "Tilt"},
};

// KS property access for controls DirectShow's interfaces do not name
// (power-line frequency, auto-exposure priority). Same layout for both sets.
bool ksGet(IKsControl* ks, const GUID& set, ULONG id, LONG& value)
{
    KSPROPERTY_VIDEOPROCAMP_S s{};
    s.Property.Set = set;
    s.Property.Id = id;
    s.Property.Flags = KSPROPERTY_TYPE_GET;
    ULONG ret = 0;
    if (FAILED(ks->KsProperty(&s.Property, sizeof(s), &s, sizeof(s), &ret)))
        return false;
    value = s.Value;
    return true;
}

HRESULT ksSet(IKsControl* ks, const GUID& set, ULONG id, LONG value)
{
    KSPROPERTY_VIDEOPROCAMP_S s{};
    s.Property.Set = set;
    s.Property.Id = id;
    s.Property.Flags = KSPROPERTY_TYPE_SET;
    s.Value = value;
    s.Flags = KSPROPERTY_VIDEOPROCAMP_FLAGS_MANUAL;
    ULONG ret = 0;
    return ks->KsProperty(&s.Property, sizeof(s), &s, sizeof(s), &ret);
}

std::vector<CameraControl> queryControls(IMFMediaSource* source)
{
    std::vector<CameraControl> out;
    ComPtr<IAMVideoProcAmp> amp;
    if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&amp)))) {
        for (const auto& p : kProcAmp) {
            CameraControl c;
            long flags = 0, capFlags = 0;
            if (FAILED(amp->GetRange(p.id, &c.min, &c.max, &c.step, &c.defaultValue, &capFlags)) || c.max <= c.min)
                continue;
            amp->Get(p.id, &c.value, &flags);
            c.kind = CameraControl::Kind::ProcAmp;
            c.property = p.id;
            c.name = p.name;
            c.autoSupported = (capFlags & VideoProcAmp_Flags_Auto) != 0;
            c.autoEnabled = (flags & VideoProcAmp_Flags_Auto) != 0;
            out.push_back(c);
        }
    }
    ComPtr<IAMCameraControl> cam;
    if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&cam)))) {
        for (const auto& p : kCamCtl) {
            CameraControl c;
            long flags = 0, capFlags = 0;
            if (FAILED(cam->GetRange(p.id, &c.min, &c.max, &c.step, &c.defaultValue, &capFlags)) || c.max <= c.min)
                continue;
            cam->Get(p.id, &c.value, &flags);
            c.kind = CameraControl::Kind::Camera;
            c.property = p.id;
            c.name = p.name;
            c.autoSupported = (capFlags & CameraControl_Flags_Auto) != 0;
            c.autoEnabled = (flags & CameraControl_Flags_Auto) != 0;
            out.push_back(c);
        }
    }
    ComPtr<IKsControl> ks;
    if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&ks)))) {
        LONG v = 0;
        if (ksGet(ks.Get(), PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_POWERLINE_FREQUENCY, v)) {
            CameraControl c;
            c.kind = CameraControl::Kind::PowerLine;
            c.name = "Anti-flicker (power line)";
            c.min = 0;
            c.max = 2;
            c.value = v;
            c.defaultValue = v;
            out.push_back(c);
        }
        if (ksGet(ks.Get(), PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY, v)) {
            CameraControl c;
            c.kind = CameraControl::Kind::ExposurePriority;
            c.name = "Low-light frame rate priority";
            c.min = 0;
            c.max = 1;
            c.value = v;
            c.defaultValue = v;
            out.push_back(c);
        }
    }
    return out;
}

void applyControl(IMFMediaSource* source, const CameraControl& c)
{
    HRESULT hr = E_NOINTERFACE;
    switch (c.kind) {
    case CameraControl::Kind::Camera: {
        ComPtr<IAMCameraControl> cam;
        if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&cam))))
            hr = cam->Set(c.property, c.value, c.autoEnabled ? CameraControl_Flags_Auto : CameraControl_Flags_Manual);
        break;
    }
    case CameraControl::Kind::ProcAmp: {
        ComPtr<IAMVideoProcAmp> amp;
        if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&amp))))
            hr = amp->Set(c.property, c.value, c.autoEnabled ? VideoProcAmp_Flags_Auto : VideoProcAmp_Flags_Manual);
        break;
    }
    case CameraControl::Kind::PowerLine:
    case CameraControl::Kind::ExposurePriority: {
        ComPtr<IKsControl> ks;
        if (SUCCEEDED(source->QueryInterface(IID_PPV_ARGS(&ks)))) {
            hr = c.kind == CameraControl::Kind::PowerLine
                     ? ksSet(ks.Get(), PROPSETID_VIDCAP_VIDEOPROCAMP, KSPROPERTY_VIDEOPROCAMP_POWERLINE_FREQUENCY, c.value)
                     : ksSet(ks.Get(), PROPSETID_VIDCAP_CAMERACONTROL, KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY,
                             c.value);
        }
        break;
    }
    }
    if (FAILED(hr))
        log::warn("Camera control '{}' could not be set: {}", c.name, hresultToString(hr));
}

// Nearest-neighbour downscale of a BGRX frame to `dstW` wide (for the before/after preview).
void writeSmallCopy(FrameExchange& ex, const uint8_t* src, long pitch, unsigned w, unsigned h, unsigned dstW)
{
    dstW = std::min(dstW, w);
    const unsigned dstH = std::max(1u, h * dstW / w);
    ImageBuffer* b = ex.beginWrite(dstW, dstH);
    if (!b)
        return;
    for (unsigned y = 0; y < dstH; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(src + static_cast<long>(y * h / dstH) * pitch);
        uint32_t* out = b->pixels.data() + static_cast<size_t>(y) * dstW;
        for (unsigned x = 0; x < dstW; ++x)
            out[x] = row[x * w / dstW] | 0xFF000000u;
    }
    ex.endWrite(b);
}

} // namespace

std::vector<WebcamDeviceInfo> enumerateWebcams()
{
    ComScope com;
    MfScope mf;
    std::vector<WebcamDeviceInfo> out;
    ComPtr<IMFAttributes> attr;
    check(MFCreateAttributes(&attr, 1), "MFCreateAttributes");
    check(attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID), "SetGUID");
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    check(MFEnumDeviceSources(attr.Get(), &devices, &count), "MFEnumDeviceSources");
    for (UINT32 i = 0; i < count; ++i) {
        WCHAR* name = nullptr;
        WCHAR* link = nullptr;
        UINT32 len = 0;
        WebcamDeviceInfo info;
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len))) {
            info.name = name;
            CoTaskMemFree(name);
        }
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &len))) {
            info.symbolicLink = link;
            CoTaskMemFree(link);
        }
        devices[i]->Release();
        if (!info.symbolicLink.empty())
            out.push_back(std::move(info));
    }
    CoTaskMemFree(devices);
    return out;
}

std::vector<WebcamMode> enumerateWebcamModes(const std::wstring& link)
{
    ComScope com;
    MfScope mf;
    ComPtr<IMFMediaSource> source = createSource(link);
    ComPtr<IMFSourceReader> reader;
    const HRESULT hr = MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader);
    if (FAILED(hr)) {
        source->Shutdown();
        throw std::runtime_error(friendlyError(hr));
    }
    // Keep the best format for each (width, height, fps).
    std::map<std::tuple<unsigned, unsigned, unsigned, unsigned>, WebcamMode> best;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), i, &type)))
            break;
        WebcamMode m;
        if (!readMode(type.Get(), m))
            continue;
        const auto key = std::make_tuple(m.width, m.height, m.fpsNum, m.fpsDen);
        auto it = best.find(key);
        if (it == best.end() || formatRank(m.format) < formatRank(it->second.format))
            best[key] = m;
    }
    reader.Reset(); // no reads were issued: releasing and shutting down on this thread is safe
    source->Shutdown();
    std::vector<WebcamMode> out;
    for (auto& [k, m] : best)
        out.push_back(m);
    std::sort(out.begin(), out.end(), [](const WebcamMode& a, const WebcamMode& b) {
        if (a.width * a.height != b.width * b.height)
            return a.width * a.height > b.width * b.height;
        return a.fps() > b.fps();
    });
    return out;
}

WebcamCapture::WebcamCapture() = default;

WebcamCapture::~WebcamCapture()
{
    stop();
}

void WebcamCapture::start(const std::wstring& link, const WebcamMode& mode)
{
    stop();
    m_stop = false;
    m_finished = false;
    m_state = State::Starting;
    {
        std::lock_guard lock(m_mutex);
        m_message.clear();
        m_controls.clear();
        m_filtersChanged = true;
        m_stats = {};
    }
    m_thread = std::thread(&WebcamCapture::run, this, link, mode);
}

void WebcamCapture::requestStop()
{
    m_stop = true;
}

void WebcamCapture::stop()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join(); // bounded: the camera thread checks m_stop at least every 100 ms
    m_frames.clear();
    m_raw.clear();
    m_state = State::Stopped;
}

void WebcamCapture::setFilters(const WebcamFilterSettings& filters)
{
    std::lock_guard lock(m_mutex);
    m_filters = filters;
    m_filtersChanged = true;
}

void WebcamCapture::setControl(const CameraControl& control)
{
    std::lock_guard lock(m_mutex);
    m_pendingControls.push_back(control);
}

void WebcamCapture::requestAnalysis()
{
    m_analysisRequested = true;
}

bool WebcamCapture::takeAnalysis(FrameStats& out)
{
    std::lock_guard lock(m_mutex);
    if (!m_analysisReady)
        return false;
    out = m_analysis;
    m_analysisReady = false;
    return true;
}

std::string WebcamCapture::message() const
{
    std::lock_guard lock(m_mutex);
    return m_message;
}

std::vector<CameraControl> WebcamCapture::controls() const
{
    std::lock_guard lock(m_mutex);
    return m_controls;
}

CameraStats WebcamCapture::stats() const
{
    std::lock_guard lock(m_mutex);
    return m_stats;
}

void WebcamCapture::setError(const std::string& msg)
{
    {
        std::lock_guard lock(m_mutex);
        m_message = msg;
    }
    m_state = State::Error;
    log::warn("Webcam: {}", msg);
}

void WebcamCapture::run(std::wstring link, WebcamMode mode)
{
    SetThreadDescription(GetCurrentThread(), L"luma-webcam");
    struct FinishedGuard {
        std::atomic<bool>& f;
        ~FinishedGuard() { f = true; }
    } finishedGuard{m_finished};

    ComScope com;
    MfScope mf;
    if (!com.ok() || !mf.ok()) {
        setError("Media Foundation could not be initialised");
        return;
    }
    WebcamProcessor processor;
    std::deque<int64_t> arrivals; // QPC of recent frames (fps / gap statistics)
    double processMsAvg = 0;

    // Retries after disconnects / errors until stop is requested.
    while (!m_stop) {
        ComPtr<IMFMediaSource> source;
        ComPtr<IMFSourceReader> reader;
        ComPtr<ReaderCallback> callback;
        callback.Attach(new ReaderCallback());
        bool readPending = false;
        try {
            source = createSource(link);
            {
                const auto controls = queryControls(source.Get());
                std::lock_guard lock(m_mutex);
                m_controls = controls;
            }

            ComPtr<IMFAttributes> attr;
            check(MFCreateAttributes(&attr, 2), "MFCreateAttributes");
            attr->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
            attr->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get());
            check(MFCreateSourceReaderFromMediaSource(source.Get(), attr.Get(), &reader),
                  "MFCreateSourceReaderFromMediaSource");

            const auto stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
            // Select the requested native mode (size, rate and the chosen format).
            bool selected = false;
            for (DWORD i = 0; !selected; ++i) {
                ComPtr<IMFMediaType> type;
                if (FAILED(reader->GetNativeMediaType(stream, i, &type)))
                    break;
                WebcamMode m;
                if (readMode(type.Get(), m) && m.width == mode.width && m.height == mode.height &&
                    m.fpsNum * mode.fpsDen == mode.fpsNum * m.fpsDen && (mode.format.empty() || m.format == mode.format)) {
                    check(reader->SetCurrentMediaType(stream, nullptr, type.Get()), "SetCurrentMediaType(native)");
                    selected = true;
                }
            }
            if (!selected && mode.width > 0)
                log::warn("Webcam: requested mode {}x{} {} not offered by the camera; using its default mode",
                          mode.width, mode.height, mode.format);

            // Ask the reader to convert (decode MJPEG / convert YUV) to RGB32.
            ComPtr<IMFMediaType> out;
            check(MFCreateMediaType(&out), "MFCreateMediaType");
            out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            check(reader->SetCurrentMediaType(stream, nullptr, out.Get()), "SetCurrentMediaType(RGB32)");
            ComPtr<IMFMediaType> actual;
            check(reader->GetCurrentMediaType(stream, &actual), "GetCurrentMediaType");
            UINT32 w = 0, h = 0;
            check(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &w, &h), "MF_MT_FRAME_SIZE");
            const LONG defaultStride = static_cast<LONG>(MFGetAttributeUINT32(actual.Get(), MF_MT_DEFAULT_STRIDE, w * 4));

            check(reader->ReadSample(stream, 0, nullptr, nullptr, nullptr, nullptr), "ReadSample(first)");
            readPending = true;
            log::info("Webcam started: {}x{} ({} requested {:.2f} fps)", w, h, mode.format, mode.fps());
            {
                std::lock_guard lock(m_mutex);
                m_message.clear();
                m_stats.width = w;
                m_stats.height = h;
            }

            int64_t lastFrameQpc = qpc::now();
            while (!m_stop) {
                // Controls / filters requested by the UI (applied between frames).
                std::vector<CameraControl> pending;
                {
                    std::lock_guard lock(m_mutex);
                    pending.swap(m_pendingControls);
                    if (m_filtersChanged) {
                        processor.setSettings(m_filters);
                        m_filtersChanged = false;
                    }
                }
                for (const auto& c : pending) {
                    applyControl(source.Get(), c);
                    std::lock_guard lock(m_mutex);
                    for (auto& known : m_controls)
                        if (known.kind == c.kind && known.property == c.property) {
                            known.value = c.value;
                            known.autoEnabled = c.autoEnabled;
                        }
                }

                if (WaitForSingleObject(callback->readyEvent(), 100) != WAIT_OBJECT_0) {
                    // No frame yet. A healthy camera delivers at least every ~0.5 s even in the dark.
                    if (qpc::toSeconds(qpc::now() - lastFrameQpc) > 5.0)
                        throw HResultError(HRESULT_FROM_WIN32(ERROR_TIMEOUT), "camera stalled");
                    continue;
                }
                HRESULT hr = S_OK;
                DWORD flags = 0;
                ComPtr<IMFSample> sample;
                if (!callback->take(hr, flags, sample))
                    continue;
                readPending = false;
                if (FAILED(hr))
                    throw HResultError(hr, "ReadSample");
                if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))
                    throw HResultError(MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED, "camera stream ended");
                // Request the next frame right away so the camera never waits for our processing.
                check(reader->ReadSample(stream, 0, nullptr, nullptr, nullptr, nullptr), "ReadSample");
                readPending = true;
                if (!sample)
                    continue;

                const int64_t now = qpc::now();
                lastFrameQpc = now;
                m_state = State::Running;
                arrivals.push_back(now);
                while (!arrivals.empty() && qpc::toSeconds(now - arrivals.front()) > 1.0)
                    arrivals.pop_front();

                ComPtr<IMFMediaBuffer> buffer;
                check(sample->ConvertToContiguousBuffer(&buffer), "ConvertToContiguousBuffer");
                BYTE* scan0 = nullptr;
                LONG pitch = 0;
                ComPtr<IMF2DBuffer> buf2d;
                bool locked2d = false;
                BYTE* raw = nullptr;
                if (SUCCEEDED(buffer.As(&buf2d)) && SUCCEEDED(buf2d->Lock2D(&scan0, &pitch))) {
                    locked2d = true;
                } else {
                    DWORD maxLen = 0, curLen = 0;
                    check(buffer->Lock(&raw, &maxLen, &curLen), "IMFMediaBuffer::Lock");
                    pitch = defaultStride;
                    scan0 = pitch < 0 ? raw + static_cast<size_t>(-pitch) * (h - 1) : raw;
                }
                if (m_analysisRequested.exchange(false)) {
                    const FrameStats st = analyzeFrame(scan0, pitch, w, h);
                    std::lock_guard lock(m_mutex);
                    m_analysis = st;
                    m_analysisReady = true;
                }
                if (m_compare)
                    writeSmallCopy(m_raw, scan0, pitch, w, h, 480);
                const int64_t p0 = qpc::now();
                if (ImageBuffer* dst = m_frames.beginWrite(w, h)) {
                    processor.process(scan0, pitch, w, h, dst->pixels.data());
                    m_frames.endWrite(dst);
                }
                const double pms = qpc::toMs(qpc::now() - p0);
                processMsAvg = processMsAvg == 0 ? pms : processMsAvg * 0.95 + pms * 0.05;
                if (locked2d)
                    buf2d->Unlock2D();
                else
                    buffer->Unlock();

                double maxGap = 0;
                for (size_t i = 1; i < arrivals.size(); ++i)
                    maxGap = std::max(maxGap, qpc::toMs(arrivals[i] - arrivals[i - 1]));
                std::lock_guard lock(m_mutex);
                m_stats.fps = static_cast<double>(arrivals.size());
                m_stats.maxGapMs = maxGap;
                m_stats.processMs = processMsAvg;
                ++m_stats.frames;
            }
        } catch (const HResultError& e) {
            if (!m_stop)
                setError(friendlyError(e.code()));
        } catch (const std::exception& e) {
            if (!m_stop)
                setError(e.what());
        } catch (...) {
            if (!m_stop)
                setError("unexpected camera error");
        }

        // Orderly teardown on this thread: flush the reader so no callback is
        // outstanding, then release it and shut the device down.
        if (reader && readPending) {
            if (SUCCEEDED(reader->Flush(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS))))
                WaitForSingleObject(callback->flushedEvent(), 1000);
        }
        reader.Reset();
        if (source)
            source->Shutdown();
        source.Reset();
        m_frames.clear();
        m_raw.clear();
        {
            std::lock_guard lock(m_mutex);
            m_stats.fps = 0;
        }
        arrivals.clear();

        // Wait before retrying (camera unplugged / busy); a stop request ends the wait quickly.
        for (int i = 0; i < 30 && !m_stop; ++i)
            Sleep(100);
        if (!m_stop)
            m_state = State::Starting;
    }
}

} // namespace luma::webcam
