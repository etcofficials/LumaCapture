#include "OverlayRenderer.h"

#include <QDataStream>
#include <QIODevice>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cstring>

namespace luma::app {

const QImage& OverlayRenderer::image(const QString& path)
{
    auto it = m_images.find(path);
    if (it == m_images.end()) {
        QImage img(path);
        if (!img.isNull())
            img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        it = m_images.insert(path, img);
    }
    return it.value();
}

void OverlayRenderer::paintItem(QPainter& p, const OverlayItem& o, const QRectF& frame)
{
    const double scale = frame.height() / 1080.0;
    const QRectF r(frame.left() + o.x * frame.width(), frame.top() + o.y * frame.height(), o.w * frame.width(),
                   o.h * frame.height());
    p.save();
    p.setOpacity(std::clamp(o.opacity, 0.0, 1.0));
    switch (o.type) {
    case OverlayItem::Type::Text: {
        QFont font(o.fontFamily);
        font.setPixelSize(std::max(4, static_cast<int>(o.fontSize * scale)));
        font.setBold(o.bold);
        QPainterPath path;
        const QFontMetricsF fm(font);
        const QStringList lines = o.text.split('\n');
        double y = r.top() + fm.ascent();
        for (const QString& line : lines) {
            path.addText(QPointF(r.left(), y), font, line);
            y += fm.lineSpacing();
        }
        if (o.shadow)
            p.fillPath(path.translated(2 * scale + 1, 2 * scale + 1), QColor(0, 0, 0, 140));
        if (o.outline && o.outlineWidth > 0) {
            QPen pen(o.outlineColor, o.outlineWidth * 2 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.strokePath(path, pen);
        }
        p.fillPath(path, o.color);
        break;
    }
    case OverlayItem::Type::Image: {
        const QImage& img = image(o.imagePath);
        if (!img.isNull()) {
            // Fit inside the item rectangle, keeping the image aspect ratio.
            const QSizeF s = QSizeF(img.size()).scaled(r.size(), Qt::KeepAspectRatio);
            const QRectF dst(r.topLeft(), s);
            p.drawImage(dst, img);
        }
        break;
    }
    case OverlayItem::Type::Rectangle: {
        const double radius = o.cornerRadius * scale;
        QPainterPath path;
        path.addRoundedRect(r, radius, radius);
        p.fillPath(path, o.fillColor);
        if (o.borderWidth > 0)
            p.strokePath(path, QPen(o.borderColor, o.borderWidth * scale));
        break;
    }
    }
    p.restore();
}

void OverlayRenderer::paintWatermark(QPainter& p, const WatermarkSettings& w, const QRectF& frame)
{
    const double scale = frame.height() / 1080.0;
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(std::max(4, static_cast<int>(w.fontSize * scale)));
    font.setBold(true);
    const QFontMetricsF fm(font);
    const double tw = fm.horizontalAdvance(w.text), th = fm.height();
    const double m = 16 * scale;
    const double x = (w.corner == 1 || w.corner == 3) ? frame.right() - tw - m : frame.left() + m;
    const double y = (w.corner >= 2) ? frame.bottom() - m - th + fm.ascent() : frame.top() + m + fm.ascent();
    QPainterPath path;
    path.addText(QPointF(x, y), font, w.text);
    p.save();
    p.setOpacity(std::clamp(w.opacity, 0.0, 1.0));
    p.strokePath(path, QPen(QColor(0, 0, 0, 160), 3 * scale));
    p.fillPath(path, Qt::white);
    p.restore();
}

void OverlayRenderer::paint(QPainter& p, const AppSettings& s, const QRectF& frame)
{
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (const OverlayItem& o : s.overlays)
        if (o.enabled)
            paintItem(p, o, frame);
    if (s.watermark.enabled && !s.watermark.text.isEmpty())
        paintWatermark(p, s.watermark, frame);
}

std::shared_ptr<const gpu::OverlayImage> OverlayRenderer::render(const AppSettings& s, int outW, int outH)
{
    bool any = s.watermark.enabled && !s.watermark.text.isEmpty();
    for (const OverlayItem& o : s.overlays)
        any = any || o.enabled;
    if (!any || outW <= 0 || outH <= 0)
        return nullptr;

    // Signature of everything that affects the layer.
    QByteArray key;
    {
        QDataStream ds(&key, QIODevice::WriteOnly);
        ds << outW << outH << s.watermark.enabled << s.watermark.text << s.watermark.corner << s.watermark.opacity
           << s.watermark.fontSize;
        for (const OverlayItem& o : s.overlays)
            ds << static_cast<int>(o.type) << o.enabled << o.x << o.y << o.w << o.h << o.opacity << o.text
               << o.fontFamily << o.fontSize << o.bold << o.color << o.outline << o.outlineColor << o.outlineWidth
               << o.shadow << o.imagePath << o.fillColor << o.borderWidth << o.borderColor << o.cornerRadius;
    }
    if (key == m_lastKey && m_last)
        return m_last;

    QImage img(outW, outH, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        paint(p, s, QRectF(0, 0, outW, outH));
    }

    auto out = std::make_shared<gpu::OverlayImage>();
    out->width = static_cast<unsigned>(outW);
    out->height = static_cast<unsigned>(outH);
    out->pixels.resize(static_cast<size_t>(outW) * outH);
    // Format_ARGB32_Premultiplied in memory is B,G,R,A = DXGI_FORMAT_B8G8R8A8_UNORM, premultiplied.
    int minX = outW, minY = outH, maxX = -1, maxY = -1;
    for (int y = 0; y < outH; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(img.constScanLine(y));
        uint32_t* dst = out->pixels.data() + static_cast<size_t>(y) * outW;
        std::memcpy(dst, row, static_cast<size_t>(outW) * 4);
        for (int x = 0; x < outW; ++x) {
            if (row[x] >> 24) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
    }
    if (maxX < 0)
        return nullptr;
    out->boundsX = minX;
    out->boundsY = minY;
    out->boundsW = maxX - minX + 1;
    out->boundsH = maxY - minY + 1;
    m_lastKey = key;
    m_last = out;
    return out;
}

} // namespace luma::app
