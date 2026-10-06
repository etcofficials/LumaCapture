#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace luma::app {

struct RecordingEntry {
    QString path;
    QDateTime created;
    double durationSeconds = 0;
    int width = 0, height = 0;
    double fps = 0;
    qint64 sizeBytes = 0;
    bool imported = false; // added with Import / drag & drop (not recorded by LumaCapture)
    bool exists = true;    // refreshed in the background
};

// Library contents stored as JSON in the data folder (newest first, max 500).
// Saving happens on the disk worker thread with a snapshot; the UI never waits.
class History {
public:
    explicit History(QString file);
    void load(); // startup only
    void add(const RecordingEntry& e);
    void remove(const QString& path);
    bool rename(const QString& oldPath, const QString& newPath);
    bool contains(const QString& path) const;
    const QList<RecordingEntry>& entries() const { return m_entries; }
    void setEntries(QList<RecordingEntry> e);

private:
    void save() const;

    QString m_file;
    QList<RecordingEntry> m_entries;
};

} // namespace luma::app
