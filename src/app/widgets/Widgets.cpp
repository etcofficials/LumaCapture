#include "widgets/Widgets.h"

#include "Theme.h"

#include <QEnterEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegion>
#include <QScreen>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace luma::app {

// -------------------------------------------------------------- ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(36, 20);
}

void ToggleSwitch::setChecked(bool on)
{
    if (on == m_checked)
        return;
    m_checked = on;
    update();
    emit toggled(on);
}

void ToggleSwitch::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette& pal = currentPalette();
    const QRectF track = QRectF(rect()).adjusted(1, 2, -1, -2);
    QColor trackColor = m_checked ? pal.accent : pal.border;
    if (!isEnabled())
        trackColor.setAlphaF(0.4f);
    p.setPen(hasFocus() ? QPen(pal.text, 1.2) : Qt::NoPen);
    p.setBrush(trackColor);
    p.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const double d = track.height() - 4;
    const double x = m_checked ? track.right() - d - 2 : track.left() + 2;
    p.setPen(Qt::NoPen);
    p.setBrush(isEnabled() ? QColor(Qt::white) : pal.textDim);
    p.drawEllipse(QRectF(x, track.top() + 2, d, d));
}

void ToggleSwitch::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()))
        setChecked(!m_checked);
}

void ToggleSwitch::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return)
        setChecked(!m_checked);
    else
        QWidget::keyPressEvent(e);
}

// ------------------------------------------------------------------ StatTile

StatTile::StatTile(const QString& caption, QWidget* parent) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(1);
    m_caption = new QLabel(caption, this);
    m_caption->setObjectName("Hint");
    m_value = new QLabel(QStringLiteral("-"), this);
    m_value->setObjectName("Value");
    v->addWidget(m_caption);
    v->addWidget(m_value);
}

void StatTile::setValue(const QString& value, bool warn)
{
    m_value->setText(value);
    m_value->setObjectName(warn ? "Warn" : "Value");
    m_value->style()->unpolish(m_value);
    m_value->style()->polish(m_value);
}

// -------------------------------------------------------------------- Banner

Banner::Banner(QWidget* parent) : QFrame(parent)
{
    setObjectName("Banner");
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(12, 6, 6, 6);
    h->setSpacing(10);
    m_icon = new QLabel(this);
    m_icon->setFixedWidth(10);
    m_text = new QLabel(this);
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_action = new QPushButton(this);
    m_action->setObjectName("Flat");
    auto* close = new QToolButton(this);
    close->setIcon(makeIcon(IconId::Close, currentPalette().textDim));
    close->setToolTip(QStringLiteral("Dismiss"));
    close->setAccessibleName(QStringLiteral("Dismiss notification"));
    h->addWidget(m_icon);
    h->addWidget(m_text, 1);
    h->addWidget(m_action);
    h->addWidget(close);
    connect(close, &QToolButton::clicked, this, &QWidget::hide);
    connect(m_action, &QPushButton::clicked, this, &Banner::actionClicked);
    m_hide.setSingleShot(true);
    connect(&m_hide, &QTimer::timeout, this, &QWidget::hide);
    hide();
}

void Banner::showMessage(Kind kind, const QString& text, const QString& actionText, int autoHideMs)
{
    static const char* kinds[] = {"info", "success", "warning", "error"};
    setProperty("kind", kinds[static_cast<int>(kind)]);
    style()->unpolish(this);
    style()->polish(this);
    m_text->setText(text);
    m_action->setText(actionText);
    m_action->setVisible(!actionText.isEmpty());
    const Palette& pal = currentPalette();
    const QColor c = kind == Kind::Error ? pal.record : kind == Kind::Warning ? pal.warning
                   : kind == Kind::Success ? pal.success : pal.accent;
    QPixmap dot(10, 10);
    dot.fill(Qt::transparent);
    QPainter p(&dot);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(1, 1, 8, 8));
    p.end();
    m_icon->setPixmap(dot);
    show();
    if (autoHideMs > 0)
        m_hide.start(autoHideMs);
    else
        m_hide.stop();
}

