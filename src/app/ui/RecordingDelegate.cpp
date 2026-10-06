#include "ui/RecordingDelegate.h"

#include "Icons.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "WinUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QPainter>

#include <cmath>

namespace luma::app {

void fillRecordingItem(QListWidgetItem* item, const RecordingEntry& e)
{
    QStringList meta;
    if (e.width > 0) {
        const int fps = static_cast<int>(std::lround(e.fps));
        meta << (fps > 0 ? QStringLiteral("%1×%2 · %3 FPS").arg(e.width).arg(e.height).arg(fps)
                         : QStringLiteral("%1×%2").arg(e.width).arg(e.height));
    }
    meta << formatBytes(static_cast<uint64_t>(e.sizeBytes));
    if (!e.exists)
        meta = QStringList{QStringLiteral("File missing")};
    item->setText(QFileInfo(e.path).fileName());
    item->setData(roles::Path, e.path);
    item->setData(roles::Meta, meta.join(QStringLiteral(" · ")));
    item->setData(roles::Duration, e.durationSeconds > 0 ? formatDuration(e.durationSeconds) : QString());
    item->setData(roles::Size, e.sizeBytes);
    item->setData(roles::Created, e.created);
    item->setData(roles::Missing, !e.exists);
    item->setData(roles::Seconds, e.durationSeconds);
    item->setToolTip(QStringLiteral("%1\n%2%3")
                         .arg(QDir::toNativeSeparators(e.path), e.created.toString(QStringLiteral("d MMM yyyy, HH:mm")),
                              e.imported ? QStringLiteral("\nImported") : QString()));
}

RecordingDelegate::RecordingDelegate(ThumbnailCache& thumbs, QSize tile, QObject* parent)
    : QStyledItemDelegate(parent), m_thumbs(thumbs), m_tile(tile)
{
}

void RecordingDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& index) const
{
    const Palette& pal = currentPalette();
    p->save();
    const QRect card = opt.rect.adjusted(4, 4, -4, -4);
    if (opt.state & QStyle::State_Selected)
        p->fillRect(card, pal.selected);
    else if (opt.state & QStyle::State_MouseOver)
        p->fillRect(card, pal.surfaceAlt);

    // Thumbnail (16:9) with a duration badge.
    const int thumbW = card.width() - 12;
    const QRect thumb(card.left() + 6, card.top() + 6, thumbW, thumbW * 9 / 16);
    p->fillRect(thumb, QColor(8, 9, 12));
    const bool missing = index.data(roles::Missing).toBool();
    const QString path = index.data(roles::Path).toString();
    QPixmap pm;
    if (!missing && !path.isEmpty())
        pm = m_thumbs.get(path, index.data(roles::Size).toLongLong(), index.data(roles::Created).toDateTime());
    if (!pm.isNull()) {
        const QSize s = pm.size().scaled(thumb.size(), Qt::KeepAspectRatio);
        const QRect dst(thumb.left() + (thumb.width() - s.width()) / 2, thumb.top() + (thumb.height() - s.height()) / 2,
                        s.width(), s.height());
        p->drawPixmap(dst, pm);
    } else {
        const int is = std::min(thumb.width(), thumb.height()) / 3;
        makeIcon(missing ? IconId::Warning : IconId::Play, pal.textDim)
            .paint(p, QRect(thumb.center().x() - is / 2, thumb.center().y() - is / 2, is, is));
    }
    if (opt.state & QStyle::State_Selected) {
        p->setPen(QPen(pal.accent, 2));
        p->drawRect(thumb.adjusted(1, 1, -1, -1));
    }
    const QString duration = index.data(roles::Duration).toString();
    QFont small = opt.font;
    small.setPointSizeF(8);
    if (!duration.isEmpty()) {
        p->setFont(small);
        const int w = p->fontMetrics().horizontalAdvance(duration) + 10;
        const QRect badge(thumb.right() - w - 4, thumb.bottom() - 20, w, 17);
        p->fillRect(badge, QColor(0, 0, 0, 190));
        p->setPen(Qt::white);
        p->drawText(badge, Qt::AlignCenter, duration);
    }

    // Name and details.
    QFont name = opt.font;
    name.setWeight(QFont::DemiBold);
    p->setFont(name);
    p->setPen(missing ? pal.textDim : pal.text);
    const QRect nameRect(card.left() + 6, thumb.bottom() + 6, card.width() - 12, p->fontMetrics().height());
    p->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                p->fontMetrics().elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, nameRect.width()));
    p->setFont(small);
    p->setPen(pal.textDim);
    const QRect metaRect(nameRect.left(), nameRect.bottom() + 2, nameRect.width(), p->fontMetrics().height());
    p->drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter,
                p->fontMetrics().elidedText(index.data(roles::Meta).toString(), Qt::ElideRight, metaRect.width()));
    p->restore();
}

} // namespace luma::app
