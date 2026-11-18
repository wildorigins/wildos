#include "zipjob.h"

#include <KZip>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QtConcurrentRun>

namespace
{
struct Item {
    QString path;
    QString name;
    bool dir = false;
    qint64 size = 0;
};
}

ZipJob::ZipJob(const QList<QUrl> &sources, const QUrl &target, QObject *parent)
    : KJob(parent)
    , m_sources(sources)
    , m_target(target)
{
    setCapabilities(KJob::Killable);
}

void ZipJob::start()
{
    // Everything to pack, with the name it gets inside the archive: a
    // folder keeps its own name as the top level, as in File Explorer.
    QList<Item> items;
    qint64 total = 0;
    for (const QUrl &url : std::as_const(m_sources)) {
        const QFileInfo info(url.toLocalFile());
        if (info.isDir()) {
            items.append({info.absoluteFilePath(), info.fileName(), true, 0});
            QDirIterator it(info.absoluteFilePath(), QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QFileInfo entry(it.next());
                const QString name = info.fileName() + QLatin1Char('/') + QDir(info.absoluteFilePath()).relativeFilePath(entry.absoluteFilePath());
                items.append({entry.absoluteFilePath(), name, entry.isDir(), entry.isDir() ? 0 : entry.size()});
                total += entry.isDir() ? 0 : entry.size();
            }
        } else if (info.exists()) {
            items.append({info.absoluteFilePath(), info.fileName(), false, info.size()});
            total += info.size();
        }
    }

    setTotalAmount(KJob::Bytes, quint64(total));
    setTotalAmount(KJob::Files, quint64(items.size()));
    const QString name = QFileInfo(m_target.toLocalFile()).fileName();
    Q_EMIT description(this, QCoreApplication::translate("ZipJob", "Compressing"), qMakePair(QCoreApplication::translate("ZipJob", "To"), name));

    const QString target = m_target.toLocalFile();
    auto cancel = m_cancel;
    QPointer<ZipJob> self(this);
    (void)QtConcurrent::run([items, target, cancel, self] {
        KZip zip(target);
        QString error;
        if (!zip.open(QIODevice::WriteOnly)) {
            error = QCoreApplication::translate("ZipJob", "Wild OS could not create %1 here. Check there is space and you can save in this folder.").arg(QFileInfo(target).fileName());
        } else {
            qint64 done = 0;
            qulonglong files = 0;
            for (const Item &item : items) {
                if (*cancel) {
                    break;
                }
                const bool ok = item.dir ? zip.writeDir(item.name) : zip.addLocalFile(item.path, item.name);
                if (!ok) {
                    error = QCoreApplication::translate("ZipJob", "%1 could not be added. It may be open in another app or you may not be allowed to read it.").arg(QFileInfo(item.path).fileName());
                    break;
                }
                done += item.size;
                ++files;
                QMetaObject::invokeMethod(qApp, [self, done, files] {
                    if (self) {
                        self->setProcessedAmount(KJob::Bytes, quint64(done));
                        self->setProcessedAmount(KJob::Files, files);
                    }
                });
            }
            zip.close();
        }
        const bool cancelled = *cancel;
        if (!error.isEmpty() || cancelled) {
            QFile::remove(target);
        }
        QMetaObject::invokeMethod(qApp, [self, error, cancelled] {
            if (!self || self->isFinished()) {
                return;
            }
            if (cancelled) {
                self->setError(KJob::KilledJobError);
            } else if (!error.isEmpty()) {
                self->setError(KJob::UserDefinedError);
                self->setErrorText(error);
            }
            self->emitResult();
        });
    });
}

bool ZipJob::doKill()
{
    // The worker sees the flag between files and removes the half-written
    // archive; the job itself ends now.
    *m_cancel = true;
    return true;
}
