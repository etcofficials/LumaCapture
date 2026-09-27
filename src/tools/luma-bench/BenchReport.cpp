#include "BenchReport.h"

#include "util/Log.h"
#include "util/QpcClock.h"

#include <algorithm>
#include <fstream>
#include <numeric>

namespace luma::bench {
namespace {

double mb(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

struct Dist {
    size_t n = 0;
    double avg = 0, p50 = 0, p95 = 0, max = 0;
};

Dist distribution(std::vector<float> v)
{
    Dist d;
    d.n = v.size();
    if (v.empty())
        return d;
    std::sort(v.begin(), v.end());
    d.avg = std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
    d.p50 = v[v.size() / 2];
    d.p95 = v[std::min(v.size() - 1, static_cast<size_t>(v.size() * 0.95))];
    d.max = v.back();
    return d;
}

// Average of v[begin*n, end*n) for fractions of the sample count.
template <typename T, typename F>
double rangeAvg(const std::vector<T>& v, double begin, double end, F get)
{
    const auto b = static_cast<size_t>(begin * v.size());
    const auto e = std::min(v.size(), static_cast<size_t>(end * v.size()));
    if (e <= b)
        return 0;
    double sum = 0;
    for (size_t i = b; i < e; ++i)
        sum += get(v[i]);
    return sum / static_cast<double>(e - b);
}

struct MinAvgMax {
    double min = 0, avg = 0, max = 0;
};

template <typename F>
MinAvgMax minAvgMax(const std::vector<SecondSample>& s, size_t skip, F get)
{
    MinAvgMax r;
    size_t n = 0;
    for (size_t i = skip; i < s.size(); ++i) {
        const std::optional<double> v = get(s[i]);
        if (!v)
            continue;
        r.min = n ? std::min(r.min, *v) : *v;
        r.max = n ? std::max(r.max, *v) : *v;
        r.avg += *v;
        ++n;
    }
    if (n)
        r.avg /= static_cast<double>(n);
    return r;
}

std::string narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w)
        s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

// Everything the summary and the CSV report, computed once.
struct RunMetrics {
    int64_t ticks = 0, late = 0, captured = 0, unique = 0, repeated = 0, encoded = 0, keyframes = 0, dropped = 0;
    int64_t dropQueueFull = 0, dropPool = 0, dropNoImage = 0, dropDeviceLost = 0;
    int64_t desktopUpdates = 0, desktopCopies = 0, gpuStalls = 0, queueMax = 0, bytes = 0;
    double mediaSeconds = 0, gpuStallMs = 0, queueAvg = 0;
    MinAvgMax capFps, encFps, cpu, sysCpu, ws, priv, gpuBusiest, gpuOwn;
    bool gpuMeasured = false;
    Dist capLat, encLat;
    double encLatFirst = 0, encLatLast = 0, capLatFirst = 0, capLatLast = 0, queueFirst = 0, queueLast = 0;
    double encBusySeconds = 0, encCapacityFps = 0, realtimeFactor = 0;
    double bitrateMbps = 0, readbackMs = 0, cpuConvertMs = 0;
    uint64_t peakWs = 0;
    bool queueFilled = false, latencyIncreasing = false, fellBehind = false, cpuSaturated = false, gpuSaturated = false;
    std::string dropReason;
    std::string classification;
    std::string classificationReason;
};

RunMetrics computeMetrics(const BenchArgs& args, const session::RecordingSession& session, const BenchMonitor& monitor)
{
    const auto& c = session.counters();
    const auto& samples = monitor.samples();
    const double fps = args.fps;
    RunMetrics m;

    m.ticks = c.ticks;
    m.late = c.droppedLateTicks;
    m.captured = c.enqueued;
    m.unique = c.uniqueFrames;
    m.repeated = c.repeatedFrames;
    m.encoded = c.encoded;
    m.keyframes = c.keyframes;
    m.dropped = c.dropped();
    m.dropQueueFull = c.droppedQueueFull;
    m.dropPool = c.droppedPoolEmpty;
    m.dropNoImage = c.droppedNoImage;
    m.dropDeviceLost = c.droppedDeviceLost;
    m.desktopUpdates = c.desktopUpdates;
    m.desktopCopies = c.desktopCopies;
    m.gpuStalls = c.gpuStalls;
    m.gpuStallMs = c.gpuStallMicros / 1000.0;
    m.queueMax = c.queueDepthMax;
    m.queueAvg = c.queueDepthSamples ? static_cast<double>(c.queueDepthSum) / c.queueDepthSamples : 0;
    m.bytes = c.encodedBytes;
    m.mediaSeconds = static_cast<double>(m.ticks + m.late) / fps;
    m.bitrateMbps = m.mediaSeconds > 0 ? m.bytes * 8.0 / m.mediaSeconds / 1e6 : 0;
    if (m.unique > 0) {
        m.readbackMs = c.readbackCopyMicros / 1000.0 / m.unique;
        m.cpuConvertMs = c.cpuConvertMicros / 1000.0 / m.unique;
    }

    // The first second contains start-up (x264 lookahead fill); excluded from min/avg/max.
    const size_t skip = samples.size() > 2 ? 1 : 0;
    m.capFps = minAvgMax(samples, skip, [](const SecondSample& s) { return std::optional(s.captureFps); });
    m.encFps = minAvgMax(samples, skip, [](const SecondSample& s) { return std::optional(s.encodeFps); });
    m.cpu = minAvgMax(samples, skip, [](const SecondSample& s) { return std::optional(s.process.processCpuPercent); });
    m.sysCpu = minAvgMax(samples, skip, [](const SecondSample& s) { return std::optional(s.process.systemCpuPercent); });
    m.ws = minAvgMax(samples, 0, [](const SecondSample& s) { return std::optional(mb(s.process.workingSetBytes)); });
    m.priv = minAvgMax(samples, 0, [](const SecondSample& s) { return std::optional(mb(s.process.privateBytes)); });
    m.gpuMeasured = monitor.gpu().available();
    m.gpuBusiest = minAvgMax(samples, skip, [](const SecondSample& s) {
        return s.gpu ? std::optional(s.gpu->busiestEnginePercent) : std::nullopt;
    });
    m.gpuOwn = minAvgMax(samples, skip, [](const SecondSample& s) {
        return s.gpu ? std::optional(s.gpu->thisProcessPercent) : std::nullopt;
    });
    m.peakWs = samples.empty() ? 0 : samples.back().process.peakWorkingSetBytes;

    const auto& lat = session.latency();
    m.capLat = distribution(lat.captureMs);
    m.encLat = distribution(lat.encodeMs);
    // Trend: 5-35 % of the run vs the last 35 % (skips the start-up transient).
    const auto asD = [](float v) { return static_cast<double>(v); };
    m.encLatFirst = rangeAvg(lat.encodeMs, 0.05, 0.35, asD);
    m.encLatLast = rangeAvg(lat.encodeMs, 0.65, 1.0, asD);
    m.capLatFirst = rangeAvg(lat.captureMs, 0.05, 0.35, asD);
    m.capLatLast = rangeAvg(lat.captureMs, 0.65, 1.0, asD);
    const auto qd = [](const SecondSample& s) { return static_cast<double>(s.queueDepth); };
    m.queueFirst = rangeAvg(samples, 0.05, 0.35, qd);
    m.queueLast = rangeAvg(samples, 0.65, 1.0, qd);

    m.encBusySeconds = c.encoderBusyMicros / 1e6;
    m.encCapacityFps = m.encBusySeconds > 0 ? static_cast<double>(m.captured) / m.encBusySeconds : 0;
    m.realtimeFactor = m.encCapacityFps / fps;

    const double period = 1000.0 / fps;
    m.queueFilled = m.dropQueueFull > 0 || m.queueMax >= static_cast<int64_t>(args.queueCapacity);
    m.latencyIncreasing = (m.encLatLast > m.encLatFirst * 1.25 && m.encLatLast - m.encLatFirst > 2 * period) ||
                          (m.queueLast - m.queueFirst > 2.0);
    m.fellBehind = m.realtimeFactor < 1.0 || m.dropQueueFull > 0;
    m.cpuSaturated = m.sysCpu.avg >= 95.0;
    m.gpuSaturated = m.gpuMeasured && m.gpuBusiest.avg >= 95.0;

    struct Reason { int64_t n; const char* name; };
    const Reason reasons[] = {{m.dropQueueFull, "queue-full"}, {m.late, "capture-late"}, {m.dropPool, "no-buffer"},
                              {m.dropNoImage, "no-image"}, {m.dropDeviceLost, "device-lost"}};
    for (const auto& r : reasons)
        if (r.n > 0)
            m.dropReason += (m.dropReason.empty() ? "" : "+") + std::string(r.name);
    if (m.dropReason.empty())
        m.dropReason = "none";

    // Classification (documented in benchmark-report.txt):
    //  NOT REAL-TIME       encoder capacity < 1.0x, or >1 % of ticks dropped, or the worst 1 s window
    //                      encodes < 90 % of target fps, or latency/queue depth trends upward.
    //  REAL-TIME WITH DROPS otherwise, if any frame was dropped or the queue ever reached capacity.
    //  REAL-TIME STABLE     otherwise.
    const double totalTicks = static_cast<double>(m.ticks + m.late);
    const double dropRatio = totalTicks > 0 ? m.dropped / totalTicks : 0;
    std::vector<std::string> why;
    if (m.realtimeFactor < 1.0)
        why.push_back(std::format("encoder capacity {:.2f}x", m.realtimeFactor));
    if (dropRatio > 0.01)
        why.push_back(std::format("{:.1f}% frames dropped", dropRatio * 100));
    if (m.encFps.min < 0.9 * fps)
        why.push_back(std::format("min encode fps {:.1f}", m.encFps.min));
    if (m.latencyIncreasing)
        why.push_back("latency/queue rising");
    if (!why.empty()) {
        m.classification = "NOT REAL-TIME";
    } else if (m.dropped > 0 || m.queueFilled) {
        m.classification = "REAL-TIME WITH DROPS";
        why.push_back(std::format("{} dropped ({})", m.dropped, m.dropReason));
    } else {
        m.classification = "REAL-TIME STABLE";
    }
    for (const auto& w : why)
        m.classificationReason += (m.classificationReason.empty() ? "" : "; ") + w;
    return m;
}

std::string csvEscape(const std::string& s)
{
    if (s.find_first_of(",\"\n") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char ch : s)
        out += ch == '"' ? std::string("\"\"") : std::string(1, ch);
    return out + "\"";
}

void appendCsv(const std::filesystem::path& path, const BenchArgs& args, const RunMetrics& m, const RunResult& result,
               const std::filesystem::path& file)
{
    const bool exists = std::filesystem::exists(path);
    std::ofstream out(path, std::ios::app);
    if (!out) {
        log::error("Cannot write CSV {}", path.string());
        return;
    }
    if (!exists)
        out << "label,timestamp_file,resolution,fps,preset,crf,threads,conversion,duration_s,status,classification,"
               "classification_reason,capture_fps_avg,capture_fps_min,encode_fps_avg,encode_fps_min,ticks,late_ticks,"
               "frames_captured,frames_unique,frames_repeated,frames_encoded,frames_dropped,drop_reason,"
               "drop_queue_full,drop_no_buffer,drop_no_image,drop_device_lost,capture_latency_avg_ms,"
               "capture_latency_p95_ms,capture_latency_max_ms,encode_latency_avg_ms,encode_latency_p95_ms,"
               "encode_latency_max_ms,encode_latency_first_ms,encode_latency_last_ms,queue_depth_avg,queue_depth_max,"
               "queue_capacity,queue_filled,latency_increasing,gpu_stalls,gpu_stall_ms,encoder_realtime_factor,"
               "encoder_fell_behind,cpu_process_avg,cpu_process_max,cpu_system_avg,cpu_system_max,cpu_saturated,"
               "gpu_busiest_avg,gpu_busiest_max,gpu_process_avg,gpu_saturated,ram_ws_avg_mb,ram_ws_max_mb,"
               "ram_private_max_mb,bitrate_mbps,file_size_bytes,desktop_updates,desktop_copies,readback_ms_per_frame,"
               "cpu_convert_ms_per_frame,output_file\n";

    const auto b = [](bool v) { return v ? "yes" : "no"; };
    const auto gpuVal = [&](double v) { return m.gpuMeasured ? std::format("{:.1f}", v) : std::string("n/a"); };
    out << csvEscape(args.label) << ',' << csvEscape(file.stem().string()) << ','
        << std::format("{}x{}", args.width, args.height) << ',' << args.fps << ',' << args.preset << ',' << args.crf
        << ',' << args.threads << ',' << (args.cpuConvert ? "cpu" : "gpu") << ','
        << std::format("{:.2f}", result.wallSeconds) << ',' << (result.failed ? "failed" : "ok") << ','
        << m.classification << ',' << csvEscape(m.classificationReason) << ','
        << std::format("{:.2f},{:.2f},{:.2f},{:.2f}", m.capFps.avg, m.capFps.min, m.encFps.avg, m.encFps.min) << ','
        << m.ticks << ',' << m.late << ',' << m.captured << ',' << m.unique << ',' << m.repeated << ',' << m.encoded
        << ',' << m.dropped << ',' << m.dropReason << ',' << m.dropQueueFull << ',' << m.dropPool << ','
        << m.dropNoImage << ',' << m.dropDeviceLost << ','
        << std::format("{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f}", m.capLat.avg, m.capLat.p95,
                       m.capLat.max, m.encLat.avg, m.encLat.p95, m.encLat.max, m.encLatFirst, m.encLatLast)
        << ',' << std::format("{:.2f}", m.queueAvg) << ',' << m.queueMax << ',' << args.queueCapacity << ','
        << b(m.queueFilled) << ',' << b(m.latencyIncreasing) << ',' << m.gpuStalls << ','
        << std::format("{:.2f},{:.2f}", m.gpuStallMs, m.realtimeFactor) << ',' << b(m.fellBehind) << ','
        << std::format("{:.1f},{:.1f},{:.1f},{:.1f}", m.cpu.avg, m.cpu.max, m.sysCpu.avg, m.sysCpu.max) << ','
        << b(m.cpuSaturated) << ',' << gpuVal(m.gpuBusiest.avg) << ',' << gpuVal(m.gpuBusiest.max) << ','
        << gpuVal(m.gpuOwn.avg) << ',' << b(m.gpuSaturated) << ','
        << std::format("{:.1f},{:.1f},{:.1f},{:.3f}", m.ws.avg, m.ws.max, m.priv.max, m.bitrateMbps) << ','
        << result.fileBytes << ',' << m.desktopUpdates << ',' << m.desktopCopies << ','
        << std::format("{:.3f},{:.3f}", m.readbackMs, m.cpuConvertMs) << ',' << csvEscape(file.string()) << '\n';
}

} // namespace

BenchMonitor::BenchMonitor(const session::RecordingSession& session) : m_session(session)
{
    m_lastQpc = qpc::now();
    if (!m_gpu.available())
        log::warn("GPU utilisation unavailable: {}", m_gpu.unavailableReason());
}

const SecondSample& BenchMonitor::sample()
{
    const auto& c = m_session.counters();
    const int64_t now = qpc::now();
    const double dt = qpc::toSeconds(now - m_lastQpc);
    // A window shorter than 0.5 s (monitor woke late, then immediately again) gives
    // meaningless rates; merge it into the next window instead of recording it.
    if (dt < 0.5 && !m_samples.empty())
        return m_samples.back();
    const int64_t enq = c.enqueued.load(), enc = c.encoded.load(), bytes = c.encodedBytes.load();

    SecondSample s;
    s.elapsedSeconds = qpc::toSeconds(now - m_session.startQpc());
    s.captureFps = static_cast<double>(enq - m_lastEnqueued) / dt;
    s.encodeFps = static_cast<double>(enc - m_lastEncoded) / dt;
    s.bitrateMbps = static_cast<double>(bytes - m_lastBytes) * 8.0 / dt / 1e6;
    s.queueDepth = static_cast<int64_t>(m_session.queueDepth());
    s.process = m_process.sample();
    s.gpu = m_gpu.sample();

    m_lastQpc = now;
    m_lastEnqueued = enq;
    m_lastEncoded = enc;
    m_lastBytes = bytes;
    m_samples.push_back(s);
    return m_samples.back();
}

void BenchMonitor::printLive(const SecondSample& s) const
{
    const auto& c = m_session.counters();
    const std::string gpu = s.gpu ? std::format("{:4.1f}% (own {:4.1f}%)", s.gpu->busiestEnginePercent, s.gpu->thisProcessPercent)
                                  : std::string("n/a");
    log::info("[{:5.1f}s] FPS: {:5.1f} (enc {:5.1f}) | Captured: {} | Encoded: {} | Dropped: {} | Queue: {} | "
              "CPU: {:4.1f}% (system {:4.1f}%) | RAM: {:.1f} MB | GPU: {} | Bitrate: {:.2f} Mbps",
              s.elapsedSeconds, s.captureFps, s.encodeFps, c.enqueued.load(), c.encoded.load(), c.dropped(),
              s.queueDepth, s.process.processCpuPercent, s.process.systemCpuPercent, mb(s.process.workingSetBytes), gpu,
              s.bitrateMbps);
}

void printSummary(const BenchArgs& args, const session::RecordingSession& session, const BenchMonitor& monitor,
                  const RunResult& result, const std::filesystem::path& file)
{
    const RunMetrics m = computeMetrics(args, session, monitor);
    const auto& info = session.info();
    const auto yn = [](bool v) { return v ? "YES" : "no"; };

    log::info("================================ SUMMARY ================================");
    log::info("Run            : {} {}x{} @ {} fps, {}, preset {}, CRF {}, {} threads, keyint {}s",
              result.failed ? "FAILED" : "completed", args.width, args.height, args.fps,
              args.cpuConvert ? "CPU convert (libswscale)" : "GPU NV12 shader", args.preset, args.crf, args.threads,
              args.keyintSeconds);
    if (result.failed)
        log::error("Error          : {}", result.error);
    log::info("Classification : {}{}{}", m.classification, m.classificationReason.empty() ? "" : "  - ",
              m.classificationReason);
    log::info("Duration       : {:.2f} s wall, {:.2f} s of capture timeline; stop/drain took {:.0f} ms (x264 flush {:.0f} ms)",
              result.wallSeconds, m.mediaSeconds, session.stopDurationMs(), session.encoderFlushMs());
    log::info("Desktop        : {}x{} @ {:.0f} Hz on {} ({})", info.desktopWidth, info.desktopHeight, info.refreshHz,
              narrow(info.outputName), narrow(info.adapterName));

    log::info("-- Frames --");
    log::info("Capture ticks  : {} (+{} missed late)", m.ticks, m.late);
    log::info("Frames captured: {} ({} new desktop content, {} repeats of an unchanged screen)", m.captured, m.unique,
              m.repeated);
    log::info("Frames encoded : {} ({} keyframes)", m.encoded, m.keyframes);
    log::info("Frames dropped : {} (queue full {}, no buffer {}, late ticks {}, no image {}, device lost {})", m.dropped,
              m.dropQueueFull, m.dropPool, m.late, m.dropNoImage, m.dropDeviceLost);
    log::info("Desktop updates: {} ticks carried a new Desktop Duplication image; {} desktop GPU copies",
              m.desktopUpdates, m.desktopCopies);

    log::info("-- Frame rate (per 1 s window, first second excluded) --");
    log::info("Capture FPS    : avg {:.2f}  min {:.2f}  max {:.2f}", m.capFps.avg, m.capFps.min, m.capFps.max);
    log::info("Encode FPS     : avg {:.2f}  min {:.2f}  max {:.2f}", m.encFps.avg, m.encFps.min, m.encFps.max);

    log::info("-- Latency (ms) --");
    log::info("Capture latency: avg {:.2f}  p50 {:.2f}  p95 {:.2f}  max {:.2f}  (tick -> frame in encoder queue, n={})",
              m.capLat.avg, m.capLat.p50, m.capLat.p95, m.capLat.max, m.capLat.n);
    log::info("Encode latency : avg {:.2f}  p50 {:.2f}  p95 {:.2f}  max {:.2f}  (queue entry -> packet out, n={})",
              m.encLat.avg, m.encLat.p50, m.encLat.p95, m.encLat.max, m.encLat.n);
    log::info("Latency trend  : encode {:.1f} -> {:.1f} ms, capture {:.1f} -> {:.1f} ms, queue {:.2f} -> {:.2f} "
              "(5-35% vs 65-100% of run)  -> increasing: {}",
              m.encLatFirst, m.encLatLast, m.capLatFirst, m.capLatLast, m.queueFirst, m.queueLast, yn(m.latencyIncreasing));

    log::info("-- Queue / encoder --");
    log::info("Queue depth    : avg {:.2f}  max {} / capacity {}  -> queue filled: {}", m.queueAvg, m.queueMax,
              args.queueCapacity, yn(m.queueFilled));
    log::info("Encoder busy   : {:.2f} s -> capacity {:.1f} fps = {:.2f}x real time  -> fell behind: {}",
              m.encBusySeconds, m.encCapacityFps, m.realtimeFactor, yn(m.fellBehind));

    log::info("-- GPU pipeline --");
    log::info("GPU sync stalls: {} (total {:.2f} ms blocked in Map)", m.gpuStalls, m.gpuStallMs);
    if (m.unique > 0) {
        if (args.cpuConvert)
            log::info("CPU convert    : avg {:.2f} ms per new frame (libswscale BGRA->NV12)", m.cpuConvertMs);
        else
            log::info("Readback copy  : avg {:.2f} ms per new frame (staging -> frame buffer memcpy)", m.readbackMs);
    }
    if (m.gpuMeasured)
        log::info("GPU utilisation: busiest engine avg {:.1f}% max {:.1f}% | this process avg {:.1f}% max {:.1f}%  "
                  "(PDH GPU Engine counters) -> saturated: {}",
                  m.gpuBusiest.avg, m.gpuBusiest.max, m.gpuOwn.avg, m.gpuOwn.max, yn(m.gpuSaturated));
    else
        log::info("GPU utilisation: not measured ({})", monitor.gpu().unavailableReason());

    log::info("-- Process --");
    log::info("CPU (process)  : avg {:.1f}%  min {:.1f}%  max {:.1f}%  (100% = all 4 cores)", m.cpu.avg, m.cpu.min, m.cpu.max);
    log::info("CPU (system)   : avg {:.1f}%  max {:.1f}%  -> saturated: {}", m.sysCpu.avg, m.sysCpu.max, yn(m.cpuSaturated));
    log::info("Capture thread : {:.2f} s CPU time", session.captureThreadCpuSeconds());
    log::info("RAM            : working set avg {:.1f} MB, max {:.1f} MB, peak {:.1f} MB | private max {:.1f} MB | frame pool {} x = {:.1f} MB",
              m.ws.avg, m.ws.max, mb(m.peakWs), m.priv.max, info.framePoolSize, mb(info.framePoolBytes));

    log::info("-- Output --");
    log::info("File           : {}", file.string());
    log::info("File size      : {:.2f} MB ({} bytes)", mb(result.fileBytes), result.fileBytes);
    log::info("Video bitrate  : {:.2f} Mbps average over {:.2f} s of timeline", m.bitrateMbps, m.mediaSeconds);
    log::info("=========================================================================");

    if (!args.csvPath.empty())
        appendCsv(args.csvPath, args, m, result, file);
}

} // namespace luma::bench
