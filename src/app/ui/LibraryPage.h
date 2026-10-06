#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;

namespace luma::app {

class History;
class MediaImporter;
class SettingsStore;
class ThumbnailCache;

// Library: every recording (and imported video) with thumbnail, duration,
// resolution, frame rate, size and date. Search, sort, play, open with, show in
// folder, rename, details, delete (Recycle Bin), import and drag & drop.
// File operations run on worker threads.
class LibraryPage : public QWidget {
    Q_OBJECT
public:
    LibraryPage(History& history, ThumbnailCache& thumbs, MediaImporter& importer, SettingsStore& store,
                QWidget* parent = nullptr);

    void refresh();
    // The file being recorded right now cannot be renamed or deleted.
    void setRecordingFile(const QString& file) { m_recordingFile = file; }
    void importFiles(const QStringList& files);

signals:
    void historyEdited();
    void notify(int kind, const QString& text); // 0 info, 1 success, 2 warning, 3 error

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QString currentPath() const;
    void updateButtons();
    void play();
    void renameCurrent();
    void deleteCurrent();
    void showDetails();
    void showMenu(const QPoint& pos);

    History& m_history;
    ThumbnailCache& m_thumbs;
    MediaImporter& m_importer;
    SettingsStore& m_store;
    QListWidget* m_list = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_sort = nullptr;
    QLabel* m_count = nullptr;
    QPushButton* m_play = nullptr;
    QPushButton* m_open = nullptr;
    QPushButton* m_show = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_details = nullptr;
    QPushButton* m_delete = nullptr;
    QWidget* m_progressRow = nullptr;
    QProgressBar* m_progress = nullptr;
    QLabel* m_progressText = nullptr;
    QLabel* m_empty = nullptr;
    QString m_recordingFile;
};

} // namespace luma::app
