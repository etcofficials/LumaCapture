#include "ui/LibraryPage.h"

#include "History.h"
#include "Icons.h"
#include "MediaImporter.h"
#include "SettingsStore.h"
#include "Theme.h"
#include "ThumbnailCache.h"
#include "WinUtil.h"
#include "mux/Muxer.h"
#include "ui/RecordingDelegate.h"
#include "ui/UiKit.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace luma::app {
namespace {

QStringList droppedVideos(const QMimeData* mime)
{
    QStringList files;
    if (!mime || !mime->hasUrls())
        return files;
    for (const QUrl& u : mime->urls())
        if (u.isLocalFile() && MediaImporter::hasSupportedExtension(u.toLocalFile()))
            files << u.toLocalFile();
    return files;
}

QString detailsHtml(const QString& path, const mux::MediaDetails& d)
{
    QString html = QStringLiteral("<b>%1</b><br><span>%2</span><table cellspacing='0' cellpadding='2' style='margin-top:8px'>")
                       .arg(QFileInfo(path).fileName().toHtmlEscaped(),
                            QDir::toNativeSeparators(QFileInfo(path).path()).toHtmlEscaped());
    auto row = [&html](const QString& k, const QString& v) {
        html += QStringLiteral("<tr><td style='padding-right:14px'>%1</td><td>%2</td></tr>").arg(k, v.toHtmlEscaped());
    };
    row(QStringLiteral("Container"), QString::fromStdString(d.container));
    row(QStringLiteral("Duration"), formatDuration(d.durationSeconds) + QStringLiteral("  (%1 s)").arg(d.durationSeconds, 0, 'f', 2));
    if (d.bitRate > 0)
        row(QStringLiteral("Overall bit rate"), QStringLiteral("%1 Mbit/s").arg(d.bitRate / 1e6, 0, 'f', 2));
    for (const auto& v : d.video) {
        row(QStringLiteral("Video"), QStringLiteral("%1 (%2)").arg(QString::fromStdString(v.codec), QString::fromStdString(v.profile)));
        row(QStringLiteral("Resolution"), QStringLiteral("%1 × %2").arg(v.width).arg(v.height));
        row(QStringLiteral("Frame rate"), QStringLiteral("%1 FPS average, %2 FPS base").arg(v.avgFps, 0, 'f', 3).arg(v.baseFps, 0, 'f', 3));
        if (v.frames > 0)
            row(QStringLiteral("Frames"), QString::number(v.frames));
        row(QStringLiteral("Pixel format"), QString::fromStdString(v.pixelFormat));
        row(QStringLiteral("Colour"), QStringLiteral("%1, %2 range").arg(QString::fromStdString(v.colorSpace),
                                                                          QString::fromStdString(v.colorRange)));
    }
    int n = 1;
    for (const auto& a : d.audio) {
        QString text = QStringLiteral("%1, %2 Hz, %3 ch").arg(QString::fromStdString(a.codec)).arg(a.sampleRate).arg(a.channels);
        if (a.bitRate > 0)
            text += QStringLiteral(", %1 kbit/s").arg(a.bitRate / 1000);
        if (!a.title.empty())
            text += QStringLiteral(" - ") + QString::fromStdString(a.title);
        row(QStringLiteral("Audio %1").arg(n++), text);
    }
    if (d.audio.empty())
        row(QStringLiteral("Audio"), QStringLiteral("none"));
    html += QStringLiteral("</table>");
    return html;
}

} // namespace