// ---------------------------------------------------------------- LevelMeter

LevelMeter::LevelMeter(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(60, 8);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAccessibleName(QStringLiteral("Audio level"));
    m_holdTimer.start();
}

void LevelMeter::setLevel(float linearPeak)
{
    const float db = linearPeak > 1e-5f ? 20.f * std::log10(linearPeak) : -60.f;
    const float old = m_db, oldHold = m_hold;
    m_db = db > m_db ? db : std::max(db, m_db - 2.f); // fast attack, ~50 dB/s decay at 25 Hz
    if (m_db >= m_hold || m_holdTimer.elapsed() > 1200) {
        m_hold = m_db;
        m_holdTimer.restart();
    }
    if (std::abs(m_db - old) > 0.3f || m_hold != oldHold) // repaint only when something visibly changes
        update();
}

void LevelMeter::reset()
{
    m_db = m_hold = -60.f;
    update();
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Palette& pal = currentPalette();
    constexpr int kSegments = 24;
    const int gap = 2;
    const double segW = (width() - gap * (kSegments - 1)) / static_cast<double>(kSegments);
    auto frac = [](float db) { return std::clamp((db + 60.f) / 60.f, 0.f, 1.f); };
    const int lit = static_cast<int>(std::ceil(frac(m_db) * kSegments - 0.01));
    const int hold = m_hold > -59.f ? static_cast<int>(std::ceil(frac(m_hold) * kSegments - 0.01)) - 1 : -1;
    for (int i = 0; i < kSegments; ++i) {
        const QRectF seg(i * (segW + gap), 0, segW, height());
        QColor c = i >= kSegments - 2 ? pal.record : (i >= kSegments - 6 ? pal.warning : pal.success);
        if (i >= lit && i != hold)
            c = pal.surfaceAlt;
        p.fillRect(seg, c);
    }
}

// ------------------------------------------------------------- WebcamPreview

WebcamPreview::WebcamPreview(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(160, 90);
    setAccessibleName(QStringLiteral("Webcam preview"));
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void WebcamPreview::setFrame(const QImage& frame)
{
    m_frame = frame;
    m_message.clear();
    update();
}

void WebcamPreview::setMessage(const QString& text)
{
    m_message = text;
    if (!text.isEmpty())
        m_frame = QImage();
    update();
}

void WebcamPreview::setMirror(bool on)
{
    m_mirror = on;
    update();
}

void WebcamPreview::setOriginal(const QImage& frame)
{
    m_original = frame;
    if (m_compare)
        update();
}

void WebcamPreview::setCompare(bool on)
{
    m_compare = on;
    if (!on)
        m_original = QImage();
    update();
}

void WebcamPreview::drawImage(QPainter& p, const QImage& img, const QRectF& area)
{
    const QSizeF s = QSizeF(img.size()).scaled(area.size(), Qt::KeepAspectRatio);
    const QRectF dst(area.left() + (area.width() - s.width()) / 2, area.top() + (area.height() - s.height()) / 2,
                     s.width(), s.height());
    if (m_mirror) {
        p.save();
        p.translate(dst.center());
        p.scale(-1, 1);
        p.translate(-dst.center());
        p.drawImage(dst, img);
        p.restore();
    } else {
        p.drawImage(dst, img);
    }
}

void WebcamPreview::paintEvent(QPaintEvent*)
{
    // No rounded clip path or antialiasing here: this repaints at the camera rate, and
    // an antialiased clip forces Qt's slowest software path (v1 did that).
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const Palette& pal = currentPalette();
    p.fillRect(rect(), QColor(8, 9, 12));
    if (m_frame.isNull()) {
        p.setPen(pal.textDim);
        p.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap,
                   m_message.isEmpty() ? QStringLiteral("No camera image") : m_message);
        return;
    }
    if (m_compare && !m_original.isNull()) {
        const QRectF l(0, 0, width() / 2.0 - 1, height()), r(width() / 2.0 + 1, 0, width() / 2.0 - 1, height());
        drawImage(p, m_original, l);
        drawImage(p, m_frame, r);
        p.fillRect(QRectF(width() / 2.0 - 1, 0, 2, height()), pal.accent);
        QFont f = font();
        f.setPointSizeF(8);
        f.setBold(true);
        p.setFont(f);
        auto tag = [&](const QRectF& area, const QString& text) {
            const QRectF t(area.left() + 8, area.top() + 8, 64, 20);
            p.fillRect(t, QColor(0, 0, 0, 170));
            p.setPen(Qt::white);
            p.drawText(t, Qt::AlignCenter, text);
        };
        tag(l, QStringLiteral("BEFORE"));
        tag(r, QStringLiteral("AFTER"));
    } else {
        drawImage(p, m_frame, QRectF(rect()));
    }
}

