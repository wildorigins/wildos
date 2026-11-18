#include "background.h"
#include "bookmarks.h"

#include <KUiServerV2JobTracker>

#include <PackageKit/Daemon>
#include <PackageKit/Transaction>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

using PackageKit::Daemon;
using PackageKit::Transaction;

namespace
{
// Windows writes these into every folder for Explorer's own use; they would
// only clutter Files.
const QStringList windowsClutter = {QStringLiteral("desktop.ini"), QStringLiteral("thumbs.db"), QStringLiteral("ehthumbs.db")};

QString joinNames(const QStringList &names)
{
    if (names.size() <= 1) {
        return names.value(0);
    }
    return QStringList(names.mid(0, names.size() - 1)).join(QCoreApplication::translate("Background", ", ")) + QCoreApplication::translate("Background", " and ") + names.last();
}

// "Report.docx" becomes "Report (from Windows).docx", then
// "Report (from Windows 2).docx" and so on.
QString freeName(const QString &path)
{
    const QFileInfo info(path);
    const QString base = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
    const QString suffix = info.completeBaseName().isEmpty() || info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    for (int n = 1;; ++n) {
        const QString tag = n == 1 ? QStringLiteral(" (from Windows)") : QStringLiteral(" (from Windows %1)").arg(n);
        const QString candidate = info.absolutePath() + QLatin1Char('/') + base + tag + suffix;
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

void unmount(const QString &kname)
{
    if (kname.isEmpty()) {
        return;
    }
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.UDisks2"), QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + kname,
                                                  QStringLiteral("org.freedesktop.UDisks2.Filesystem"), QStringLiteral("Unmount"));
    message << QVariantMap();
    QDBusConnection::systemBus().call(message, QDBus::Block, 30000);
}
}

namespace Background
{
void notify(const QString &summary, const QString &body, const QString &icon)
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                  QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    message << QStringLiteral("Wild OS") << uint(0) << icon << summary << body << QStringList() << QVariantMap() << int(-1);
    QDBusConnection::sessionBus().call(message, QDBus::Block, 5000);
}

int runCopy(const QString &jobFile)
{
    QFile file(jobFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return 1;
    }
    const QJsonObject job = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    auto tracker = new KUiServerV2JobTracker(qApp);
    auto copy = new CopyJob(job, qApp);
    tracker->registerJob(copy);
    int code = 0;
    QObject::connect(copy, &KJob::result, qApp, [copy, jobFile, job, &code] {
        unmount(job.value(QStringLiteral("unmount")).toString());
        QFile::remove(jobFile);

        const QStringList names = copy->folderNames();
        QStringList lines;
        if (copy->error() == KJob::KilledJobError) {
            notify(QCoreApplication::translate("Background", "Copying from Windows stopped"),
                   QCoreApplication::translate("Background", "Anything already copied is in your Home folder. Nothing was changed on Windows."));
            code = 1;
            return;
        }
        if (!names.isEmpty()) {
            lines << (names.size() == 1 ? QCoreApplication::translate("Background", "%1 is in your Home folder.") : QCoreApplication::translate("Background", "%1 are in your Home folder.")).arg(joinNames(names));
        }
        if (copy->bookmarksAdded() > 0 && copy->historyAdded() > 0) {
            lines << QCoreApplication::translate("Background", "Your bookmarks are in Brave, on the bookmarks bar, and so is your history.");
        } else if (copy->bookmarksAdded() > 0) {
            lines << QCoreApplication::translate("Background", "Your bookmarks are in Brave, on the bookmarks bar.");
        } else if (copy->historyAdded() > 0) {
            lines << QCoreApplication::translate("Background", "Your browsing history is in Brave.");
        }
        if (copy->bookmarksAdded() == -1 || copy->historyAdded() == -1) {
            lines << QCoreApplication::translate("Background", "Some of your browser's bookmarks or history couldn't be added to Brave.");
        }
        // Edge and Chrome lock saved passwords to the Windows account, so
        // they can't be read from here.
        if (copy->bookmarksAdded() != -2) {
            lines << QCoreApplication::translate("Background", "Saved passwords can't be copied from Windows: export them in Edge or Chrome on Windows, "
                                    "or use a password manager you can sign in to here.");
        }
        if (copy->failures() > 0) {
            lines << (copy->failures() == 1 ? QCoreApplication::translate("Background", "1 file couldn't be read from Windows and was left out.")
                                            : QCoreApplication::translate("Background", "%1 files couldn't be read from Windows and were left out.").arg(copy->failures()));
        }
        notify(copy->failures() > 0 ? QCoreApplication::translate("Background", "Most of your files are here") : QCoreApplication::translate("Background", "Your files from Windows are here"),
               lines.join(QLatin1Char(' ')));
    });
    QObject::connect(copy, &KJob::result, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    copy->start();
    QCoreApplication::exec();
    return code;
}

int runInstall(const QString &jobFile)
{
    QFile file(jobFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return 1;
    }
    const QJsonArray apps = QJsonDocument::fromJson(file.readAll()).array();
    file.close();
    QFile::remove(jobFile);

    auto tracker = new KUiServerV2JobTracker(qApp);
    auto install = new InstallJob(apps, qApp);
    tracker->registerJob(install);
    QObject::connect(install, &KJob::result, qApp, [install] {
        const QStringList done = install->installed();
        const QStringList failed = install->failed();
        if (failed.isEmpty()) {
            notify(QCoreApplication::translate("Background", "Your apps are ready"),
                   (done.size() == 1 ? QCoreApplication::translate("Background", "%1 is installed. Find them in Start.") : QCoreApplication::translate("Background", "%1 are installed. Find them in Start.")).arg(joinNames(done)));
        } else {
            QString body = QCoreApplication::translate("Background", "%1 couldn't be installed. Try again from the Store.").arg(joinNames(failed));
            if (!done.isEmpty()) {
                body = (done.size() == 1 ? QCoreApplication::translate("Background", "%1 is installed. %2") : QCoreApplication::translate("Background", "%1 are installed. %2")).arg(joinNames(done), body);
            }
            notify(QCoreApplication::translate("Background", "Some apps weren't installed"), body);
        }
    });
    QObject::connect(install, &KJob::result, qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    install->start();
    return QCoreApplication::exec();
}
}

CopyJob::CopyJob(const QJsonObject &job, QObject *parent)
    : KJob(parent)
    , m_job(job)
{
    setCapabilities(KJob::Killable);
    const QJsonArray folders = job.value(QStringLiteral("folders")).toArray();
    for (const QJsonValue &value : folders) {
        m_total += value.toObject().value(QStringLiteral("bytes")).toInteger();
        m_names << value.toObject().value(QStringLiteral("name")).toString();
    }
}

void CopyJob::start()
{
    Q_EMIT description(this, QCoreApplication::translate("Background", "Copying your files from Windows"),
                       {QCoreApplication::translate("Background", "From"), m_job.value(QStringLiteral("drive")).toString(QStringLiteral("Windows"))},
                       {QCoreApplication::translate("Background", "To"), QCoreApplication::translate("Background", "Your Home folder")});
    setTotalAmount(KJob::Bytes, qMax<qint64>(m_total, 0));
    setProcessedAmount(KJob::Bytes, 0);

    QThread *thread = QThread::create([this] {
        work();
    });
    connect(thread, &QThread::finished, this, [this, thread] {
        thread->deleteLater();
        if (m_cancel) {
            return;
        }
        importBookmarks();
    });
    thread->start(QThread::LowPriority);
}

bool CopyJob::doKill()
{
    m_cancel = true;
    return true;
}

void CopyJob::work()
{
    const QJsonArray folders = m_job.value(QStringLiteral("folders")).toArray();
    QElapsedTimer sinceUpdate;
    QElapsedTimer sinceStart;
    sinceUpdate.start();
    sinceStart.start();
    qint64 copied = 0;
    int failures = 0;
    QByteArray buffer(4 * 1024 * 1024, Qt::Uninitialized);

    const auto report = [this, &copied, &sinceStart] {
        const qint64 now = copied;
        const qint64 elapsed = std::max<qint64>(sinceStart.elapsed(), 1);
        QMetaObject::invokeMethod(this, [this, now, elapsed] {
            m_copied = now;
            setProcessedAmount(KJob::Bytes, now);
            emitSpeed(ulong(double(now) / (double(elapsed) / 1000.0)));
        }, Qt::QueuedConnection);
    };

    for (const QJsonValue &value : folders) {
        const QJsonObject folder = value.toObject();
        const QString to = folder.value(QStringLiteral("to")).toString();
        if (to.isEmpty()) {
            continue;
        }
        QDir().mkpath(to);
        const QJsonArray from = folder.value(QStringLiteral("from")).toArray();
        for (const QJsonValue &source : from) {
            const QDir root(source.toString());
            QDirIterator it(root.absolutePath(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (m_cancel) {
                    return;
                }
                const QString path = it.next();
                const QFileInfo info = it.fileInfo();
                QString target = to + QLatin1Char('/') + root.relativeFilePath(path);
                if (info.isDir()) {
                    QDir().mkpath(target);
                    continue;
                }
                if (windowsClutter.contains(info.fileName().toLower())) {
                    copied += info.size();
                    continue;
                }
                const QFileInfo existing(target);
                if (existing.exists()) {
                    // Run again after a break, the files already across are
                    // left alone; a different file of the same name is kept
                    // beside it rather than replaced.
                    if (existing.isFile() && existing.size() == info.size()) {
                        copied += info.size();
                        continue;
                    }
                    target = freeName(target);
                }

                QFile in(path);
                QSaveFile out(target);
                bool ok = in.open(QIODevice::ReadOnly) && out.open(QIODevice::WriteOnly);
                while (ok && !in.atEnd()) {
                    if (m_cancel) {
                        out.cancelWriting();
                        return;
                    }
                    const qint64 read = in.read(buffer.data(), buffer.size());
                    if (read < 0 || out.write(buffer.constData(), read) != read) {
                        ok = false;
                        break;
                    }
                    copied += read;
                    if (sinceUpdate.elapsed() > 250) {
                        sinceUpdate.restart();
                        report();
                    }
                }
                if (ok && out.commit()) {
                    QFile done(target);
                    if (done.open(QIODevice::ReadWrite)) {
                        done.setFileTime(info.lastModified(), QFileDevice::FileModificationTime);
                    }
                } else {
                    out.cancelWriting();
                    ++failures;
                }
            }
        }
    }
    report();
    QMetaObject::invokeMethod(this, [this, failures] {
        m_failures = failures;
    }, Qt::QueuedConnection);
}

void CopyJob::importBookmarks()
{
    QList<BrowserProfile> profiles;
    const QJsonArray list = m_job.value(QStringLiteral("bookmarks")).toArray();
    for (const QJsonValue &value : list) {
        const QJsonObject entry = value.toObject();
        profiles.append({entry.value(QStringLiteral("browser")).toString(), entry.value(QStringLiteral("path")).toString()});
    }
    if (profiles.isEmpty()) {
        emitResult();
        return;
    }
    // Brave writes its whole bookmarks file back when it closes, so ours
    // would be lost if it went in while Brave was open, and it keeps its
    // history locked. Wait for it.
    if (Bookmarks::braveRunning()) {
        Q_EMIT infoMessage(this, QCoreApplication::translate("Background", "Waiting for Brave to close to add your bookmarks and history"));
        QTimer::singleShot(10000, this, &CopyJob::importBookmarks);
        return;
    }
    // History goes into Brave's own history file, which only exists once
    // Brave has run. Done before the bookmarks, because Brave writes its
    // bookmarks file when it quits.
    if (!QFileInfo::exists(History::braveHistoryFile())) {
        Q_EMIT infoMessage(this, QCoreApplication::translate("Background", "Getting Brave ready for your bookmarks and history"));
        History::createBraveProfile();
    }
    m_history = History::importInto(History::braveHistoryFile(), profiles);
    m_bookmarks = Bookmarks::importInto(Bookmarks::braveBookmarksFile(), profiles);
    emitResult();
}

InstallJob::InstallJob(const QJsonArray &apps, QObject *parent)
    : KJob(parent)
    , m_apps(apps)
{
}

void InstallJob::start()
{
    QStringList names;
    for (const QJsonValue &value : std::as_const(m_apps)) {
        names << value.toObject().value(QStringLiteral("name")).toString();
    }
    Q_EMIT description(this, names.size() == 1 ? QCoreApplication::translate("Background", "Installing %1").arg(names.first()) : QCoreApplication::translate("Background", "Installing your apps"),
                       {QCoreApplication::translate("Background", "Apps"), joinNames(names)});
    QTimer::singleShot(0, this, &InstallJob::resolve);
}

void InstallJob::resolve()
{
    QStringList packages;
    for (const QJsonValue &value : std::as_const(m_apps)) {
        const QJsonObject app = value.toObject();
        const QString package = app.value(QStringLiteral("package")).toString();
        if (package.isEmpty()) {
            if (!app.value(QStringLiteral("flatpak")).toString().isEmpty()) {
                m_flatpakQueue << app.value(QStringLiteral("name")).toString();
            } else {
                m_failed << app.value(QStringLiteral("name")).toString();
            }
            continue;
        }
        packages << package;
        m_nameForPackage.insert(package, app.value(QStringLiteral("name")).toString());
    }
    if (packages.isEmpty()) {
        installFlatpaks();
        return;
    }

    auto ids = std::make_shared<QStringList>();
    auto found = std::make_shared<QSet<QString>>();
    Transaction *resolve = Daemon::resolve(packages, Transaction::FilterArch | Transaction::FilterNewest);
    connect(resolve, &Transaction::package, this, [this, ids, found](Transaction::Info info, const QString &packageID, const QString &) {
        const QString name = Transaction::packageName(packageID);
        if (found->contains(name)) {
            return;
        }
        found->insert(name);
        if (info == Transaction::InfoInstalled) {
            m_installed << m_nameForPackage.value(name);
        } else {
            ids->append(packageID);
        }
    });
    connect(resolve, &Transaction::finished, this, [this, ids, found, packages](Transaction::Exit, uint) {
        // Packages this computer's software sources do not carry, such as
        // Steam without the extra Debian sections, come from Flathub instead.
        for (const QString &package : packages) {
            if (found->contains(package)) {
                continue;
            }
            const QString name = m_nameForPackage.value(package);
            bool hasFlatpak = false;
            for (const QJsonValue &value : std::as_const(m_apps)) {
                const QJsonObject app = value.toObject();
                if (app.value(QStringLiteral("name")).toString() == name && !app.value(QStringLiteral("flatpak")).toString().isEmpty()) {
                    hasFlatpak = true;
                }
            }
            if (hasFlatpak) {
                m_flatpakQueue << name;
            } else {
                m_failed << name;
            }
        }
        if (ids->isEmpty()) {
            installFlatpaks();
        } else {
            installPackages(*ids);
        }
    });
}

void InstallJob::installPackages(const QStringList &ids)
{
    Transaction *install = Daemon::installPackages(ids, Transaction::TransactionFlagOnlyTrusted);
    connect(install, &Transaction::percentageChanged, this, [this, install] {
        const uint percent = install->percentage();
        // PackageKit reports 101 while it does not know yet.
        if (percent <= 100) {
            setPercent(percent);
        }
    });
    connect(install, &Transaction::finished, this, [this, ids](Transaction::Exit status, uint) {
        for (const QString &id : ids) {
            const QString name = m_nameForPackage.value(Transaction::packageName(id));
            if (status == Transaction::ExitSuccess) {
                m_installed << name;
            } else {
                m_failed << name;
            }
        }
        installFlatpaks();
    });
}

void InstallJob::installFlatpaks()
{
    if (m_flatpakQueue.isEmpty()) {
        done();
        return;
    }
    const QString name = m_flatpakQueue.takeFirst();
    QString id;
    for (const QJsonValue &value : std::as_const(m_apps)) {
        const QJsonObject app = value.toObject();
        if (app.value(QStringLiteral("name")).toString() == name) {
            id = app.value(QStringLiteral("flatpak")).toString();
        }
    }
    if (id.isEmpty() || QStandardPaths::findExecutable(QStringLiteral("flatpak")).isEmpty()) {
        m_failed << name;
        installFlatpaks();
        return;
    }
    Q_EMIT infoMessage(this, QCoreApplication::translate("Background", "Installing %1").arg(name));
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, name](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater();
        if (status == QProcess::NormalExit && exitCode == 0) {
            m_installed << name;
        } else {
            m_failed << name;
        }
        installFlatpaks();
    });
    process->start(QStringLiteral("flatpak"), {QStringLiteral("install"), QStringLiteral("--noninteractive"), QStringLiteral("--assumeyes"),
                                               QStringLiteral("flathub"), id});
}

void InstallJob::done()
{
    setPercent(100);
    emitResult();
}
