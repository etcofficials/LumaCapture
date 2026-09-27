#pragma once

#include "AppSettings.h"
#include "OverlayRenderer.h"

#include <QDialog>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QStackedWidget;

namespace luma::app {

class WebcamController;

// Button showing a colour swatch; opens QColorDialog.
class ColorButton : public QPushButton {
    Q_OBJECT
public:
    explicit ColorButton(QWidget* parent = nullptr);
    void setColor(const QColor& c);
    QColor color() const { return m_color; }

signals:
    void colorChanged(const QColor& c);

private:
    QColor m_color = Qt::white;
};

// Scaled preview of the output frame where the webcam and overlays are placed
// by dragging (move) and dragging the corner handle (resize).
class LayoutCanvas : public QWidget {
    Q_OBJECT
public:
    LayoutCanvas(AppSettings& settings, WebcamController& webcam, OverlayRenderer& renderer, QWidget* parent = nullptr);

    void setOutputSize(const QSize& s);
    void setBackground(const QPixmap& p);
    void setCameraFrame(const QImage& img);
    void setSelectedOverlay(int index); // -1 = webcam
    QSize sizeHint() const override { return {640, 360}; }

signals:
    void changed();
    void overlaySelected(int index); // -1 = webcam

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QRectF frameRect() const;
    QRectF webcamRect() const;
    QRectF overlayRect(int i) const;

    AppSettings& m_settings;
    WebcamController& m_webcam;
    OverlayRenderer& m_renderer;
    QSize m_out{1920, 1080};
    QPixmap m_background;
    QImage m_camFrame;
    int m_selected = -2; // -2 none, -1 webcam, >=0 overlay
    enum class Drag { None, Move, Resize } m_drag = Drag::None;
    QPointF m_dragStart;
    QRectF m_startRect;
};

// "Layout & overlays" editor. Edits AppSettings in place; emits changed()
// (the main window saves the settings and pushes them to a running recording).
class LayoutDialog : public QDialog {
    Q_OBJECT
public:
    LayoutDialog(AppSettings& settings, WebcamController& webcam, QSize outputSize, const QPixmap& background,
                 QWidget* parent = nullptr);

signals:
    void changed();

private:
    QWidget* buildWebcamTab();
    QWidget* buildOverlayTab();
    QWidget* buildWatermarkTab();
    void refreshOverlayList();
    void loadOverlayEditor();
    void storeOverlayEditor();
    void emitChanged();

    AppSettings& m_settings;
    WebcamController& m_webcam;
    OverlayRenderer m_renderer;
    LayoutCanvas* m_canvas = nullptr;
    QSize m_outSize{1920, 1080};

    // Overlay editor
    QListWidget* m_list = nullptr;
    QWidget* m_editor = nullptr;
    QStackedWidget* m_typeStack = nullptr;
    QCheckBox* m_itemEnabled = nullptr;
    QSlider* m_itemOpacity = nullptr;
    QPlainTextEdit* m_text = nullptr;
    QFontComboBox* m_font = nullptr;
    QSpinBox* m_fontSize = nullptr;
    QCheckBox* m_bold = nullptr;
    ColorButton* m_textColor = nullptr;
    QCheckBox* m_outline = nullptr;
    ColorButton* m_outlineColor = nullptr;
    QSpinBox* m_outlineWidth = nullptr;
    QCheckBox* m_shadow = nullptr;
    QLineEdit* m_imagePath = nullptr;
    ColorButton* m_fill = nullptr;
    QSpinBox* m_borderWidth = nullptr;
    ColorButton* m_borderColor = nullptr;
    QSpinBox* m_radius = nullptr;
    bool m_loading = false;
};

} // namespace luma::app