LibraryPage::LibraryPage(History& history, ThumbnailCache& thumbs, MediaImporter& importer, SettingsStore& store,
                         QWidget* parent)
    : QWidget(parent), m_history(history), m_thumbs(thumbs), m_importer(importer), m_store(store)
{
    setAcceptDrops(true);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 12);
    root->setSpacing(10);

    // Toolbar
    auto* bar = new QHBoxLayout;
    bar->setSpacing(8);
    auto* title = new QLabel(QStringLiteral("Library"), this);
    title->setObjectName("PageTitle");
    bar->addWidget(title);
    m_count = ui::dim(QString(), this);
    bar->addWidget(m_count);
    bar->addStretch();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search recordings"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(makeIcon(IconId::Search, currentPalette().textDim), QLineEdit::LeadingPosition);
    m_search->setMinimumWidth(220);
    m_search->setAccessibleName(QStringLiteral("Search recordings"));
    bar->addWidget(m_search);
    m_sort = new QComboBox(this);
    m_sort->addItem(QStringLiteral("Newest first"), static_cast<int>(LibrarySort::Newest));
    m_sort->addItem(QStringLiteral("Oldest first"), static_cast<int>(LibrarySort::Oldest));
    m_sort->addItem(QStringLiteral("Largest first"), static_cast<int>(LibrarySort::Size));
    m_sort->addItem(QStringLiteral("Longest first"), static_cast<int>(LibrarySort::Duration));
    m_sort->setAccessibleName(QStringLiteral("Sort recordings"));
    bar->addWidget(m_sort);
    auto* copyBox = new QCheckBox(QStringLiteral("Copy imports into the recordings folder"), this);
    copyBox->setChecked(store.get().libraryCopyOnImport);
    copyBox->setToolTip(QStringLiteral("Off: imported videos stay where they are and are only listed here"));
    bar->addWidget(copyBox);
    auto* import = new QPushButton(makeIcon(IconId::Import, Qt::white), QStringLiteral("Import..."), this);
    import->setObjectName("Primary");
    import->setToolTip(QStringLiteral("Add existing videos to the library (you can also drag files onto this page)"));
    bar->addWidget(import);
    auto* folder = new QPushButton(makeIcon(IconId::Folder, currentPalette().text), QStringLiteral("Open folder"), this);
    folder->setToolTip(QStringLiteral("Open the recordings folder in Explorer"));
    bar->addWidget(folder);
    root->addLayout(bar);

    // Import progress
    m_progressRow = new QWidget(this);
    auto* pr = new QHBoxLayout(m_progressRow);
    pr->setContentsMargins(0, 0, 0, 0);
    m_progressText = ui::dim(QString(), m_progressRow);
    m_progress = new QProgressBar(m_progressRow);
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(8);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), m_progressRow);
    pr->addWidget(m_progressText);
    pr->addWidget(m_progress, 1);
    pr->addWidget(cancel);
    m_progressRow->hide();
    root->addWidget(m_progressRow);

    // Grid
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setMouseTracking(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setItemDelegate(new RecordingDelegate(thumbs, QSize(216, 178), m_list));
    m_list->setAccessibleName(QStringLiteral("Recordings"));
    m_list->setAcceptDrops(false); // drops are handled by the page
    root->addWidget(m_list, 1);
    m_empty = new QLabel(QStringLiteral("No recordings yet.\nPress Record on the Record page, or drag videos here / "
                                        "use Import to add existing ones."),
                         this);
    m_empty->setObjectName("Dim");
    m_empty->setAlignment(Qt::AlignCenter);
    root->addWidget(m_empty, 1);

    // Actions
    auto* actions = new QHBoxLayout;
    actions->setSpacing(8);
    const Palette& pal = currentPalette();
    m_play = new QPushButton(makeIcon(IconId::Play, pal.text), QStringLiteral("Play"), this);
    m_open = new QPushButton(makeIcon(IconId::External, pal.text), QStringLiteral("Open with..."), this);
    m_show = new QPushButton(makeIcon(IconId::Folder, pal.text), QStringLiteral("Show in folder"), this);
    m_rename = new QPushButton(makeIcon(IconId::Edit, pal.text), QStringLiteral("Rename..."), this);
    m_details = new QPushButton(makeIcon(IconId::Info, pal.text), QStringLiteral("Details..."), this);
    m_delete = new QPushButton(makeIcon(IconId::Trash, pal.record), QStringLiteral("Delete..."), this);
    m_delete->setObjectName("Danger");
    m_details->setToolTip(QStringLiteral("Actual resolution, frame rate, codec, pixel format and audio of the file"));
    m_delete->setToolTip(QStringLiteral("Move the file to the Recycle Bin"));
    for (QPushButton* b : {m_play, m_open, m_show, m_rename, m_details})
        actions->addWidget(b);
    actions->addStretch();
    actions->addWidget(m_delete);
    root->addLayout(actions);

    connect(m_search, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(m_sort, &QComboBox::activated, this, [this](int i) {
        const auto sort = static_cast<LibrarySort>(m_sort->itemData(i).toInt());
        m_store.edit(SettingsStore::Ui, [sort](AppSettings& s) { s.librarySort = sort; });
        refresh();
    });
    connect(copyBox, &QCheckBox::toggled, this,
            [this](bool on) { m_store.edit(SettingsStore::Ui, [on](AppSettings& s) { s.libraryCopyOnImport = on; }); });
    connect(import, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, QStringLiteral("Import videos"), QString(),
                                                                MediaImporter::fileDialogFilter());
        importFiles(files);
    });
    connect(folder, &QPushButton::clicked, this, [this] {
        const QString dir = m_store.get().outputDir;
        (void)QtConcurrent::run([dir] { QDir().mkpath(dir); });
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(cancel, &QPushButton::clicked, &m_importer, &MediaImporter::cancel);
    connect(&m_importer, &MediaImporter::progress, this, [this](int i, int n, double f, const QString& name) {
        m_progressRow->show();
        m_progress->setValue(static_cast<int>((i + f) / std::max(1, n) * 1000));
        m_progressText->setText(QStringLiteral("Importing %1 of %2: %3").arg(i + 1).arg(n).arg(name));
    });
    connect(&m_importer, &MediaImporter::finished, this,
            [this](const QList<RecordingEntry>& entries, const QStringList& problems, bool cancelled) {
                m_progressRow->hide();
                for (const RecordingEntry& e : entries)
                    m_history.add(e);
                refresh();
                emit historyEdited();
                if (!entries.isEmpty())
                    emit notify(1, QStringLiteral("Imported %1 video(s) into the library.").arg(entries.size()));
                if (!problems.isEmpty())
                    emit notify(2, QStringLiteral("Not imported:<br>%1").arg(problems.join(QStringLiteral("<br>")).toHtmlEscaped()));
                else if (cancelled)
                    emit notify(0, QStringLiteral("Import cancelled."));
            });
    connect(&m_thumbs, &ThumbnailCache::thumbnailReady, m_list->viewport(), [this] { m_list->viewport()->update(); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this] { updateButtons(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { play(); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &LibraryPage::showMenu);
    connect(m_play, &QPushButton::clicked, this, &LibraryPage::play);
    connect(m_open, &QPushButton::clicked, this, [this] {
        const QString f = currentPath();
        if (!f.isEmpty())
            openWithDialog(f);
    });
    connect(m_show, &QPushButton::clicked, this, [this] {
        const QString f = currentPath();
        if (!f.isEmpty())
            showInExplorer(f);
    });
    connect(m_rename, &QPushButton::clicked, this, &LibraryPage::renameCurrent);
    connect(m_details, &QPushButton::clicked, this, &LibraryPage::showDetails);
    connect(m_delete, &QPushButton::clicked, this, &LibraryPage::deleteCurrent);
    refresh();
}

QString LibraryPage::currentPath() const
{
    const QListWidgetItem* it = m_list->currentItem();
    return it ? it->data(roles::Path).toString() : QString();
}

void LibraryPage::refresh()
{
    const QString keep = currentPath();
    const AppSettings& s = m_store.get();
    ui::selectData(m_sort, static_cast<int>(s.librarySort));
    QList<RecordingEntry> entries = m_history.entries();
    const QString needle = m_search->text().trimmed();
    if (!needle.isEmpty())
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&needle](const RecordingEntry& e) {
                                         return !QFileInfo(e.path).fileName().contains(needle, Qt::CaseInsensitive);
                                     }),
                      entries.end());
    switch (s.librarySort) {
    case LibrarySort::Newest:
        std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.created > b.created; });
        break;
    case LibrarySort::Oldest:
        std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.created < b.created; });
        break;
    case LibrarySort::Size:
        std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.sizeBytes > b.sizeBytes; });
        break;
    case LibrarySort::Duration:
        std::stable_sort(entries.begin(), entries.end(),
                         [](const auto& a, const auto& b) { return a.durationSeconds > b.durationSeconds; });
        break;
    }
    m_list->setUpdatesEnabled(false);
    m_list->clear();
    for (const RecordingEntry& e : entries) {
        auto* it = new QListWidgetItem(m_list);
        fillRecordingItem(it, e);
        if (e.path == keep)
            m_list->setCurrentItem(it);
    }
    m_list->setUpdatesEnabled(true);
    m_count->setText(entries.size() == m_history.entries().size()
                         ? QStringLiteral("%1 videos").arg(entries.size())
                         : QStringLiteral("%1 of %2 videos").arg(entries.size()).arg(m_history.entries().size()));
    const bool empty = m_history.entries().isEmpty();
    m_empty->setVisible(empty);
    m_list->setVisible(!empty);
    updateButtons();
}

