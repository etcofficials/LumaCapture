#pragma once

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

namespace luma::app {

// iOS/Windows-11 style on/off switch (checkable, keyboard accessible: Space toggles).
class ToggleSwitch : public QWidget {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    bool isChecked() const { return m_checked; }
    void setChecked(bool on);
    QSize sizeHint() const override { return {38, 22}; }

signals:
    void toggled(bool on);

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;

private:
    bool m_checked = false;
};

// Small labelled value ("Frame rate  30 fps") used in the recording panel.
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

// Horizontal peak meter (dBFS scale, -60..0) with fast attack / slow decay.
class LevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit LevelMeter(QWidget* parent = nullptr);
    void setLevel(float linearPeak);
    QSize sizeHint() const override { return {140, 10}; }

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
    QSize sizeHint() const override { return {240, 135}; }

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

// Big translucent countdown in the middle of a screen; Esc cancels.
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

// Small always-on-top control bar shown while recording (excluded from capture).
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
