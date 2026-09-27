#include "LayoutDialog.h"

#include "Theme.h"
#include "WebcamController.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace luma::app {

// ---------------------------------------------------------------- ColorButton

ColorButton::ColorButton(QWidget* parent) : QPushButton(parent)
{
    setFixedWidth(64);
    connect(this, &QPushButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, QStringLiteral("Choose colour"),
                                                QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            setColor(c);
            emit colorChanged(c);
        }
    });
    setColor(Qt::white);
}

void ColorButton::setColor(const QColor& c)
{
    m_color = c;
    setStyleSheet(QStringLiteral("QPushButton { background: %1; border: 1px solid #888; border-radius: 6px; }")
                      .arg(c.name(QColor::HexArgb)));
    setAccessibleName(QStringLiteral("Colour %1").arg(c.name()));
}

namespace {

gpu::Rgba toRgba(const QColor& c)
{
    return {static_cast<float>(c.redF()), static_cast<float>(c.greenF()), static_cast<float>(c.blueF()),
            static_cast<float>(c.alphaF())};
}
QColor toQColor(const gpu::Rgba& c) { return QColor::fromRgbF(c.r, c.g, c.b, c.a); }

QSlider* makeSlider(int min, int max, int value, QWidget* parent)
{
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(min, max);
    s->setValue(value);
    return s;
}

} // namespace

// --------------------------------------------------------------- LayoutCanvas