// --------------------------------------------------------------- PreviewView

PreviewView::PreviewView(QWidget* parent) : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("Recording preview"));
}

void PreviewView::setFrame(const QImage& frame)
{
    m_frame = frame;
    update();
}

void PreviewView::clearFrame()
{
    m_frame = QImage();
    update();
}

void PreviewView::setMessage(const QString& text)
{
    if (text == m_message)
        return;
    m_message = text;
    update();
}

void PreviewView::setInfo(const QString& text)
{
    if (text == m_info)
        return;
    m_info = text;
    update();
}

void PreviewView::setRecording(bool recording, bool paused)
{
    m_recording = recording;
    m_paused = paused;
    update();
}

QSize PreviewView::pixelSize() const
{
    return (QSizeF(size()) * devicePixelRatioF()).toSize();
}

void PreviewView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    emit resized();
}

void PreviewView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Palette& pal = currentPalette();
    p.fillRect(rect(), QColor(6, 7, 10));
    QRectF imageRect;
    if (!m_frame.isNull()) {
        const QSizeF logical = QSizeF(m_frame.size()) / devicePixelRatioF();
        const QSizeF s = logical.scaled(QSizeF(size()), Qt::KeepAspectRatio);
        imageRect = QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
        // Frames are produced at about the widget's pixel size: smooth scaling only if they differ a lot.
        if (std::abs(s.width() - logical.width()) > logical.width() * 0.1)
            p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(imageRect, m_frame);
    }
    QFont small = font();
    small.setPointSizeF(8.5);
    p.setFont(small);
    const QFontMetrics fm(small);
    if (!m_message.isEmpty()) {
        if (!m_frame.isNull())
            p.fillRect(rect(), QColor(0, 0, 0, 150));
        p.setPen(pal.textDim);
        QFont f = font();
        f.setPointSizeF(10);
        p.setFont(f);
        p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        p.setFont(small);
    }
    if (!m_info.isEmpty()) {
        const int w = fm.horizontalAdvance(m_info) + 18;
        const QRect chip(width() - w - 10, 10, w, fm.height() + 8);
        p.fillRect(chip, QColor(10, 12, 16, 210));
        p.setPen(QColor(0xE6, 0xE9, 0xEF));
        p.drawText(chip, Qt::AlignCenter, m_info);
    }
    if (m_recording) {
        const QString text = m_paused ? QStringLiteral("PAUSED") : QStringLiteral("REC");
        const QRect badge(10, 10, fm.horizontalAdvance(text) + 28, fm.height() + 8);
        p.fillRect(badge, QColor(10, 12, 16, 210));
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(m_paused ? pal.warning : pal.record);
        p.drawEllipse(QPointF(badge.left() + 11, badge.center().y() + 0.5), 4, 4);
        p.setPen(Qt::white);
        p.drawText(badge.adjusted(20, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }
}

// ---------------------------------------------------------------- ModeButton

ModeButton::ModeButton(IconId icon, const QString& title, const QString& subtitle, QWidget* parent)
    : QAbstractButton(parent), m_icon(icon), m_title(title), m_subtitle(subtitle)
{
    setCheckable(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(title);
    setAccessibleDescription(subtitle);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize ModeButton::sizeHint() const
{
    return {220, 46};
}

void ModeButton::enterEvent(QEnterEvent* e)
{
    QAbstractButton::enterEvent(e);
    update();
}

void ModeButton::leaveEvent(QEvent* e)
{
    QAbstractButton::leaveEvent(e);
    update();
}

void ModeButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette& pal = currentPalette();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (isChecked()) {
        p.setPen(QPen(pal.accent, 1));
        p.setBrush(pal.selected);
        p.drawRoundedRect(r, 6, 6);
    } else if (underMouse() || hasFocus()) {
        p.setPen(hasFocus() ? QPen(pal.accent, 1) : Qt::NoPen);
        p.setBrush(pal.surfaceAlt);
        p.drawRoundedRect(r, 6, 6);
    }
    const QColor iconColor = isEnabled() ? (isChecked() ? pal.accent : pal.text) : pal.textDim;
    makeIcon(m_icon, iconColor).paint(&p, QRect(12, (height() - 22) / 2, 22, 22));
    QFont f = font();
    f.setPointSizeF(9.5);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(isEnabled() ? pal.text : pal.textDim);
    const QRect textRect(46, 5, width() - 52, height() / 2 - 3);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignBottom, m_title);
    f.setPointSizeF(8);
    f.setWeight(QFont::Normal);
    p.setFont(f);
    p.setPen(pal.textDim);
    p.drawText(QRect(46, height() / 2 + 1, width() - 52, height() / 2 - 5), Qt::AlignLeft | Qt::AlignTop,
               p.fontMetrics().elidedText(m_subtitle, Qt::ElideRight, width() - 52));
}

// ----------------------------------------------------------- TransportButton

TransportButton::TransportButton(const QString& caption, int diameter, QWidget* parent)
    : QAbstractButton(parent), m_diameter(diameter), m_caption(caption)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(caption);
    m_fill = currentPalette().surfaceAlt;
    m_iconColor = currentPalette().text;
}

