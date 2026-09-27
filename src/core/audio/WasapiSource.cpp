#include "audio/WasapiSource.h"

#include "encode/FfmpegUtil.h"
#include "util/ComScope.h"
#include "util/HResult.h"
#include "util/Log.h"
#include "util/QpcClock.h"
#include "util/StringUtil.h"

#include <windows.h>
#include <avrt.h>
#include <audioclient.h>

#include <mmdeviceapi.h>
#include <initguid.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <vector>

namespace luma::audio {

using Microsoft::WRL::ComPtr;

// Signals the capture thread when the Windows default device changes, so a
// source configured for "default device" follows it.
class DefaultDeviceWatcher final : public IMMNotificationClient {
public:
    DefaultDeviceWatcher(std::atomic<bool>& flag, EDataFlow flow) : m_flag(flag), m_flow(flow) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG r = InterlockedDecrement(&m_ref);
        if (r == 0)
            delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
    {
        if (flow == m_flow && role == eConsole)
            m_flag = true;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<bool>& m_flag;
    EDataFlow m_flow;
    LONG m_ref = 1;
};

namespace {

constexpr int kOutRate = 48000;

struct SwrDeleter {
    void operator()(SwrContext* s) const { swr_free(&s); }
};
using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;

struct DeviceFormat {
    AVSampleFormat fmt = AV_SAMPLE_FMT_NONE;
    int rate = 0;
    int channels = 0;
    uint64_t mask = 0;
    int bytesPerFrame = 0;
};

DeviceFormat describe(const WAVEFORMATEX* wf)
{
    DeviceFormat d;
    d.rate = static_cast<int>(wf->nSamplesPerSec);
    d.channels = wf->nChannels;
    d.bytesPerFrame = wf->nBlockAlign;
    bool isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool isPcm = wf->wFormatTag == WAVE_FORMAT_PCM;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        isFloat = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        isPcm = ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
        d.mask = ext->dwChannelMask;
    }
    if (isFloat && wf->wBitsPerSample == 32)
        d.fmt = AV_SAMPLE_FMT_FLT;
    else if (isPcm && wf->wBitsPerSample == 16)
        d.fmt = AV_SAMPLE_FMT_S16;
    else if (isPcm && wf->wBitsPerSample == 32)
        d.fmt = AV_SAMPLE_FMT_S32;
    return d;
}

std::wstring deviceFriendlyName(IMMDevice* dev)
{
    ComPtr<IPropertyStore> props;
    std::wstring name;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
            name = v.pwszVal;
        PropVariantClear(&v);
    }
    return name;
}

} // namespace

WasapiSource::WasapiSource(AudioSourceConfig config, AudioRing* ring, int64_t startQpc, MicProcessor* processor)
    : m_config(std::move(config)), m_ring(ring), m_startQpc(startQpc), m_processor(processor)
{
}

WasapiSource::~WasapiSource()
{
    stop();
}

void WasapiSource::start()
{
    stop();
    m_stop = false;
    m_thread = std::thread(&WasapiSource::run, this);
}

void WasapiSource::stop()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
    m_running = false;
}

std::string WasapiSource::status() const
{
    std::lock_guard lock(m_mutex);
    return m_status;
}

std::wstring WasapiSource::deviceName() const
{
    std::lock_guard lock(m_mutex);
    return m_deviceName;
}

void WasapiSource::setStatus(const std::string& s)
{
    std::lock_guard lock(m_mutex);
    if (s != m_status && !s.empty())
        log::warn("Audio ({}): {}", m_config.loopback ? "system" : "microphone", s);
    m_status = s;
}