LayoutCanvas::LayoutCanvas(AppSettings& settings, WebcamController& webcam, OverlayRenderer& renderer, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_webcam(webcam), m_renderer(renderer)
{
    setMinimumSize(480, 270);
    setMouseTracking(true);
    setAccessibleName(QStringLiteral("Layout preview"));
    connect(&webcam, &WebcamController::previewFrame, this, &LayoutCanvas::setCameraFrame);
}

void LayoutCanvas::setOutputSize(const QSize& s)
{
    if (s.isValid() && !s.isEmpty())
        m_out = s;
    update();
}

void LayoutCanvas::setBackground(const QPixmap& p)
{
    m_background = p;
    update();
}

void LayoutCanvas::setCameraFrame(const QImage& img)
{
    m_camFrame = img;
    update();
}

void LayoutCanvas::setSelectedOverlay(int index)
{
    m_selected = index;
    update();
}

QRectF LayoutCanvas::frameRect() const
{
    const QSizeF s = QSizeF(m_out).scaled(QSizeF(size()) - QSizeF(8, 8), Qt::KeepAspectRatio);
    return QRectF((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
}

QRectF LayoutCanvas::webcamRect() const
{
    const QRectF f = frameRect();
    const gpu::WebcamPlacement p = resolvedPlacement(m_settings.webcamPlacement,
                                                     m_webcam.aspect(m_settings.webcamPlacement), m_out.width(),
                                                     m_out.height());
    return QRectF(f.left() + p.x * f.width(), f.top() + p.y * f.height(), p.w * f.width(), p.h * f.height());
}

QRectF LayoutCanvas::overlayRect(int i) const
{
    const QRectF f = frameRect();
    const OverlayItem& o = m_settings.overlays[i];
    return QRectF(f.left() + o.x * f.width(), f.top() + o.y * f.height(), o.w * f.width(), o.h * f.height());
}

void LayoutCanvas::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const Palette& pal = currentPalette();
    const QRectF f = frameRect();
    p.fillRect(rect(), pal.window);
    if (!m_background.isNull())
        p.drawPixmap(f, m_background, QRectF(m_background.rect()));
    else
        p.fillRect(f, QColor(40, 44, 52));

    // Webcam
    if (m_settings.webcamEnabled) {
        const QRectF r = webcamRect();
        const auto& wp = m_settings.webcamPlacement;
        QPainterPath shape;
        if (wp.shape == gpu::WebcamShape::Circle)
            shape.addEllipse(r);
        else if (wp.shape == gpu::WebcamShape::Rounded)
            shape.addRoundedRect(r, wp.cornerRadius * std::min(r.width(), r.height()),
                                 wp.cornerRadius * std::min(r.width(), r.height()));
        else
            shape.addRect(r);
        p.save();
        p.setOpacity(wp.opacity);
        p.setClipPath(shape);
        if (!m_camFrame.isNull()) {
            const gpu::WebcamPlacement rp = resolvedPlacement(wp, m_webcam.aspect(wp), m_out.width(), m_out.height());
            const QRectF src(m_camFrame.width() * rp.cropLeft, m_camFrame.height() * rp.cropTop,
                             m_camFrame.width() * (1 - rp.cropLeft - rp.cropRight),
                             m_camFrame.height() * (1 - rp.cropTop - rp.cropBottom));
            if (wp.mirror) {
                p.translate(r.center());
                p.scale(-1, 1);
                p.translate(-r.center());
            }
            p.drawImage(r, m_camFrame, src);
        } else {
            p.fillRect(r, QColor(70, 76, 90));
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("Webcam"));
        }
        p.restore();
        if (wp.borderPx > 0) {
            const double scale = f.height() / m_out.height();
            p.setPen(QPen(toQColor(wp.borderColor), std::max(1.0, wp.borderPx * scale)));
            p.setBrush(Qt::NoBrush);
            p.drawPath(shape);
        }
    }

    // Overlays (same renderer as the recording) and watermark.
    p.save();
    p.setClipRect(f);
    m_renderer.paint(p, m_settings, f);
    p.restore();

    // Selection handles.
    QRectF sel;
    if (m_selected == -1 && m_settings.webcamEnabled)
        sel = webcamRect();
    else if (m_selected >= 0 && m_selected < m_settings.overlays.size())
        sel = overlayRect(m_selected);
    if (sel.isValid()) {
        p.setPen(QPen(pal.accent, 1.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(sel);
        p.setBrush(pal.accent);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(sel.bottomRight() - QPointF(6, 6), QSizeF(12, 12)));
    }
}

void LayoutCanvas::mousePressEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    auto hitHandle = [&](const QRectF& r) { return QRectF(r.bottomRight() - QPointF(8, 8), QSizeF(16, 16)).contains(pos); };

    // Handle of the current selection first.
    QRectF cur;
    if (m_selected == -1 && m_settings.webcamEnabled)
        cur = webcamRect();
    else if (m_selected >= 0 && m_selected < m_settings.overlays.size())
        cur = overlayRect(m_selected);
    if (cur.isValid() && hitHandle(cur)) {
        m_drag = Drag::Resize;
    } else {
        int hit = -2;
        for (int i = static_cast<int>(m_settings.overlays.size()) - 1; i >= 0 && hit == -2; --i)
            if (m_settings.overlays[i].enabled && overlayRect(i).contains(pos))
                hit = i;
        if (hit == -2 && m_settings.webcamEnabled && webcamRect().contains(pos))
            hit = -1;
        m_selected = hit;
        if (hit != -2)
            emit overlaySelected(hit);
        m_drag = hit == -2 ? Drag::None : Drag::Move;
        cur = hit == -1 ? webcamRect() : (hit >= 0 ? overlayRect(hit) : QRectF());
    }
    m_dragStart = pos;
    m_startRect = cur;
    update();
}

void LayoutCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (m_drag == Drag::None || m_selected == -2)
        return;
    const QRectF f = frameRect();
    const QPointF d = e->position() - m_dragStart;
    QRectF r = m_startRect;
    if (m_drag == Drag::Move)
        r.translate(d);
    else
        r.setBottomRight(r.bottomRight() + d);
    const double x = (r.left() - f.left()) / f.width(), y = (r.top() - f.top()) / f.height();
    const double w = std::clamp(r.width() / f.width(), 0.03, 1.0), h = std::clamp(r.height() / f.height(), 0.02, 1.0);
    if (m_selected == -1) {
        auto& wp = m_settings.webcamPlacement;
        wp.x = static_cast<float>(std::clamp(x, 0.0, 1.0));
        wp.y = static_cast<float>(std::clamp(y, 0.0, 1.0));
        if (m_drag == Drag::Resize)
            wp.w = static_cast<float>(w);
    } else if (m_selected < m_settings.overlays.size()) {
        OverlayItem& o = m_settings.overlays[m_selected];
        o.x = std::clamp(x, -0.5, 1.0);
        o.y = std::clamp(y, -0.5, 1.0);
        if (m_drag == Drag::Resize) {
            o.w = w;
            o.h = h;
        }
    }
    update();
    emit changed();
}