void TransportButton::setVisual(IconId icon, const QColor& fill, const QColor& iconColor)
{
    m_icon = icon;
    m_fill = fill;
    m_iconColor = iconColor;
    update();
}

void TransportButton::setCaption(const QString& caption)
{
    m_caption = caption;
    setAccessibleName(caption);
    updateGeometry();
    update();
}

QSize TransportButton::sizeHint() const
{
    const int textW = fontMetrics().horizontalAdvance(m_caption) + 8;
    return {std::max(m_diameter + 8, textW), m_diameter + fontMetrics().height() + 8};
}

void TransportButton::enterEvent(QEnterEvent* e)
{
    QAbstractButton::enterEvent(e);
    update();
}

void TransportButton::leaveEvent(QEvent* e)
{
    QAbstractButton::leaveEvent(e);
    update();
}

void TransportButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette& pal = currentPalette();
    const QRectF circle((width() - m_diameter) / 2.0, 2, m_diameter, m_diameter);
    QColor fill = m_fill;
    if (!isEnabled())
        fill = pal.surfaceAlt;
    else if (isDown())
        fill = fill.darker(115);
    else if (underMouse())
        fill = fill.lighter(112);
    p.setPen(hasFocus() ? QPen(pal.text, 2) : QPen(pal.border, 1));
    p.setBrush(fill);
    p.drawEllipse(circle.adjusted(1, 1, -1, -1));
    const int iconSize = m_diameter * 4 / 10;
    const QRect iconRect(static_cast<int>(circle.center().x() - iconSize / 2.0),
                         static_cast<int>(circle.center().y() - iconSize / 2.0), iconSize, iconSize);
    makeIcon(m_icon, isEnabled() ? m_iconColor : pal.textDim).paint(&p, iconRect);
    p.setPen(isEnabled() ? pal.text : pal.textDim);
    p.drawText(QRect(0, static_cast<int>(circle.bottom()) + 4, width(), fontMetrics().height() + 2),
               Qt::AlignHCenter | Qt::AlignTop, m_caption);
}

