#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

namespace luma::app {

// Small thumbnails of videos for the Library and the recent-recordings strip.
// One frame per file is decoded with FFmpeg on a single low-priority background
// thread and cached as a JPEG in the data folder; the UI thread never decodes or
// touches the disk. The in-memory cache is bounded (about 12 MB at most).
class ThumbnailCache : public QObject {
    Q_OBJECT
public:
    static constexpr int kWidth = 192;
    static constexpr int kHeight = 108;

    explicit ThumbnailCache(QObject* parent = nullptr);
    ~ThumbnailCache() override;

    // The cached thumbnail, or a null pixmap while it is being made (thumbnailReady follows).
    QPixmap get(const QString& path, qint64 size, const QDateTime& modified);
    void forget(const QString& path);

signals:
    void thumbnailReady(const QString& path);

private:
    void remember(const QString& path, const QPixmap& pm);

    QThreadPool m_pool;
    QHash<QString, QPixmap> m_cache;
    QStringList m_order; // insertion order for eviction
    QSet<QString> m_pending;
    QSet<QString> m_failed;
};

} // namespace luma::app