void LayoutCanvas::mouseReleaseEvent(QMouseEvent*)
{
    m_drag = Drag::None;
}

// --------------------------------------------------------------- LayoutDialog

LayoutDialog::LayoutDialog(AppSettings& settings, WebcamController& webcam, QSize outputSize,
                           const QPixmap& background, QWidget* parent)
    : QDialog(parent), m_settings(settings), m_webcam(webcam)
{
    if (outputSize.isValid() && !outputSize.isEmpty())
        m_outSize = outputSize;
    setWindowTitle(QStringLiteral("Layout & overlays"));
    resize(1100, 640);
    auto* root = new QHBoxLayout(this);

    auto* left = new QVBoxLayout;
    m_canvas = new LayoutCanvas(settings, webcam, m_renderer, this);
    m_canvas->setOutputSize(outputSize);
    m_canvas->setBackground(background);
    left->addWidget(m_canvas, 1);
    auto* hint = new QLabel(QStringLiteral("Drag items to move them; drag the square handle to resize. "
                                           "Layer order (bottom to top): screen, cursor effects, cursor, webcam, "
                                           "overlays in list order, watermark."),
                            this);
    hint->setObjectName("Dim");
    hint->setWordWrap(true);
    left->addWidget(hint);
    root->addLayout(left, 3);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildWebcamTab(), QStringLiteral("Webcam"));
    tabs->addTab(buildOverlayTab(), QStringLiteral("Text && images"));
    tabs->addTab(buildWatermarkTab(), QStringLiteral("Watermark"));
    tabs->setMinimumWidth(360);
    root->addWidget(tabs, 2);

    connect(m_canvas, &LayoutCanvas::changed, this, &LayoutDialog::emitChanged);
    connect(m_canvas, &LayoutCanvas::overlaySelected, this, [this, tabs](int index) {
        if (index >= 0) {
            tabs->setCurrentIndex(1);
            m_list->setCurrentRow(index);
        } else {
            tabs->setCurrentIndex(0);
        }
    });
}

void LayoutDialog::emitChanged()
{
    m_canvas->update();
    emit changed();
}

