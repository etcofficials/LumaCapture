#include "ThumbnailCache.h"

#include "AppPaths.h"
#include "encode/FfmpegUtil.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace luma::app {
namespace {

constexpr int kMaxCached = 150;

QImage toImage(const AVFrame* f)
{
    const double scale = std::min(static_cast<double>(ThumbnailCache::kWidth) / f->width,
                                  static_cast<double>(ThumbnailCache::kHeight) / f->height);
    const int w = std::max(2, static_cast<int>(f->width * scale)) & ~1;
    const int h = std::max(2, static_cast<int>(f->height * scale)) & ~1;
    ff::SwsPtr sws(sws_getContext(f->width, f->height, static_cast<AVPixelFormat>(f->format), w, h, AV_PIX_FMT_BGRA,
                                  SWS_AREA, nullptr, nullptr, nullptr));
    if (!sws)
        return {};
    QImage img(w, h, QImage::Format_RGB32);
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int stride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
    sws_scale(sws.get(), f->data, f->linesize, 0, f->height, dst, stride);
    return img;
}

// Decodes one representative frame (about 10 % into the file, at most 5 s in).
QImage decodeFrame(const QString& path)
{
    const QByteArray file = path.toUtf8();
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, file.constData(), nullptr, nullptr) < 0)
        return {};
    struct Close {
        AVFormatContext*& f;
        ~Close() { avformat_close_input(&f); }
    } close{fmt};
    if (avformat_find_stream_info(fmt, nullptr) < 0)
        return {};
    const AVCodec* codec = nullptr;
    const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (si < 0 || !codec)
        return {};
    ff::CodecContextPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx || avcodec_parameters_to_context(ctx.get(), fmt->streams[si]->codecpar) < 0)
        return {};
    ctx->thread_count = 1; // a background nicety; never compete with a recording
    if (avcodec_open2(ctx.get(), codec, nullptr) < 0)
        return {};
    if (fmt->duration > 0) {
        const int64_t target = std::min<int64_t>(fmt->duration / 10, int64_t{5} * AV_TIME_BASE);
        av_seek_frame(fmt, -1, target, AVSEEK_FLAG_BACKWARD);
    }
    ff::PacketPtr pkt(av_packet_alloc());
    ff::FramePtr frame(av_frame_alloc());
    if (!pkt || !frame)
        return {};
    int packets = 0;
    while (packets < 400 && av_read_frame(fmt, pkt.get()) >= 0) {
        if (pkt->stream_index == si) {
            ++packets;
            if (avcodec_send_packet(ctx.get(), pkt.get()) >= 0 && avcodec_receive_frame(ctx.get(), frame.get()) >= 0) {
                av_packet_unref(pkt.get());
                return toImage(frame.get());
            }
        }
        av_packet_unref(pkt.get());
    }
    avcodec_send_packet(ctx.get(), nullptr); // drain
    if (avcodec_receive_frame(ctx.get(), frame.get()) >= 0)
        return toImage(frame.get());
    return {};
}

QString cacheFile(const QString& path, qint64 size, const QDateTime& modified)
{
    const QByteArray key = QCryptographicHash::hash(
        (path.toLower() + QLatin1Char('|') + QString::number(size) + QLatin1Char('|') + modified.toString(Qt::ISODate)).toUtf8(),
        QCryptographicHash::Sha1);
    return AppPaths::thumbnailDir() + QLatin1Char('/') + QString::fromLatin1(key.toHex()) + QStringLiteral(".jpg");
}

} // namespace

ThumbnailCache::ThumbnailCache(QObject* parent) : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    m_pool.setThreadPriority(QThread::LowestPriority);
}

ThumbnailCache::~ThumbnailCache()
{
    m_pool.clear();          // drop queued jobs
    m_pool.waitForDone(3000); // a running decode finishes (or is abandoned at exit)
}

QPixmap ThumbnailCache::get(const QString& path, qint64 size, const QDateTime& modified)
{
    const auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd())
        return it.value();
    if (m_pending.contains(path) || m_failed.contains(path))
        return {};
    m_pending.insert(path);
    // The data folder path is resolved here (UI thread, cached) - the job only does file I/O.
    const QString cached = cacheFile(path, size, modified);
    QPointer<ThumbnailCache> self(this);
    (void)QtConcurrent::run(&m_pool, [self, path, cached] {
        QImage img;
        if (!img.load(cached)) {
            img = decodeFrame(path);
            if (!img.isNull())
                img.save(cached, "JPG", 85);
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, path, img] {
            if (!self)
                return;
            self->m_pending.remove(path);
            if (img.isNull()) {
                self->m_failed.insert(path);
                return;
            }
            self->remember(path, QPixmap::fromImage(img));
            emit self->thumbnailReady(path);
        });
    });
    return {};
}

void ThumbnailCache::remember(const QString& path, const QPixmap& pm)
{
    m_cache.insert(path, pm);
    m_order.append(path);
    while (m_order.size() > kMaxCached)
        m_cache.remove(m_order.takeFirst());
}

void ThumbnailCache::forget(const QString& path)
{
    m_cache.remove(path);
    m_order.removeAll(path);
    m_failed.remove(path);
}

} // namespace luma::app
