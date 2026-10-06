#pragma once

#include "Icons.h"

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

class QPushButton;
class QToolButton;
class QScreen;
class QVBoxLayout;

namespace luma::app {

// iOS/Windows-11 style on/off switch (checkable, keyboard accessible: Space toggles).
class ToggleSwitch : public QWidget {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    bool isChecked() const { return m_checked; }
    void setChecked(bool on);
    QSize sizeHint() const override { return {36, 20}; }

signals:
    void toggled(bool on);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    bool m_checked = false;
};

// Small labelled value ("Frame rate  30 fps").
class StatTile : public QWidget {
    Q_OBJECT
public:
    StatTile(const QString& caption, QWidget* parent = nullptr);
    void setValue(const QString& value, bool warn = false);

private:
    QLabel* m_caption;
    QLabel* m_value;
};

// Non-intrusive notification strip with an optional action and close button.
class Banner : public QFrame {
    Q_OBJECT
public:
    enum class Kind { Info, Success, Warning, Error };
    explicit Banner(QWidget* parent = nullptr);
    void showMessage(Kind kind, const QString& text, const QString& actionText = {}, int autoHideMs = 0);

signals:
    void actionClicked();

private:
    QLabel* m_icon;
    QLabel* m_text;
    QPushButton* m_action;
    QTimer m_hide;
};

// Segmented peak meter (dBFS scale, -60..0) with fast attack / slow decay.
// Cheap to paint: plain rectangles, no antialiasing or gradients.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);
    void setLevel(float linearPeak);
    void reset();
    QSize sizeHint() const override { return {160, 8}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    float m_db = -60.f;
    float m_hold = -60.f;
    QElapsedTimer m_holdTimer;
};

// Shows the latest webcam frame (aspect-fit) or a placeholder message.
class WebcamPreview : public QWidget {
    Q_OBJECT
public:
    explicit WebcamPreview(QWidget* parent = nullptr);
    void setFrame(const QImage& frame);
    void setMessage(const QString& text);
    void setMirror(bool on);
    // Side-by-side "Before | After": left half original, right half processed.
    void setOriginal(const QImage& frame);
    void setCompare(bool on);
    QSize sizeHint() const override { return {320, 180}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void drawImage(QPainter& p, const QImage& img, const QRectF& area);

    QImage m_frame;
    QImage m_original;
    QString m_message;
    bool m_mirror = false;
    bool m_compare = false;
};

// Live preview of the composed recording frame (see PreviewController). The images
// arrive at (about) the widget's pixel size, so painting is a plain blit.
class PreviewView : public QWidget {
    Q_OBJECT
public:
    explicit PreviewView(QWidget* parent = nullptr);
    void setFrame(const QImage& frame);
    void setMessage(const QString& text); // empty = show frames
    void setInfo(const QString& text);    // chip in the top-right corner ("Display 1 | 1920×1080 | 30 FPS")
    void setRecording(bool recording, bool paused);
    void clearFrame();
    QSize pixelSize() const;              // device pixels available for the image
    QSize sizeHint() const override { return {640, 360}; }
    QSize minimumSizeHint() const override { return {320, 180}; }

signals:
    void resized();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QImage m_frame;
    QString m_message;
    QString m_info;
    bool m_recording = false;
    bool m_paused = false;
};

// One entry of the capture mode list: icon, title and a short description.
class ModeButton : public QAbstractButton {
    Q_OBJECT
public:
    ModeButton(IconId icon, const QString& title, const QString& subtitle, QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    IconId m_icon;
    QString m_title;
    QString m_subtitle;
};

// Round transport button (Record / Pause / Stop / Screenshot) with a caption below.
class TransportButton : public QAbstractButton {
    Q_OBJECT
public:
    TransportButton(const QString& caption, int diameter, QWidget* parent = nullptr);
    void setVisual(IconId icon, const QColor& fill, const QColor& iconColor);
    void setCaption(const QString& caption);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    IconId m_icon = IconId::Record;
    QColor m_fill;
    QColor m_iconColor;
    int m_diameter;
    QString m_caption;
};

// Titled section whose body can be collapsed (keeps long panels scannable).
class CollapsibleSection : public QWidget {
    Q_OBJECT
public:
    CollapsibleSection(const QString& title, QWidget* parent = nullptr, bool expanded = true);
    QVBoxLayout* body() const { return m_body; }
    void setExpanded(bool on);
    bool isExpanded() const;

signals:
    void toggled(bool expanded); // by the user (header click)

private:
    QToolButton* m_header;
    QWidget* m_content;
    QVBoxLayout* m_body;
};

// Full-screen overlay on one monitor for dragging out a capture region.
// Coordinates are converted to physical desktop pixels (monitorRect is physical).
class RegionSelector : public QWidget {
    Q_OBJECT
public:
    RegionSelector(QScreen* screen, const QRect& monitorPhysical, double aspect /*0 = free*/);

signals:
    void selected(const QRect& physical);
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    QRect normalized() const;
    QRect toPhysical(const QRect& logical) const;

    QPixmap m_background;
    QRect m_monitor;
    double m_dpr = 1.0;
    double m_aspect = 0;
    QPoint m_start, m_end;
    bool m_dragging = false;
    bool m_hasSelection = false;
};

// Big countdown in the middle of a screen; Esc cancels.
class CountdownOverlay : public QWidget {
    Q_OBJECT
public:
    CountdownOverlay(QScreen* screen, int seconds);

signals:
    void finished();
    void cancelled();

protected:
    void paintEvent(QPaintEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    int m_remaining;
    QTimer m_timer;
};

// Optional recording HUD (off by default): a small always-on-top control bar.
// It is an ordinary opaque window (rounded via a window region, NOT a layered/
// translucent window) and is excluded from capture before it is first shown.
class RecordingBar : public QWidget {
    Q_OBJECT
public:
    explicit RecordingBar(QWidget* parent = nullptr);
    void setElapsed(const QString& text, bool paused);
    void setMicMuted(bool muted, bool micEnabled);

signals:
    void pauseClicked();
    void stopClicked();
    void micClicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    QLabel* m_time;
    QToolButton* m_pause;
    QToolButton* m_mic;
    QPoint m_dragOffset;
    bool m_paused = false;
};

} // namespace luma::app