void LibraryPage::updateButtons()
{
    const QListWidgetItem* it = m_list->currentItem();
    const bool has = it != nullptr;
    const bool exists = has && !it->data(roles::Missing).toBool();
    for (QPushButton* b : {m_play, m_open, m_show, m_rename, m_details})
        b->setEnabled(exists);
    m_delete->setEnabled(has);
}

void LibraryPage::play()
{
    const QString f = currentPath();
    if (!f.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(f));
}

void LibraryPage::importFiles(const QStringList& files)
{
    if (files.isEmpty())
        return;
    if (m_importer.busy()) {
        emit notify(2, QStringLiteral("An import is already running."));
        return;
    }
    m_progress->setValue(0);
    m_progressText->setText(QStringLiteral("Checking %1 file(s)...").arg(files.size()));
    m_progressRow->show();
    const AppSettings& s = m_store.get();
    m_importer.import(files, s.libraryCopyOnImport, s.outputDir);
}

void LibraryPage::renameCurrent()
{
    const QString path = currentPath();
    if (path.isEmpty())
        return;
    if (path.compare(m_recordingFile, Qt::CaseInsensitive) == 0) {
        emit notify(2, QStringLiteral("This file is being recorded right now."));
        return;
    }
    const QFileInfo fi(path);
    bool ok = false;
    QString name = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("New name:"), QLineEdit::Normal,
                                         fi.completeBaseName(), &ok);
    if (!ok)
        return;
    name = sanitizeFileName(name.trimmed());
    if (name.isEmpty() || name == fi.completeBaseName())
        return;
    const QString target = fi.dir().filePath(name + QLatin1Char('.') + fi.suffix());
    QPointer<LibraryPage> self(this);
    (void)QtConcurrent::run([self, path, target] {
        QString error;
        if (QFileInfo::exists(target))
            error = QStringLiteral("A file named \"%1\" already exists.").arg(QFileInfo(target).fileName());
        else if (!QFile::rename(path, target))
            error = QStringLiteral("The file could not be renamed. It may be open in another program.");
        QMetaObject::invokeMethod(qApp, [self, path, target, error] {
            if (!self)
                return;
            if (!error.isEmpty()) {
                emit self->notify(3, error);
                return;
            }
            self->m_history.rename(path, target);
            self->m_thumbs.forget(path);
            self->refresh();
            emit self->historyEdited();
        });
    });
}