// -------------------------------------------------------- CollapsibleSection

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent, bool expanded) : QWidget(parent)
{
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    m_header = new QToolButton(this);
    m_header->setObjectName("SectionHeader");
    m_header->setText(title);
    m_header->setCheckable(true);
    m_header->setChecked(expanded);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    m_header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_header->setAccessibleName(title);
    m_content = new QWidget(this);
    m_body = new QVBoxLayout(m_content);
    m_body->setContentsMargins(4, 2, 2, 6);
    m_body->setSpacing(6);
    m_content->setVisible(expanded);
    v->addWidget(m_header);
    v->addWidget(m_content);
    connect(m_header, &QToolButton::clicked, this, [this](bool on) {
        setExpanded(on);
        emit toggled(on);
    });
}

void CollapsibleSection::setExpanded(bool on)
{
    m_header->setChecked(on);
    m_header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    m_content->setVisible(on);
}

bool CollapsibleSection::isExpanded() const
{
    return m_header->isChecked();
}

// ------------------------------------------------------------ RegionSelector

RegionSelector::RegionSelector(QScreen* screen, const QRect& monitorPhysical, double aspect)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), m_monitor(monitorPhysical),
      m_aspect(aspect)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    setGeometry(screen->geometry());
    m_dpr = screen->devicePixelRatio();
    m_background = screen->grabWindow(0);
    setFocusPolicy(Qt::StrongFocus);
}

QRect RegionSelector::normalized() const
{
    QRect r = QRect(m_start, m_end).normalized();
    if (m_aspect > 0 && r.width() > 0) {
        const int h = static_cast<int>(r.width() / m_aspect);
        r.setHeight(h);
        if (m_end.y() < m_start.y())
            r.moveBottom(m_start.y());
    }
    return r.intersected(rect());
}

QRect RegionSelector::toPhysical(const QRect& l) const
{
    return QRect(m_monitor.left() + static_cast<int>(std::lround(l.left() * m_dpr)),
                 m_monitor.top() + static_cast<int>(std::lround(l.top() * m_dpr)),
                 static_cast<int>(std::lround(l.width() * m_dpr)) & ~1,
                 static_cast<int>(std::lround(l.height() * m_dpr)) & ~1);
}

void RegionSelector::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.drawPixmap(rect(), m_background);
    p.fillRect(rect(), QColor(0, 0, 0, 120));
    const QRect sel = normalized();
    if (m_hasSelection && sel.isValid()) {
        p.drawPixmap(sel, m_background, QRectF(sel.topLeft() * m_background.devicePixelRatio(),
                                                QSizeF(sel.size()) * m_background.devicePixelRatio()));
        p.setPen(QPen(currentPalette().accent, 2));
        p.drawRect(sel.adjusted(0, 0, -1, -1));
        const QRect phys = toPhysical(sel);
        const QString label = QStringLiteral("%1 x %2").arg(phys.width()).arg(phys.height());
        QFont f = font();
        f.setPointSize(11);
        f.setBold(true);
        p.setFont(f);
        const QRect tr(sel.left(), std::max(0, sel.top() - 28), 120, 24);
        p.fillRect(tr, QColor(0, 0, 0, 180));
        p.setPen(Qt::white);
        p.drawText(tr, Qt::AlignCenter, label);
    }
    QFont f = font();
    f.setPointSize(12);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(rect().adjusted(0, 24, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("Drag to select the recording area.  Enter = confirm,  Esc = cancel%1")
                   .arg(m_aspect > 0 ? QStringLiteral("  (aspect ratio locked)") : QString()));
}

