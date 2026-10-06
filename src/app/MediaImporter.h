#pragma once

#include "History.h"

#include <QObject>
#include <QStringList>

#include <atomic>
#include <memory>

namespace luma::app {

// Adds existing video files to the Library (Import button / drag & drop).
// Each file is validated on a worker thread: extension, existence, and FFmpeg must
// be able to open it and find a video stream. Optionally the files are copied into
// the recordings folder first, with progress and cancel. The UI never waits.
class MediaImporter : public QObject {
    Q_OBJECT
public:
    explicit MediaImporter(QObject* parent = nullptr);
    ~MediaImporter() override;

    static QStringList extensions();             // "mkv", "mp4", ...
    static QString fileDialogFilter();           // for QFileDialog
    static bool hasSupportedExtension(const QString& path);

    bool busy() const { return m_busy; }
    void import(const QStringList& files, bool copy, const QString& targetDir);
    void cancel();

signals:
    void progress(int fileIndex, int fileCount, double fraction, const QString& name);
    // Valid entries to add to the library; problems are human-readable lines.
    void finished(const QList<luma::app::RecordingEntry>& entries, const QStringList& problems, bool cancelled);

private:
    std::shared_ptr<std::atomic<bool>> m_cancel;
    bool m_busy = false;
};

} // namespace luma::app