void LibraryPage::deleteCurrent()
{
    const QListWidgetItem* it = m_list->currentItem();
    if (!it)
        return;
    const QString path = it->data(roles::Path).toString();
    if (path.compare(m_recordingFile, Qt::CaseInsensitive) == 0) {
        emit notify(2, QStringLiteral("This file is being recorded right now."));
        return;
    }
    const bool missing = it->data(roles::Missing).toBool();
    if (missing) {
        m_history.remove(path);
        refresh();
        emit historyEdited();
        return;
    }
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Delete recording"),
                    QStringLiteral("Move \"%1\" to the Recycle Bin?\n\n%2")
                        .arg(QFileInfo(path).fileName(), QDir::toNativeSeparators(QFileInfo(path).path())),
                    QMessageBox::NoButton, this);
    auto* del = box.addButton(QStringLiteral("Move to Recycle Bin"), QMessageBox::DestructiveRole);
    auto* keepFile = box.addButton(QStringLiteral("Remove from library only"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == keepFile) {
        m_history.remove(path);
        refresh();
        emit historyEdited();
        return;
    }
    if (box.clickedButton() != del)
        return;
    QPointer<LibraryPage> self(this);
    (void)QtConcurrent::run([self, path] {
        const bool ok = !QFileInfo::exists(path) || QFile::moveToTrash(path);
        QMetaObject::invokeMethod(qApp, [self, path, ok] {
            if (!self)
                return;
            if (!ok) {
                emit self->notify(3, QStringLiteral("The file could not be moved to the Recycle Bin. It may be open in "
                                                    "another program."));
                return;
            }
            self->m_history.remove(path);
            self->m_thumbs.forget(path);
            self->refresh();
            emit self->historyEdited();
        });
    });
}

