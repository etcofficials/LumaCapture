#include "Icons.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace luma::app {
namespace {

void paint(QPainter& p, IconId id, const QColor& c, qreal s)
{
    const qreal u = s / 24.0; // design grid 24x24
    QPen pen(c, 2.0 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto R = [u](qreal x, qreal y, qreal w, qreal h) { return QRectF(x * u, y * u, w * u, h * u); };
    auto P = [u](qreal x, qreal y) { return QPointF(x * u, y * u); };

    switch (id) {
    case IconId::App: {
        // Brand mark (same design as resources/lumacapture.ico): blue tile, white peak, red record dot.
        QLinearGradient g(P(0, 0), P(24, 24));
        g.setColorAt(0, QColor(0x5A, 0xA2, 0xFF));
        g.setColorAt(1, QColor(0x2A, 0x6B, 0xF0));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(R(1, 1, 22, 22), 6 * u, 6 * u);
        p.setPen(QPen(Qt::white, 2.6 * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        QPainterPath peak;
        peak.moveTo(P(5.5, 17.5));
        peak.lineTo(P(11, 6.5));
        peak.lineTo(P(16.5, 17.5));
        p.drawPath(peak);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xE5, 0x48, 0x4D));
        p.drawEllipse(R(15, 13.5, 5.5, 5.5));
        break;
    }
    case IconId::Record:
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(R(5, 5, 14, 14));
        break;
    case IconId::Stop:
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(R(6, 6, 12, 12), 2 * u, 2 * u);
        break;
    case IconId::Pause:
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(R(6, 5, 4, 14), 1 * u, 1 * u);
        p.drawRoundedRect(R(14, 5, 4, 14), 1 * u, 1 * u);
        break;
    case IconId::Resume:
    case IconId::Play: {
        QPainterPath path;
        path.moveTo(P(8, 5));
        path.lineTo(P(19, 12));
        path.lineTo(P(8, 19));
        path.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawPath(path);
        break;
    }
    case IconId::Mic:
    case IconId::MicOff:
        p.drawRoundedRect(R(9, 3, 6, 11), 3 * u, 3 * u);
        p.drawArc(R(5.5, 6, 13, 12), 180 * 16, 180 * 16);
        p.drawLine(P(12, 18), P(12, 21));
        p.drawLine(P(8.5, 21), P(15.5, 21));
        if (id == IconId::MicOff) {
            p.setPen(QPen(QColor(0xE5, 0x48, 0x4D), 2.4 * u, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(P(4, 4), P(20, 20));
        }
        break;
    case IconId::Speaker: {
        QPainterPath path;
        path.moveTo(P(4, 9));
        path.lineTo(P(8, 9));
        path.lineTo(P(13, 5));
        path.lineTo(P(13, 19));
        path.lineTo(P(8, 15));
        path.lineTo(P(4, 15));
        path.closeSubpath();
        p.drawPath(path);
        p.drawArc(R(12, 8, 6, 8), -60 * 16, 120 * 16);
        p.drawArc(R(12, 5, 10, 14), -60 * 16, 120 * 16);
        break;
    }
    case IconId::Camera:
        p.drawRoundedRect(R(3, 7, 13, 10), 2 * u, 2 * u);
        {
            QPainterPath path;
            path.moveTo(P(16, 10.5));
            path.lineTo(P(21, 8));
            path.lineTo(P(21, 16));
            path.lineTo(P(16, 13.5));
            path.closeSubpath();
            p.drawPath(path);
        }
        break;
    case IconId::Display:
        p.drawRoundedRect(R(3, 4, 18, 12), 2 * u, 2 * u);
        p.drawLine(P(12, 16), P(12, 20));
        p.drawLine(P(8, 20), P(16, 20));
        break;
    case IconId::Window:
        p.drawRoundedRect(R(3, 4, 18, 16), 2 * u, 2 * u);
        p.drawLine(P(3, 8.5), P(21, 8.5));
        break;
    case IconId::Region: {
        QPen dash = pen;
        dash.setDashPattern({2, 2});
        p.setPen(dash);
        p.drawRect(R(4, 4, 16, 16));
        p.setPen(pen);
        p.drawLine(P(2, 6), P(2, 2));
        p.drawLine(P(2, 2), P(6, 2));
        p.drawLine(P(22, 18), P(22, 22));
        p.drawLine(P(22, 22), P(18, 22));
        break;
    }
    case IconId::Folder: {
        QPainterPath path;
        path.moveTo(P(3, 7));
        path.lineTo(P(3, 18));
        path.lineTo(P(21, 18));
        path.lineTo(P(21, 8));
        path.lineTo(P(11, 8));
        path.lineTo(P(9, 6));
        path.lineTo(P(4, 6));
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case IconId::Settings:
        p.drawEllipse(R(9, 9, 6, 6));
        for (int i = 0; i < 8; ++i) {
            p.save();
            p.translate(12 * u, 12 * u);
            p.rotate(i * 45);
            p.drawLine(QPointF(0, -6.5 * u), QPointF(0, -9 * u));
            p.restore();
        }
        p.drawEllipse(R(5.5, 5.5, 13, 13));
        break;
    case IconId::Screenshot:
        p.drawRoundedRect(R(3, 7, 18, 13), 2 * u, 2 * u);
        p.drawLine(P(8, 7), P(10, 4));
        p.drawLine(P(10, 4), P(14, 4));
        p.drawLine(P(14, 4), P(16, 7));
        p.drawEllipse(R(8.5, 10, 7, 7));
        break;
    case IconId::Trash:
        p.drawLine(P(4, 7), P(20, 7));
        p.drawLine(P(9, 7), P(10, 4));
        p.drawLine(P(10, 4), P(14, 4));
        p.drawLine(P(14, 4), P(15, 7));
        p.drawRoundedRect(R(6, 7, 12, 14), 2 * u, 2 * u);
        break;
    case IconId::Layout:
        p.drawRoundedRect(R(3, 4, 18, 16), 2 * u, 2 * u);
        p.setBrush(c);
        p.drawRoundedRect(R(13, 13, 6, 5), 1 * u, 1 * u);
        break;
    case IconId::Refresh:
        p.drawArc(R(5, 5, 14, 14), 30 * 16, 300 * 16);
        p.drawLine(P(19, 5), P(19, 9.5));
        p.drawLine(P(19, 9.5), P(14.5, 9.5));
        break;
    case IconId::Game:
        p.drawRoundedRect(R(2.5, 7, 19, 11), 5 * u, 5 * u);
        p.drawLine(P(7, 10.5), P(7, 14.5));
        p.drawLine(P(5, 12.5), P(9, 12.5));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(R(14.5, 9.8, 2.2, 2.2));
        p.drawEllipse(R(17, 12.5, 2.2, 2.2));
        break;
    case IconId::Library:
        p.drawRoundedRect(R(3, 5, 7, 6), 1.5 * u, 1.5 * u);
        p.drawRoundedRect(R(14, 5, 7, 6), 1.5 * u, 1.5 * u);
        p.drawRoundedRect(R(3, 14, 7, 6), 1.5 * u, 1.5 * u);
        p.drawRoundedRect(R(14, 14, 7, 6), 1.5 * u, 1.5 * u);
        break;
    case IconId::Info:
    case IconId::Help:
        p.drawEllipse(R(3, 3, 18, 18));
        if (id == IconId::Info) {
            p.drawLine(P(12, 11), P(12, 16.5));
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(R(10.9, 6.6, 2.2, 2.2));
        } else {
            p.drawArc(R(9, 6.5, 6, 6), 0, 200 * 16);
            p.drawArc(R(9, 6.5, 6, 6), 0, -90 * 16);
            p.drawLine(P(12, 12.5), P(12, 14));
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawEllipse(R(10.9, 15.8, 2.2, 2.2));
        }
        break;
    case IconId::Search:
        p.drawEllipse(R(4, 4, 11, 11));
        p.drawLine(P(13.5, 13.5), P(20, 20));
        break;
    case IconId::Import:
        p.drawLine(P(12, 3), P(12, 14));
        p.drawLine(P(7.5, 9.5), P(12, 14));
        p.drawLine(P(16.5, 9.5), P(12, 14));
        p.drawLine(P(4, 15), P(4, 20));
        p.drawLine(P(4, 20), P(20, 20));
        p.drawLine(P(20, 20), P(20, 15));
        break;
    case IconId::Edit: {
        QPainterPath path;
        path.moveTo(P(5, 19));
        path.lineTo(P(6, 15));
        path.lineTo(P(16, 5));
        path.lineTo(P(19, 8));
        path.lineTo(P(9, 18));
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case IconId::External:
        p.drawRoundedRect(R(4, 6, 14, 14), 2 * u, 2 * u);
        p.drawLine(P(12, 12), P(20, 4));
        p.drawLine(P(14, 4), P(20, 4));
        p.drawLine(P(20, 4), P(20, 10));
        break;
    case IconId::Chart:
        p.drawLine(P(4, 20), P(20, 20));
        p.drawLine(P(7, 20), P(7, 13));
        p.drawLine(P(12, 20), P(12, 6));
        p.drawLine(P(17, 20), P(17, 10));
        break;
    case IconId::Sparkle: {
        QPainterPath path;
        path.moveTo(P(12, 3));
        path.quadTo(P(13, 11), P(21, 12));
        path.quadTo(P(13, 13), P(12, 21));
        path.quadTo(P(11, 13), P(3, 12));
        path.quadTo(P(11, 11), P(12, 3));
        p.drawPath(path);
        break;
    }
    case IconId::Layers: {
        QPainterPath top;
        top.moveTo(P(12, 4));
        top.lineTo(P(21, 9));
        top.lineTo(P(12, 14));
        top.lineTo(P(3, 9));
        top.closeSubpath();
        p.drawPath(top);
        p.drawLine(P(3, 13.5), P(12, 18.5));
        p.drawLine(P(12, 18.5), P(21, 13.5));
        break;
    }
    case IconId::Cursor: {
        QPainterPath path;
        path.moveTo(P(6, 3));
        path.lineTo(P(6, 19));
        path.lineTo(P(10.5, 14.5));
        path.lineTo(P(13.5, 21));
        path.lineTo(P(16, 20));
        path.lineTo(P(13, 13.5));
        path.lineTo(P(19, 13.5));
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case IconId::Mail:
        p.drawRoundedRect(R(3, 5.5, 18, 13), 2 * u, 2 * u);
        p.drawLine(P(4, 7), P(12, 13));
        p.drawLine(P(12, 13), P(20, 7));
        break;
    case IconId::Link:
        p.drawRoundedRect(R(2.5, 8.5, 10, 7), 3.5 * u, 3.5 * u);
        p.drawRoundedRect(R(11.5, 8.5, 10, 7), 3.5 * u, 3.5 * u);
        break;
    case IconId::Warning: {
        QPainterPath path;
        path.moveTo(P(12, 3.5));
        path.lineTo(P(21.5, 20));
        path.lineTo(P(2.5, 20));
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(P(12, 9.5), P(12, 14));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(R(10.9, 16, 2.2, 2.2));
        break;
    }
    case IconId::Text:
        p.drawLine(P(5, 5), P(19, 5));
        p.drawLine(P(12, 5), P(12, 20));
        p.drawLine(P(9, 20), P(15, 20));
        break;
    case IconId::Image:
        p.drawRoundedRect(R(3, 4, 18, 16), 2 * u, 2 * u);
        p.drawEllipse(R(7, 7.5, 3.5, 3.5));
        p.drawLine(P(4, 18), P(10, 12.5));
        p.drawLine(P(10, 12.5), P(14, 16));
        p.drawLine(P(14, 16), P(16.5, 13.5));
        p.drawLine(P(16.5, 13.5), P(20, 17));
        break;
    case IconId::Close:
        p.drawLine(P(6, 6), P(18, 18));
        p.drawLine(P(18, 6), P(6, 18));
        break;
    }
}

QIcon buildIcon(IconId id, const QColor& color)
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 48, 64, 256}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        paint(p, id, color, size);
        p.end();
        icon.addPixmap(pm);
        QPixmap disabled(size, size);
        disabled.fill(Qt::transparent);
        QPainter pd(&disabled);
        pd.setRenderHint(QPainter::Antialiasing);
        QColor dc = color;
        dc.setAlphaF(0.35f);
        paint(pd, id, dc, size);
        pd.end();
        icon.addPixmap(disabled, QIcon::Disabled);
    }
    return icon;
}

} // namespace

QIcon makeIcon(IconId id, const QColor& color)
{
    // UI thread only (QPixmap). Never destroyed: pixmaps must not outlive QApplication's
    // teardown in a static destructor.
    static auto* cache = new QHash<quint64, QIcon>;
    const quint64 key = (static_cast<quint64>(id) << 32) | color.rgba();
    auto it = cache->constFind(key);
    if (it != cache->constEnd())
        return it.value();
    const QIcon icon = buildIcon(id, color);
    cache->insert(key, icon);
    return icon;
}

} // namespace luma::app
