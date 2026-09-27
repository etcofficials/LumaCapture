#include "widgets/Widgets.h"

#include "Icons.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace luma::app {

// -------------------------------------------------------------- ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(38, 22);
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
    const QRectF track = QRectF(rect()).adjusted(1, 3, -1, -3);
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
    v->setSpacing(2);
    m_caption = new QLabel(caption, this);
    m_caption->setObjectName("TileCaption");
    m_value = new QLabel(QStringLiteral("-"), this);
    m_value->setObjectName("TileValue");
    v->addWidget(m_caption);
    v->addWidget(m_value);
}

void StatTile::setValue(const QString& value, bool warn)
{
    m_value->setText(value);
    m_value->setProperty("warn", warn);
    m_value->style()->unpolish(m_value);
    m_value->style()->polish(m_value);
}

// -------------------------------------------------------------------- Banner

Banner::Banner(QWidget* parent) : QFrame(parent)
{
    setObjectName("Banner");
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(12, 8, 8, 8);
    h->setSpacing(10);
    m_icon = new QLabel(this);
    m_icon->setFixedWidth(10);
    m_text = new QLabel(this);
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_action = new QPushButton(this);
    m_action->setObjectName("Flat");
    auto* close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
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
    m_db = db > m_db ? db : std::max(db, m_db - 3.f); // fast attack, ~45 dB/s decay at 15 Hz
    if (m_db >= m_hold || m_holdTimer.elapsed() > 1200) {
        m_hold = m_db;
        m_holdTimer.restart();
    }
    update();
}

void LevelMeter::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Palette& pal = currentPalette();
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(pal.surfaceAlt);
    p.drawRoundedRect(r, 3, 3);
    auto frac = [](float db) { return std::clamp((db + 60.f) / 60.f, 0.f, 1.f); };
    const double w = r.width() * frac(m_db);
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0.0, pal.success);
    g.setColorAt(0.75, pal.success);
    g.setColorAt(0.9, pal.warning);
    g.setColorAt(1.0, pal.record);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(r.left(), r.top(), w, r.height()), 3, 3);
    if (m_hold > -59.f) {
        const double hx = r.left() + r.width() * frac(m_hold);
        p.setPen(QPen(pal.text, 1.5));
        p.drawLine(QPointF(hx, r.top() + 1), QPointF(hx, r.bottom() - 1));
    }
}

// ------------------------------------------------------------- WebcamPreview

WebcamPreview::WebcamPreview(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(160, 90);
    setAccessibleName(QStringLiteral("Webcam preview"));
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
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const Palette& pal = currentPalette();
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), 10, 10);
    p.setClipPath(clip);
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
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 160));
            p.drawRoundedRect(t, 6, 6);
            p.setPen(Qt::white);
            p.drawText(t, Qt::AlignCenter, text);
        };
        tag(l, QStringLiteral("BEFORE"));
        tag(r, QStringLiteral("AFTER"));
    } else {
        drawImage(p, m_frame, QRectF(rect()));
    }
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
    setAttribute(Qt::WA_TranslucentBackground);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 6, 8, 6);
    lay->setSpacing(6);
    m_time = new QLabel(QStringLiteral("00:00:00"), this);
    QFont f = m_time->font();
    f.setPointSize(11);
    f.setBold(true);
    m_time->setFont(f);
    m_time->setStyleSheet(QStringLiteral("color: white;"));
    lay->addWidget(m_time);

    auto makeButton = [this, lay](IconId icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(makeIcon(icon, Qt::white));
        b->setIconSize(QSize(18, 18));
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

void RecordingBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(20, 22, 28, 235));
    p.drawRoundedRect(rect(), height() / 2.0, height() / 2.0);
    p.setBrush(m_paused ? currentPalette().warning : currentPalette().record);
    p.drawEllipse(QPointF(8, height() / 2.0), 3.5, 3.5);
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
