#include "MediaImporter.h"

#include "mux/Muxer.h"
#include "util/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

namespace luma::app {

MediaImporter::MediaImporter(QObject* parent) : QObject(parent) {}

MediaImporter::~MediaImporter()
{
    if (m_cancel)
        m_cancel->store(true); // a running copy stops at its next chunk
}

QStringList MediaImporter::extensions()
{
    return {QStringLiteral("mkv"), QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("m4v"),
            QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("wmv"), QStringLiteral("flv"),
            QStringLiteral("ts"),  QStringLiteral("mts"), QStringLiteral("m2ts")};
}

QString MediaImporter::fileDialogFilter()
{
    QStringList patterns;
    for (const QString& e : extensions())
        patterns << QStringLiteral("*.") + e;
    return QStringLiteral("Videos (%1)").arg(patterns.join(QLatin1Char(' ')));
}

bool MediaImporter::hasSupportedExtension(const QString& path)
{
    return extensions().contains(QFileInfo(path).suffix().toLower());
}

void MediaImporter::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

void MediaImporter::import(const QStringList& files, bool copy, const QString& targetDir)
{
    if (m_busy || files.isEmpty())
        return;
    m_busy = true;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancelFlag = m_cancel;
    QPointer<MediaImporter> self(this);

    (void)QtConcurrent::run([self, files, copy, targetDir, cancelFlag] {
        QList<RecordingEntry> entries;
        QStringList problems;
        auto report = [self](int i, int n, double f, const QString& name) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, i, n, f, name] {
                if (self)
                    emit self->progress(i, n, f, name);
            });
        };
        for (int i = 0; i < files.size() && !cancelFlag->load(); ++i) {
            const QString src = files[i];
            const QFileInfo fi(src);
            report(i, static_cast<int>(files.size()), 0.0, fi.fileName());
            if (!fi.exists() || !fi.isFile()) {
                problems << QStringLiteral("%1: file not found").arg(fi.fileName());
                continue;
            }
            if (!hasSupportedExtension(src)) {
                problems << QStringLiteral("%1: not a supported video type (%2)")
                                .arg(fi.fileName(), extensions().join(QStringLiteral(", ")));
                continue;
            }
            mux::MediaInfo info;
            if (!mux::probeMedia(src.toStdWString(), info) || info.width <= 0) {
                problems << QStringLiteral("%1: FFmpeg cannot read a video stream in this file").arg(fi.fileName());
                continue;
            }
            QString path = src;
            if (copy) {
                QDir().mkpath(targetDir);
                QString dst = QDir(targetDir).filePath(fi.fileName());
                for (int n = 2; QFileInfo::exists(dst); ++n)
                    dst = QDir(targetDir).filePath(QStringLiteral("%1_%2.%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
                QFile in(src), out(dst);
                if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
                    problems << QStringLiteral("%1: cannot copy (%2)").arg(fi.fileName(), out.errorString());
                    continue;
                }
                const qint64 total = in.size();
                qint64 done = 0;
                QByteArray buf(4 << 20, Qt::Uninitialized);
                QElapsedTimer tick;
                tick.start();
                bool ok = true;
                while (!in.atEnd()) {
                    if (cancelFlag->load()) {
                        ok = false;
                        break;
                    }
                    const qint64 n = in.read(buf.data(), buf.size());
                    if (n < 0 || out.write(buf.constData(), n) != n) {
                        problems << QStringLiteral("%1: copy failed (%2)").arg(fi.fileName(), out.errorString());
                        ok = false;
                        break;
                    }
                    done += n;
                    if (tick.elapsed() > 200 && total > 0) { // at most 5 progress updates per second
                        tick.restart();
                        report(i, static_cast<int>(files.size()), static_cast<double>(done) / total, fi.fileName());
                    }
                }
                out.close();
                if (!ok) {
                    QFile::remove(dst);
                    continue;
                }
                path = dst;
            }
            RecordingEntry e;
            e.path = path;
            e.created = QFileInfo(path).lastModified();
            e.durationSeconds = info.durationSeconds;
            e.width = info.width;
            e.height = info.height;
            e.fps = info.fps;
            e.sizeBytes = QFileInfo(path).size();
            e.imported = true;
            entries.append(e);
        }
        const bool cancelled = cancelFlag->load();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, entries, problems, cancelled] {
            if (!self)
                return;
            self->m_busy = false;
            log::info("Import: {} added, {} problems{}", entries.size(), problems.size(), cancelled ? " (cancelled)" : "");
            emit self->finished(entries, problems, cancelled);
        });
    });
}

} // namespace luma::app
