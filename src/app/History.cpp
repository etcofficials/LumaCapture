#include "History.h"

#include "DiskWorker.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace luma::app {

History::History(QString file) : m_file(std::move(file)) {}

void History::load()
{
    m_entries.clear();
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        RecordingEntry e;
        e.path = o.value("path").toString();
        e.created = QDateTime::fromString(o.value("created").toString(), Qt::ISODate);
        e.durationSeconds = o.value("duration").toDouble();
        e.width = o.value("width").toInt();
        e.height = o.value("height").toInt();
        e.fps = o.value("fps").toDouble();
        e.sizeBytes = static_cast<qint64>(o.value("size").toDouble());
        e.imported = o.value("imported").toBool();
        if (!e.path.isEmpty())
            m_entries.append(e);
    }
}

void History::save() const
{
    QJsonArray arr;
    for (const RecordingEntry& e : m_entries) {
        QJsonObject o;
        o["path"] = e.path;
        o["created"] = e.created.toString(Qt::ISODate);
        o["duration"] = e.durationSeconds;
        o["width"] = e.width;
        o["height"] = e.height;
        o["fps"] = e.fps;
        o["size"] = static_cast<double>(e.sizeBytes);
        if (e.imported)
            o["imported"] = true;
        arr.append(o);
    }
    const QByteArray json = QJsonDocument(arr).toJson();
    const QString file = m_file;
    diskworker::post(QStringLiteral("history"), [json, file] {
        QSaveFile f(file); // atomic replace: a crash never leaves a half-written history
        if (f.open(QIODevice::WriteOnly)) {
            f.write(json);
            f.commit();
        }
    });
}

void History::add(const RecordingEntry& e)
{
    for (int i = m_entries.size() - 1; i >= 0; --i)
        if (m_entries[i].path.compare(e.path, Qt::CaseInsensitive) == 0)
            m_entries.removeAt(i);
    m_entries.prepend(e);
    while (m_entries.size() > 500)
        m_entries.removeLast();
    save();
}

void History::remove(const QString& path)
{
    for (int i = m_entries.size() - 1; i >= 0; --i)
        if (m_entries[i].path.compare(path, Qt::CaseInsensitive) == 0)
            m_entries.removeAt(i);
    save();
}

bool History::rename(const QString& oldPath, const QString& newPath)
{
    bool found = false;
    for (RecordingEntry& e : m_entries) {
        if (e.path.compare(oldPath, Qt::CaseInsensitive) == 0) {
            e.path = newPath;
            found = true;
        }
    }
    if (found)
        save();
    return found;
}

bool History::contains(const QString& path) const
{
    for (const RecordingEntry& e : m_entries)
        if (e.path.compare(path, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

void History::setEntries(QList<RecordingEntry> e)
{
    m_entries = std::move(e); // only the "exists" flags change; nothing to save
}

} // namespace luma::app
