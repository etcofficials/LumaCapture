#include "History.h"

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
        e.fps = o.value("fps").toInt();
        e.sizeBytes = static_cast<qint64>(o.value("size").toDouble());
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
        arr.append(o);
    }
    QSaveFile f(m_file); // atomic replace: a crash never leaves a half-written history
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson());
        f.commit();
    }
}

void History::add(const RecordingEntry& e)
{
    remove(e.path);
    m_entries.prepend(e);
    while (m_entries.size() > 300)
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

} // namespace luma::app