QWidget* LayoutDialog::buildWebcamTab()
{
    auto* w = new QWidget(this);
    auto* form = new QFormLayout(w);
    auto& wp = m_settings.webcamPlacement;

    auto* enabled = new QCheckBox(QStringLiteral("Show webcam in the recording"), w);
    enabled->setChecked(m_settings.webcamEnabled);
    connect(enabled, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.webcamEnabled = on;
        emitChanged();
    });
    form->addRow(enabled);

    auto* shape = new QComboBox(w);
    shape->addItems({QStringLiteral("Rectangle"), QStringLiteral("Rounded rectangle"), QStringLiteral("Circle")});
    shape->setCurrentIndex(static_cast<int>(wp.shape));
    connect(shape, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_settings.webcamPlacement.shape = static_cast<gpu::WebcamShape>(i);
        emitChanged();
    });
    form->addRow(QStringLiteral("Shape"), shape);

    auto* radius = makeSlider(0, 50, static_cast<int>(wp.cornerRadius * 100), w);
    connect(radius, &QSlider::valueChanged, this, [this](int v) {
        m_settings.webcamPlacement.cornerRadius = v / 100.f;
        emitChanged();
    });
    form->addRow(QStringLiteral("Corner radius"), radius);

    auto* size = makeSlider(5, 60, static_cast<int>(wp.w * 100), w);
    size->setToolTip(QStringLiteral("Webcam width as a percentage of the video width"));
    connect(size, &QSlider::valueChanged, this, [this](int v) {
        m_settings.webcamPlacement.w = v / 100.f;
        emitChanged();
    });
    form->addRow(QStringLiteral("Size"), size);

    auto* corners = new QHBoxLayout;
    auto* margin = new QSpinBox(w);
    margin->setRange(0, 200);
    margin->setValue(24);
    margin->setSuffix(QStringLiteral(" px"));
    const char* names[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    for (int c = 0; c < 4; ++c) {
        auto* b = new QPushButton(QString::fromLatin1(names[c]), w);
        connect(b, &QPushButton::clicked, this, [this, c, margin] {
            const QSize out = m_outSize;
            auto& p = m_settings.webcamPlacement;
            const gpu::WebcamPlacement r = resolvedPlacement(p, m_webcam.aspect(p), out.width(), out.height());
            const double mx = margin->value() / static_cast<double>(out.width());
            const double my = margin->value() / static_cast<double>(out.height());
            p.x = static_cast<float>((c % 2 == 0) ? mx : 1.0 - r.w - mx);
            p.y = static_cast<float>((c < 2) ? my : 1.0 - r.h - my);
            emitChanged();
        });
        corners->addWidget(b);
    }
    form->addRow(QStringLiteral("Snap to"), corners);
    form->addRow(QStringLiteral("Margin"), margin);

    auto* border = new QSpinBox(w);
    border->setRange(0, 30);
    border->setValue(static_cast<int>(wp.borderPx));
    border->setSuffix(QStringLiteral(" px"));
    auto* borderColor = new ColorButton(w);
    borderColor->setColor(toQColor(wp.borderColor));
    auto* borderRow = new QHBoxLayout;
    borderRow->addWidget(border);
    borderRow->addWidget(borderColor);
    borderRow->addStretch();
    connect(border, &QSpinBox::valueChanged, this, [this](int v) {
        m_settings.webcamPlacement.borderPx = static_cast<float>(v);
        emitChanged();
    });
    connect(borderColor, &ColorButton::colorChanged, this, [this](const QColor& c) {
        m_settings.webcamPlacement.borderColor = toRgba(c);
        emitChanged();
    });
    form->addRow(QStringLiteral("Border"), borderRow);

    auto* opacity = makeSlider(10, 100, static_cast<int>(wp.opacity * 100), w);
    connect(opacity, &QSlider::valueChanged, this, [this](int v) {
        m_settings.webcamPlacement.opacity = v / 100.f;
        emitChanged();
    });
    form->addRow(QStringLiteral("Opacity"), opacity);

    auto* mirror = new QCheckBox(QStringLiteral("Mirror (selfie view)"), w);
    mirror->setChecked(wp.mirror);
    connect(mirror, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.webcamPlacement.mirror = on;
        emitChanged();
    });
    form->addRow(mirror);

    auto addCrop = [&](const QString& label, float gpu::WebcamPlacement::*field) {
        auto* s = makeSlider(0, 45, static_cast<int>(wp.*field * 100), w);
        s->setToolTip(QStringLiteral("Crop this edge of the camera image (percent)"));
        connect(s, &QSlider::valueChanged, this, [this, field](int v) {
            m_settings.webcamPlacement.*field = v / 100.f;
            emitChanged();
        });
        form->addRow(label, s);
    };
    addCrop(QStringLiteral("Crop left"), &gpu::WebcamPlacement::cropLeft);
    addCrop(QStringLiteral("Crop right"), &gpu::WebcamPlacement::cropRight);
    addCrop(QStringLiteral("Crop top"), &gpu::WebcamPlacement::cropTop);
    addCrop(QStringLiteral("Crop bottom"), &gpu::WebcamPlacement::cropBottom);

    auto* note = new QLabel(QStringLiteral("Aspect ratio follows the camera (after cropping). Circle crops the "
                                           "image to a centred square."),
                            w);
    note->setObjectName("Dim");
    note->setWordWrap(true);
    form->addRow(note);
    return w;
}

