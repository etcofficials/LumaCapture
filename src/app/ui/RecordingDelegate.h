#pragma once

#include "History.h"

#include <QStyledItemDelegate>

class QListWidgetItem;

namespace luma::app {

class ThumbnailCache;

// Item data roles used by the recording lists.
namespace roles {
constexpr int Path = Qt::UserRole;
constexpr int Meta = Qt::UserRole + 1;     // "1920×1080 · 30 FPS · 1.2 GB"
constexpr int Duration = Qt::UserRole + 2; // "00:12:34"
constexpr int Size = Qt::UserRole + 3;     // qint64
constexpr int Created = Qt::UserRole + 4;  // QDateTime
constexpr int Missing = Qt::UserRole + 5;  // bool
constexpr int Seconds = Qt::UserRole + 6;  // double
} // namespace roles

// Fills a list item from a history entry.
void fillRecordingItem(QListWidgetItem* item, const RecordingEntry& e);

// Card with a cached thumbnail, duration badge, file name and one line of details.
class RecordingDelegate : public QStyledItemDelegate {
public:
    RecordingDelegate(ThumbnailCache& thumbs, QSize tile, QObject* parent = nullptr);
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return m_tile; }
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const override;

private:
    ThumbnailCache& m_thumbs;
    QSize m_tile;
};

} // namespace luma::app