void RegionSelector::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_start = m_end = e->position().toPoint();
    m_dragging = true;
    m_hasSelection = true;
    update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging)
        return;
    m_end = e->position().toPoint();
    update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_dragging = false;
    m_end = e->position().toPoint();
    update();
}

void RegionSelector::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        emit cancelled();
        close();
    } else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        const QRect sel = normalized();
        if (m_hasSelection && sel.width() >= 16 && sel.height() >= 16)
            emit selected(toPhysical(sel));
        else
            emit cancelled();
        close();
    }
}

// ---------------------------------------------------------- CountdownOverlay

CountdownOverlay::CountdownOverlay(QScreen* screen, int seconds)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), m_remaining(seconds)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(220, 220);
    const QRect g = screen->geometry();
    move(g.center() - QPoint(110, 110));
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (--m_remaining <= 0) {
            m_timer.stop();
            emit finished();
            close();
        } else {
            update();
        }
    });
    m_timer.start();
    setFocusPolicy(Qt::StrongFocus);
}

void CountdownOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0, 0, 0, 170));
    p.setPen(QPen(currentPalette().record, 4));
    p.drawEllipse(rect().adjusted(6, 6, -6, -6));
    QFont f = font();
    f.setPixelSize(96);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(rect(), Qt::AlignCenter, QString::number(m_remaining));
}

void CountdownOverlay::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        m_timer.stop();
        emit cancelled();
        close();
    }
}

// -------------------------------------------------------------- RecordingBar

RecordingBar::RecordingBar(QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    // Deliberately NOT WA_TranslucentBackground: that makes a layered window, the kind
    // most likely to slip past capture exclusion. Rounded corners come from a region.
    setAttribute(Qt::WA_ShowWithoutActivating);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 5, 8, 5);
    lay->setSpacing(4);
    m_time = new QLabel(QStringLiteral("00:00:00"), this);
    QFont f = m_time->font();
    f.setPointSize(10);
    f.setBold(true);
    m_time->setFont(f);
    m_time->setStyleSheet(QStringLiteral("color: white;"));
    lay->addWidget(m_time);

    auto makeButton = [this, lay](IconId icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(makeIcon(icon, Qt::white));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setAutoRaise(true);
        lay->addWidget(b);
        return b;
    };
    m_pause = makeButton(IconId::Pause, QStringLiteral("Pause / resume"));
    auto* stop = makeButton(IconId::Stop, QStringLiteral("Stop recording"));
    m_mic = makeButton(IconId::Mic, QStringLiteral("Mute / unmute microphone"));
    connect(m_pause, &QToolButton::clicked, this, &RecordingBar::pauseClicked);
    connect(stop, &QToolButton::clicked, this, &RecordingBar::stopClicked);
    connect(m_mic, &QToolButton::clicked, this, &RecordingBar::micClicked);
    adjustSize();
}

void RecordingBar::setElapsed(const QString& text, bool paused)
{
    m_time->setText(paused ? text + QStringLiteral("  paused") : text);
    if (paused != m_paused) {
        m_paused = paused;
        m_pause->setIcon(makeIcon(paused ? IconId::Resume : IconId::Pause, Qt::white));
        update();
    }
}

void RecordingBar::setMicMuted(bool muted, bool micEnabled)
{
    m_mic->setVisible(micEnabled);
    m_mic->setIcon(makeIcon(muted ? IconId::MicOff : IconId::Mic, Qt::white));
}

void RecordingBar::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), height() / 2.0, height() / 2.0);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void RecordingBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(20, 22, 28));
    p.setPen(Qt::NoPen);
    p.setBrush(m_paused ? currentPalette().warning : currentPalette().record);
    p.drawEllipse(QPointF(9, height() / 2.0), 3.5, 3.5);
}

void RecordingBar::mousePressEvent(QMouseEvent* e)
{
    m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
}

void RecordingBar::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton)
        move(e->globalPosition().toPoint() - m_dragOffset);
}

} // namespace luma::app