QWidget* LayoutDialog::buildOverlayTab()
{
    auto* w = new QWidget(this);
    auto* v = new QVBoxLayout(w);
    m_list = new QListWidget(w);
    m_list->setMaximumHeight(140);
    m_list->setAccessibleName(QStringLiteral("Overlay items"));
    v->addWidget(m_list);

    auto* buttons = new QGridLayout;
    auto* addText = new QPushButton(QStringLiteral("Add text"), w);
    auto* addImage = new QPushButton(QStringLiteral("Add image"), w);
    auto* addBox = new QPushButton(QStringLiteral("Add box"), w);
    auto* remove = new QPushButton(QStringLiteral("Remove"), w);
    auto* up = new QPushButton(QStringLiteral("Move up"), w);
    auto* down = new QPushButton(QStringLiteral("Move down"), w);
    buttons->addWidget(addText, 0, 0);
    buttons->addWidget(addImage, 0, 1);
    buttons->addWidget(addBox, 0, 2);
    buttons->addWidget(remove, 1, 0);
    buttons->addWidget(up, 1, 1);
    buttons->addWidget(down, 1, 2);
    v->addLayout(buttons);

    auto addItem = [this](OverlayItem::Type type) {
        OverlayItem o;
        o.type = type;
        if (type == OverlayItem::Type::Image) {
            const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Choose image"), QString(),
                                                              QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
            if (file.isEmpty())
                return;
            o.imagePath = file;
            o.w = 0.15;
            o.h = 0.15;
        } else if (type == OverlayItem::Type::Rectangle) {
            o.w = 0.3;
            o.h = 0.12;
        } else {
            o.w = 0.4;
            o.h = 0.08;
        }
        m_settings.overlays.append(o);
        refreshOverlayList();
        m_list->setCurrentRow(static_cast<int>(m_settings.overlays.size()) - 1);
        emitChanged();
    };
    connect(addText, &QPushButton::clicked, this, [addItem] { addItem(OverlayItem::Type::Text); });
    connect(addImage, &QPushButton::clicked, this, [addItem] { addItem(OverlayItem::Type::Image); });
    connect(addBox, &QPushButton::clicked, this, [addItem] { addItem(OverlayItem::Type::Rectangle); });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_settings.overlays.size())
            return;
        m_settings.overlays.removeAt(row);
        refreshOverlayList();
        emitChanged();
    });
    auto move = [this](int delta) {
        const int row = m_list->currentRow(), to = row + delta;
        if (row < 0 || to < 0 || to >= m_settings.overlays.size())
            return;
        m_settings.overlays.swapItemsAt(row, to);
        refreshOverlayList();
        m_list->setCurrentRow(to);
        emitChanged();
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });

    // Property editor.
    m_editor = new QWidget(w);
    auto* form = new QFormLayout(m_editor);
    m_itemEnabled = new QCheckBox(QStringLiteral("Visible"), m_editor);
    form->addRow(m_itemEnabled);
    m_itemOpacity = makeSlider(5, 100, 100, m_editor);
    form->addRow(QStringLiteral("Opacity"), m_itemOpacity);
    m_typeStack = new QStackedWidget(m_editor);

    auto* textPage = new QWidget(m_typeStack);
    auto* tf = new QFormLayout(textPage);
    tf->setContentsMargins(0, 0, 0, 0);
    m_text = new QPlainTextEdit(textPage);
    m_text->setMaximumHeight(70);
    tf->addRow(QStringLiteral("Text"), m_text);
    m_font = new QFontComboBox(textPage);
    tf->addRow(QStringLiteral("Font"), m_font);
    m_fontSize = new QSpinBox(textPage);
    m_fontSize->setRange(8, 300);
    m_fontSize->setSuffix(QStringLiteral(" px @1080p"));
    m_bold = new QCheckBox(QStringLiteral("Bold"), textPage);
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_fontSize);
    sizeRow->addWidget(m_bold);
    tf->addRow(QStringLiteral("Size"), sizeRow);
    m_textColor = new ColorButton(textPage);
    tf->addRow(QStringLiteral("Colour"), m_textColor);
    m_outline = new QCheckBox(QStringLiteral("Outline"), textPage);
    m_outlineColor = new ColorButton(textPage);
    m_outlineWidth = new QSpinBox(textPage);
    m_outlineWidth->setRange(1, 20);
    auto* outRow = new QHBoxLayout;
    outRow->addWidget(m_outline);
    outRow->addWidget(m_outlineColor);
    outRow->addWidget(m_outlineWidth);
    tf->addRow(outRow);
    m_shadow = new QCheckBox(QStringLiteral("Drop shadow"), textPage);
    tf->addRow(m_shadow);
    m_typeStack->addWidget(textPage);

    auto* imagePage = new QWidget(m_typeStack);
    auto* imf = new QFormLayout(imagePage);
    imf->setContentsMargins(0, 0, 0, 0);
    m_imagePath = new QLineEdit(imagePage);
    auto* browse = new QPushButton(QStringLiteral("Browse..."), imagePage);
    auto* imgRow = new QHBoxLayout;
    imgRow->addWidget(m_imagePath);
    imgRow->addWidget(browse);
    imf->addRow(QStringLiteral("File"), imgRow);
    auto* imgNote = new QLabel(QStringLiteral("The image keeps its aspect ratio inside its box."), imagePage);
    imgNote->setObjectName("Dim");
    imf->addRow(imgNote);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Choose image"), m_imagePath->text(),
                                                       QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
        if (!f.isEmpty()) {
            m_imagePath->setText(f);
            storeOverlayEditor();
        }
    });
    m_typeStack->addWidget(imagePage);

    auto* boxPage = new QWidget(m_typeStack);
    auto* bf = new QFormLayout(boxPage);
    bf->setContentsMargins(0, 0, 0, 0);
    m_fill = new ColorButton(boxPage);
    bf->addRow(QStringLiteral("Fill"), m_fill);
    m_borderWidth = new QSpinBox(boxPage);
    m_borderWidth->setRange(0, 40);
    m_borderColor = new ColorButton(boxPage);
    auto* bRow = new QHBoxLayout;
    bRow->addWidget(m_borderWidth);
    bRow->addWidget(m_borderColor);
    bf->addRow(QStringLiteral("Border"), bRow);
    m_radius = new QSpinBox(boxPage);
    m_radius->setRange(0, 200);
    bf->addRow(QStringLiteral("Corner radius"), m_radius);
    m_typeStack->addWidget(boxPage);

    form->addRow(m_typeStack);
    v->addWidget(m_editor);
    v->addStretch();

    // Any edit stores the editor into the selected item.
    connect(m_itemEnabled, &QCheckBox::toggled, this, [this] { storeOverlayEditor(); });
    connect(m_itemOpacity, &QSlider::valueChanged, this, [this] { storeOverlayEditor(); });
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] { storeOverlayEditor(); });
    connect(m_font, &QFontComboBox::currentFontChanged, this, [this] { storeOverlayEditor(); });
    connect(m_fontSize, &QSpinBox::valueChanged, this, [this] { storeOverlayEditor(); });
    connect(m_bold, &QCheckBox::toggled, this, [this] { storeOverlayEditor(); });
    connect(m_textColor, &ColorButton::colorChanged, this, [this] { storeOverlayEditor(); });
    connect(m_outline, &QCheckBox::toggled, this, [this] { storeOverlayEditor(); });
    connect(m_outlineColor, &ColorButton::colorChanged, this, [this] { storeOverlayEditor(); });
    connect(m_outlineWidth, &QSpinBox::valueChanged, this, [this] { storeOverlayEditor(); });
    connect(m_shadow, &QCheckBox::toggled, this, [this] { storeOverlayEditor(); });
    connect(m_imagePath, &QLineEdit::editingFinished, this, [this] { storeOverlayEditor(); });
    connect(m_fill, &ColorButton::colorChanged, this, [this] { storeOverlayEditor(); });
    connect(m_borderWidth, &QSpinBox::valueChanged, this, [this] { storeOverlayEditor(); });
    connect(m_borderColor, &ColorButton::colorChanged, this, [this] { storeOverlayEditor(); });
    connect(m_radius, &QSpinBox::valueChanged, this, [this] { storeOverlayEditor(); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        m_canvas->setSelectedOverlay(row);
        loadOverlayEditor();
    });

    refreshOverlayList();
    return w;
}