void WasapiSource::run()
{
    SetThreadDescription(GetCurrentThread(), m_config.loopback ? L"luma-audio-system" : L"luma-audio-mic");
    ComScope com;
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        setStatus("Audio device enumerator unavailable");
        return;
    }
    const EDataFlow flow = m_config.loopback ? eRender : eCapture;
    DefaultDeviceWatcher* watcher = nullptr;
    if (m_config.deviceId.empty()) {
        watcher = new DefaultDeviceWatcher(m_reopen, flow);
        enumerator->RegisterEndpointNotificationCallback(watcher);
    }

    const int64_t freq = qpc::frequency();
    const int delayFrames = m_config.delayMs * kOutRate / 1000;
    std::vector<float> converted, processed;

    while (!m_stop) {
        m_reopen = false;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        WAVEFORMATEX* mix = nullptr;
        SwrPtr swr;
        try {
            if (m_config.deviceId.empty())
                check(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device), "GetDefaultAudioEndpoint");
            else
                check(enumerator->GetDevice(m_config.deviceId.c_str(), &device), "GetDevice");
            {
                std::lock_guard lock(m_mutex);
                m_deviceName = deviceFriendlyName(device.Get());
            }
            check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client), "Activate(IAudioClient)");
            check(client->GetMixFormat(&mix), "GetMixFormat");
            const DeviceFormat df = describe(mix);
            if (df.fmt == AV_SAMPLE_FMT_NONE)
                throw std::runtime_error("unsupported device sample format");
            const REFERENCE_TIME bufferDuration = 2000000; // 200 ms
            check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, m_config.loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0,
                                     bufferDuration, 0, mix, nullptr),
                  "IAudioClient::Initialize");
            check(client->GetService(IID_PPV_ARGS(&capture)), "GetService(IAudioCaptureClient)");

            AVChannelLayout inLayout{}, outLayout{};
            if (df.mask && av_channel_layout_from_mask(&inLayout, df.mask) == 0 && inLayout.nb_channels == df.channels) {
            } else {
                av_channel_layout_uninit(&inLayout);
                av_channel_layout_default(&inLayout, df.channels);
            }
            av_channel_layout_default(&outLayout, 2);
            SwrContext* raw = nullptr;
            ff::check(swr_alloc_set_opts2(&raw, &outLayout, AV_SAMPLE_FMT_FLT, kOutRate, &inLayout, df.fmt, df.rate, 0,
                                          nullptr),
                      "swr_alloc_set_opts2");
            swr.reset(raw);
            if (df.channels == 1)
                av_opt_set_double(raw, "center_mix_level", 1.0, 0); // mono mic -> full level in both channels
            ff::check(swr_init(raw), "swr_init");
            av_channel_layout_uninit(&inLayout);

            check(client->Start(), "IAudioClient::Start");
            m_running = true;
            setStatus("");
            log::info("Audio {} started: {} ({} Hz, {} ch)", m_config.loopback ? "system" : "microphone",
                      toUtf8(deviceName()), df.rate, df.channels);

            bool posValid = false;
            int64_t writePos = 0;
            while (!m_stop && !m_reopen) {
                Sleep(10);
                UINT32 packet = 0;
                check(capture->GetNextPacketSize(&packet), "GetNextPacketSize");
                while (packet > 0) {
                    BYTE* data = nullptr;
                    UINT32 frames = 0;
                    DWORD flags = 0;
                    UINT64 devPos = 0, qpcPos = 0;
                    check(capture->GetBuffer(&data, &frames, &flags, &devPos, &qpcPos), "GetBuffer");

                    const int maxOut = swr_get_out_samples(raw, static_cast<int>(frames)) + 64;
                    converted.resize(static_cast<size_t>(maxOut) * 2);
                    uint8_t* outPlanes[1] = {reinterpret_cast<uint8_t*>(converted.data())};
                    int produced;
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                        produced = swr_convert(raw, outPlanes, maxOut, nullptr, 0);
                        const int64_t silentOut = static_cast<int64_t>(frames) * kOutRate / df.rate;
                        converted.assign(static_cast<size_t>(std::max<int64_t>(produced, silentOut)) * 2, 0.f);
                        produced = static_cast<int>(converted.size() / 2);
                    } else {
                        const uint8_t* inPlanes[1] = {data};
                        produced = swr_convert(raw, outPlanes, maxOut, inPlanes, static_cast<int>(frames));
                    }
                    check(capture->ReleaseBuffer(frames), "ReleaseBuffer");
                    if (produced < 0)
                        produced = 0;

                    // Peak for meters (before processing for the mic, it shows the raw input level).
                    float peak = 0.f;
                    for (int i = 0; i < produced * 2; ++i)
                        peak = std::max(peak, std::abs(converted[static_cast<size_t>(i)]));
                    float prev = m_peak.load();
                    while (peak > prev && !m_peak.compare_exchange_weak(prev, peak)) {
                    }

                    if (m_ring && produced > 0) {
                        // Timeline position of this packet (qpcPos is in 100 ns QPC units).
                        const int64_t qpcTicks = static_cast<int64_t>(
                            static_cast<double>(qpcPos) * static_cast<double>(freq) / 1e7);
                        const int64_t expected = (qpcTicks - m_startQpc) * kOutRate / freq + delayFrames;
                        if (!posValid || (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)) {
                            writePos = expected;
                            posValid = true;
                        } else {
                            const int64_t drift = expected - writePos;
                            if (std::llabs(drift) > kOutRate * 8 / 100) {
                                writePos = expected; // gap (e.g. loopback idle) or jump: resync
                                ++m_resyncs;
                                swr_set_compensation(raw, 0, 0);
                            } else if (std::llabs(drift) > kOutRate / 200) {
                                // > 5 ms: stretch/shrink gently over the next second.
                                swr_set_compensation(raw, static_cast<int>(std::clamp<int64_t>(drift, -480, 480)),
                                                     kOutRate);
                            }
                        }
                        const float* out = converted.data();
                        size_t outFrames = static_cast<size_t>(produced);
                        int64_t pos = writePos;
                        if (m_processor) {
                            int64_t latency = 0;
                            outFrames = m_processor->process(converted.data(), outFrames, processed, latency);
                            out = processed.data();
                            pos = writePos - latency;
                        }
                        if (outFrames > 0)
                            m_ring->write(pos, out, outFrames);
                        writePos += produced;
                    }
                    check(capture->GetNextPacketSize(&packet), "GetNextPacketSize");
                }
            }
            client->Stop();
        } catch (const HResultError& e) {
            if (e.code() == AUDCLNT_E_DEVICE_INVALIDATED)
                setStatus("Audio device disconnected; waiting for it (or the new default device)");
            else
                setStatus(e.what());
        } catch (const std::exception& e) {
            setStatus(e.what());
        } catch (...) {
            setStatus("unexpected audio device error");
        }
        m_running = false;
        if (mix)
            CoTaskMemFree(mix);
        // Retry after a pause unless stopping (device unplugged, exclusive use, ...).
        for (int i = 0; i < 10 && !m_stop && !m_reopen; ++i)
            Sleep(100);
    }

    if (watcher) {
        enumerator->UnregisterEndpointNotificationCallback(watcher);
        watcher->Release();
    }
    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
}

} // namespace luma::audio