void LibraryPage::showDetails()
{
    const QString path = currentPath();
    if (path.isEmpty())
        return;
    QPointer<LibraryPage> self(this);
    (void)QtConcurrent::run([self, path] {
        mux::MediaDetails d;
        std::string error;
        const bool ok = mux::probeDetails(path.toStdWString(), d, &error);
        const QString html = ok ? detailsHtml(path, d) : QString();
        const QString err = QString::fromStdString(error);
        QMetaObject::invokeMethod(qApp, [self, html, err] {
            if (!self)
                return;
            if (html.isEmpty()) {
                emit self->notify(3, QStringLiteral("The file could not be read: %1").arg(err));
                return;
            }
            QMessageBox box(QMessageBox::NoIcon, QStringLiteral("Recording details"), html, QMessageBox::Close, self);
            box.setTextFormat(Qt::RichText);
            box.setTextInteractionFlags(Qt::TextSelectableByMouse);
            box.exec();
        });
    });
}

void LibraryPage::showMenu(const QPoint& pos)
{
    QListWidgetItem* it = m_list->itemAt(pos);
    if (!it)
        return;
    m_list->setCurrentItem(it);
    QMenu menu(this);
    const bool exists = !it->data(roles::Missing).toBool();
    menu.addAction(QStringLiteral("Play"), this, &LibraryPage::play)->setEnabled(exists);
    menu.addAction(QStringLiteral("Open with..."), this, [this] { openWithDialog(currentPath()); })->setEnabled(exists);
    menu.addAction(QStringLiteral("Show in folder"), this, [this] { showInExplorer(currentPath()); })->setEnabled(exists);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Rename..."), this, &LibraryPage::renameCurrent)->setEnabled(exists);
    menu.addAction(QStringLiteral("Details..."), this, &LibraryPage::showDetails)->setEnabled(exists);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Delete..."), this, &LibraryPage::deleteCurrent);
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void LibraryPage::dragEnterEvent(QDragEnterEvent* e)
{
    if (!droppedVideos(e->mimeData()).isEmpty())
        e->acceptProposedAction();
}

void LibraryPage::dropEvent(QDropEvent* e)
{
    const QStringList files = droppedVideos(e->mimeData());
    if (files.isEmpty())
        return;
    e->acceptProposedAction();
    importFiles(files);
}

} // namespace luma::app