void LayoutDialog::refreshOverlayList()
{
    const int row = m_list->currentRow();
    m_list->blockSignals(true);
    m_list->clear();
    for (const OverlayItem& o : m_settings.overlays) {
        QString label;
        switch (o.type) {
        case OverlayItem::Type::Text: label = QStringLiteral("Text: %1").arg(o.text.left(30)); break;
        case OverlayItem::Type::Image:
            label = QStringLiteral("Image: %1").arg(QFileInfo(o.imagePath).fileName());
            break;
        case OverlayItem::Type::Rectangle: label = QStringLiteral("Box"); break;
        }
        if (!o.enabled)
            label += QStringLiteral(" (hidden)");
        m_list->addItem(label);
    }
    m_list->blockSignals(false);
    m_list->setCurrentRow(std::min(row, static_cast<int>(m_settings.overlays.size()) - 1));
    loadOverlayEditor();
}

void LayoutDialog::loadOverlayEditor()
{
    const int row = m_list->currentRow();
    m_editor->setEnabled(row >= 0 && row < m_settings.overlays.size());
    if (!m_editor->isEnabled())
        return;
    const OverlayItem& o = m_settings.overlays[row];
    m_loading = true;
    m_itemEnabled->setChecked(o.enabled);
    m_itemOpacity->setValue(static_cast<int>(o.opacity * 100));
    m_typeStack->setCurrentIndex(static_cast<int>(o.type));
    m_text->setPlainText(o.text);
    m_font->setCurrentFont(QFont(o.fontFamily));
    m_fontSize->setValue(o.fontSize);
    m_bold->setChecked(o.bold);
    m_textColor->setColor(o.color);
    m_outline->setChecked(o.outline);
    m_outlineColor->setColor(o.outlineColor);
    m_outlineWidth->setValue(o.outlineWidth);
    m_shadow->setChecked(o.shadow);
    m_imagePath->setText(o.imagePath);
    m_fill->setColor(o.fillColor);
    m_borderWidth->setValue(o.borderWidth);
    m_borderColor->setColor(o.borderColor);
    m_radius->setValue(o.cornerRadius);
    m_loading = false;
}

