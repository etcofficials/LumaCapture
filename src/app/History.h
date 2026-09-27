#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace luma::app {

struct RecordingEntry {
    QString path;
    QDateTime created;
    double durationSeconds = 0;
    int width = 0, height = 0, fps = 0;
    qint64 sizeBytes = 0;
    bool exists = true; // refreshed in the background
};

// Recording history stored as JSON next to the settings (newest first, max 300).
class History {
public:
    explicit History(QString file);
    void load();
    void save() const;
    void add(const RecordingEntry& e);
    void remove(const QString& path);
    const QList<RecordingEntry>& entries() const { return m_entries; }
    void setEntries(QList<RecordingEntry> e) { m_entries = std::move(e); }

private:
    QString m_file;
    QList<RecordingEntry> m_entries;
};

} // namespace luma::app
