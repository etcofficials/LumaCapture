#pragma once

#include "AppSettings.h"
#include "gpu/CompositionSettings.h"

#include <QByteArray>
#include <QHash>
#include <QImage>

#include <memory>

class QPainter;

namespace luma::app {

// Renders text / image / rectangle overlays and the watermark into one
// premultiplied BGRA layer at output resolution. Images are cached by path.
class OverlayRenderer {
public:
    // Returns null when no overlay is enabled (the compositor then skips the layer entirely).
    std::shared_ptr<const gpu::OverlayImage> render(const AppSettings& s, int outW, int outH);

    // Paints the same content with QPainter (used by the layout editor preview).
    void paint(QPainter& p, const AppSettings& s, const QRectF& frame);

private:
    const QImage& image(const QString& path);
    void paintItem(QPainter& p, const OverlayItem& item, const QRectF& frame);
    void paintWatermark(QPainter& p, const WatermarkSettings& w, const QRectF& frame);

    QHash<QString, QImage> m_images;
    // The layer is re-rendered only when its content or size changes.
    QByteArray m_lastKey;
    std::shared_ptr<const gpu::OverlayImage> m_last;
};

} // namespace luma::app