void LayoutDialog::storeOverlayEditor()
{
    if (m_loading)
        return;
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_settings.overlays.size())
        return;
    OverlayItem& o = m_settings.overlays[row];
    o.enabled = m_itemEnabled->isChecked();
    o.opacity = m_itemOpacity->value() / 100.0;
    o.text = m_text->toPlainText();
    o.fontFamily = m_font->currentFont().family();
    o.fontSize = m_fontSize->value();
    o.bold = m_bold->isChecked();
    o.color = m_textColor->color();
    o.outline = m_outline->isChecked();
    o.outlineColor = m_outlineColor->color();
    o.outlineWidth = m_outlineWidth->value();
    o.shadow = m_shadow->isChecked();
    o.imagePath = m_imagePath->text();
    o.fillColor = m_fill->color();
    o.borderWidth = m_borderWidth->value();
    o.borderColor = m_borderColor->color();
    o.cornerRadius = m_radius->value();
    if (QListWidgetItem* it = m_list->item(row)) {
        QString label = o.type == OverlayItem::Type::Text    ? QStringLiteral("Text: %1").arg(o.text.left(30))
                        : o.type == OverlayItem::Type::Image ? QStringLiteral("Image: %1").arg(QFileInfo(o.imagePath).fileName())
                                                             : QStringLiteral("Box");
        if (!o.enabled)
            label += QStringLiteral(" (hidden)");
        it->setText(label);
    }
    emitChanged();
}

QWidget* LayoutDialog::buildWatermarkTab()
{
    auto* w = new QWidget(this);
    auto* form = new QFormLayout(w);
    auto& wm = m_settings.watermark;
    auto* enabled = new QCheckBox(QStringLiteral("Add a text watermark"), w);
    enabled->setChecked(wm.enabled);
    connect(enabled, &QCheckBox::toggled, this, [this](bool on) {
        m_settings.watermark.enabled = on;
        emitChanged();
    });
    form->addRow(enabled);
    auto* text = new QLineEdit(wm.text, w);
    connect(text, &QLineEdit::textChanged, this, [this](const QString& t) {
        m_settings.watermark.text = t;
        emitChanged();
    });
    form->addRow(QStringLiteral("Text"), text);
    auto* corner = new QComboBox(w);
    corner->addItems({QStringLiteral("Top left"), QStringLiteral("Top right"), QStringLiteral("Bottom left"),
                      QStringLiteral("Bottom right")});
    corner->setCurrentIndex(wm.corner);
    connect(corner, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_settings.watermark.corner = i;
        emitChanged();
    });
    form->addRow(QStringLiteral("Corner"), corner);
    auto* size = new QSpinBox(w);
    size->setRange(8, 120);
    size->setValue(wm.fontSize);
    size->setSuffix(QStringLiteral(" px @1080p"));
    connect(size, &QSpinBox::valueChanged, this, [this](int v) {
        m_settings.watermark.fontSize = v;
        emitChanged();
    });
    form->addRow(QStringLiteral("Size"), size);
    auto* opacity = makeSlider(5, 100, static_cast<int>(wm.opacity * 100), w);
    connect(opacity, &QSlider::valueChanged, this, [this](int v) {
        m_settings.watermark.opacity = v / 100.0;
        emitChanged();
    });
    form->addRow(QStringLiteral("Opacity"), opacity);
    return w;
}

} // namespace luma::app
