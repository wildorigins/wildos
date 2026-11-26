#include "packagekitbackend.h"
#include "flatpakbackend.h"

#include <PackageKit/Daemon>
#include <PackageKit/Details>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTimer>

#include <limits>

using PackageKit::Daemon;
using PackageKit::Transaction;

PackageKitBackend::PackageKitBackend(QObject *parent)
    : QObject(parent)
{
    // Lets polkit ask for the password. Without this hint PackageKit
    // marks every request as non-interactive and polkit refuses at once,
    // so installs failed with "needs the administrator password" and no
    // prompt ever appeared.
    PackageKit::Daemon::global()->setHints(QStringList{QStringLiteral("interactive=true")});
}

bool PackageKitBackend::isAvailable() const
{
    auto bus = QDBusConnection::systemBus().interface();
    if (!bus) {
        return false;
    }
    return bus->isServiceRegistered(QStringLiteral("org.freedesktop.PackageKit"))
        || bus->activatableServiceNames().value().contains(QStringLiteral("org.freedesktop.PackageKit"));
}

void PackageKitBackend::start()
{
    if (!isAvailable()) {
        return;
    }
    listInstalled();
    listUpdates();

    // An hour-old catalogue is fresh enough to browse; anything older is
    // refreshed so new apps and updates show up without a manual check.
    auto watcher = new QDBusPendingCallWatcher(Daemon::getTimeSinceAction(Transaction::RoleRefreshCache), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<uint> reply = *watcher;
        // A source added since the last refresh, like the apps repository
        // an update brings, has never been read: its apps would stay
        // missing until the next hourly refresh.
        if (reply.isError() || reply.value() > 3600 || reply.value() > secondsSinceSourcesChanged()) {
            refresh();
        }
    });
}

qint64 PackageKitBackend::secondsSinceSourcesChanged()
{
    QDateTime newest = QFileInfo(QStringLiteral("/etc/apt/sources.list")).lastModified();
    QDirIterator it(QStringLiteral("/etc/apt/sources.list.d"), {QStringLiteral("*.list"), QStringLiteral("*.sources")}, QDir::Files);
    while (it.hasNext()) {
        const QDateTime changed = it.nextFileInfo().lastModified();
        if (!newest.isValid() || changed > newest) {
            newest = changed;
        }
    }
    return newest.isValid() ? newest.secsTo(QDateTime::currentDateTime()) : std::numeric_limits<qint64>::max();
}

void PackageKitBackend::refresh()
{
    if (!isAvailable()) {
        QTimer::singleShot(0, this, [this] {
            Q_EMIT refreshFinished(false);
        });
        return;
    }
    Transaction *t = Daemon::refreshCache(false);
    connect(t, &Transaction::finished, this, [this](Transaction::Exit status, uint) {
        if (status != Transaction::ExitSuccess) {
            Q_EMIT refreshFinished(false);
            return;
        }
        Q_EMIT catalogueRefreshed();
        listUpdates([this](bool ok) {
            Q_EMIT refreshFinished(ok);
        });
    });
}

void PackageKitBackend::listInstalled()
{
    auto names = std::make_shared<QSet<QString>>();
    auto versions = std::make_shared<QHash<QString, QString>>();
    Transaction *t = Daemon::getPackages(Transaction::FilterInstalled);
    connect(t, &Transaction::package, this, [names, versions](Transaction::Info, const QString &id, const QString &) {
        const QString name = Transaction::packageName(id);
        names->insert(name);
        versions->insert(name, Transaction::packageVersion(id));
    });
    connect(t, &Transaction::finished, this, [this, names, versions](Transaction::Exit status, uint) {
        if (status != Transaction::ExitSuccess) {
            return;
        }
        m_installed = *names;
        m_versions = *versions;
        m_ready = true;
        Q_EMIT installedChanged();
    });
}

void PackageKitBackend::listUpdates(std::function<void(bool ok)> done)
{
    auto ids = std::make_shared<QHash<QString, QString>>();
    Transaction *t = Daemon::getUpdates();
    connect(t, &Transaction::package, this, [ids](Transaction::Info info, const QString &id, const QString &) {
        // Blocked updates are held back on purpose and cannot be installed.
        if (info != Transaction::InfoBlocked) {
            ids->insert(Transaction::packageName(id), id);
        }
    });
    connect(t, &Transaction::finished, this, [this, ids, done](Transaction::Exit status, uint) {
        if (status != Transaction::ExitSuccess) {
            if (done) {
                done(false);
            }
            return;
        }
        m_updateIds = *ids;
        m_updateNames = ids->keys();
        Q_EMIT updatesChanged();
        if (done) {
            done(true);
        }
    });
}

void PackageKitBackend::resolveThen(const QStringList &packages, Transaction::Filters filters, int job,
                                    std::function<Transaction *(const QStringList &ids)> next)
{
    auto ids = std::make_shared<QStringList>();
    Transaction *t = Daemon::resolve(packages, filters);
    connect(t, &Transaction::package, this, [ids](Transaction::Info, const QString &id, const QString &) {
        ids->append(id);
    });
    connect(t, &Transaction::finished, this, [this, ids, job, next](Transaction::Exit status, uint) {
        if (status != Transaction::ExitSuccess || ids->isEmpty()) {
            Q_EMIT finished(job, false, tr("This app is not in the catalogue any more. Try again after the Store has refreshed."));
            return;
        }
        Transaction *action = next(*ids);
        if (!action) {
            Q_EMIT finished(job, true, {});
        }
    });
}

void PackageKitBackend::watch(int job, Transaction *transaction, bool removing)
{
    auto error = std::make_shared<QString>();
    auto total = std::make_shared<qint64>(0);
    m_running.insert(job, transaction);
    if (m_cancelSoon.remove(job)) {
        transaction->cancel();
    }
    Q_EMIT progress(job, -1, QStringLiteral("prepare"), {}, -1, -1);
    connect(transaction, &Transaction::percentageChanged, this, [this, job, transaction, removing, total] {
        // 101 is PackageKit's "not known yet".
        const uint pct = transaction->percentage();
        QString status;
        switch (transaction->status()) {
        case Transaction::StatusWaitingForAuth:
            status = QStringLiteral("auth");
            break;
        case Transaction::StatusWaitingForLock:
            status = QStringLiteral("wait");
            break;
        case Transaction::StatusDownload:
            status = QStringLiteral("download");
            break;
        case Transaction::StatusRemove:
            status = QStringLiteral("remove");
            break;
        case Transaction::StatusInstall:
        case Transaction::StatusUpdate:
        case Transaction::StatusCommit:
        case Transaction::StatusRunHook:
            status = removing ? QStringLiteral("remove") : QStringLiteral("install");
            break;
        default:
            status = removing ? QStringLiteral("remove") : QStringLiteral("prepare");
            break;
        }
        // The first figure PackageKit gives for what is left to download is
        // the whole download.
        const qint64 remaining = qint64(transaction->downloadSizeRemaining());
        *total = qMax(*total, remaining);
        const qint64 done = *total > 0 ? *total - remaining : -1;
        Q_EMIT progress(job, pct > 100 ? -1 : int(pct), status, {}, done, *total > 0 ? *total : -1);
    });
    connect(transaction, &Transaction::statusChanged, transaction, &Transaction::percentageChanged);
    connect(transaction, &Transaction::errorCode, this, [error, removing](Transaction::Error code, const QString &) {
        *error = errorText(code, removing);
    });
    connect(transaction, &Transaction::finished, this, [this, job, error](Transaction::Exit status, uint) {
        m_running.remove(job);
        listInstalled();
        listUpdates();
        if (status == Transaction::ExitSuccess) {
            Q_EMIT finished(job, true, {});
        } else if (status == Transaction::ExitCancelled) {
            Q_EMIT finished(job, false, QString::fromLatin1(cancelledMarker));
        } else {
            Q_EMIT finished(job, false, error->isEmpty() ? tr("Something went wrong. Try again in a few minutes.") : *error);
        }
    });
}

void PackageKitBackend::cancel(int job)
{
    if (QPointer<Transaction> t = m_running.value(job)) {
        t->cancel();
        return;
    }
    // Still resolving the package names; cancelled as soon as it starts.
    m_cancelSoon.insert(job);
}

void PackageKitBackend::seedInstalled(const QSet<QString> &packages)
{
    if (!m_ready) {
        m_installed = packages;
    }
}

void PackageKitBackend::install(int job, const QStringList &packages)
{
    Q_EMIT progress(job, -1, QStringLiteral("prepare"), {}, -1, -1);
    resolveThen(packages, Transaction::FilterNotInstalled | Transaction::FilterArch | Transaction::FilterNewest, job, [this, job](const QStringList &ids) {
        Transaction *t = Daemon::installPackages(ids);
        watch(job, t, false);
        return t;
    });
}

void PackageKitBackend::remove(int job, const QStringList &packages)
{
    Q_EMIT progress(job, -1, QStringLiteral("remove"), {}, -1, -1);
    resolveThen(packages, Transaction::FilterInstalled, job, [this, job](const QStringList &ids) {
        // Never take anything else with it: if another part of Wild OS
        // depends on this app, PackageKit refuses and we say so.
        Transaction *t = Daemon::removePackages(ids, false, true);
        watch(job, t, true);
        return t;
    });
}

void PackageKitBackend::update(int job, const QStringList &packages)
{
    QStringList ids;
    for (const QString &name : packages) {
        if (m_updateIds.contains(name)) {
            ids << m_updateIds.value(name);
        }
    }
    if (ids.isEmpty()) {
        Q_EMIT finished(job, true, {});
        return;
    }
    Transaction *t = Daemon::updatePackages(ids);
    watch(job, t, false);
}

void PackageKitBackend::fetchDetails(const QString &key, const QStringList &packages)
{
    if (!isAvailable() || packages.isEmpty()) {
        return;
    }
    auto ids = std::make_shared<QStringList>();
    Transaction *t = Daemon::resolve(packages, Transaction::FilterArch | Transaction::FilterNewest);
    connect(t, &Transaction::package, this, [ids](Transaction::Info, const QString &id, const QString &) {
        ids->append(id);
    });
    connect(t, &Transaction::finished, this, [this, ids, key](Transaction::Exit status, uint) {
        if (status != Transaction::ExitSuccess || ids->isEmpty()) {
            return;
        }
        auto total = std::make_shared<qint64>(0);
        auto version = std::make_shared<QString>(Transaction::packageVersion(ids->constFirst()));
        Transaction *details = Daemon::getDetails(*ids);
        connect(details, &Transaction::details, this, [total](const PackageKit::Details &d) {
            *total += qint64(d.size());
        });
        connect(details, &Transaction::finished, this, [this, key, total, version](Transaction::Exit, uint) {
            Q_EMIT detailsReady(key, *total, 0, *version);
        });
    });
}

QString PackageKitBackend::errorText(Transaction::Error error, bool removing)
{
    switch (error) {
    case Transaction::ErrorNoNetwork:
    case Transaction::ErrorNoCache:
    case Transaction::ErrorRepoNotAvailable:
    case Transaction::ErrorCannotFetchSources:
    case Transaction::ErrorPackageDownloadFailed:
        return tr("Wild OS could not reach the app servers. Check your internet connection and try again.");
    case Transaction::ErrorNotAuthorized:
        return tr("This needs the administrator password, and it was not given.");
    case Transaction::ErrorNoSpaceOnDevice:
        return tr("There is not enough free space for this app.");
    case Transaction::ErrorLockRequired:
    case Transaction::ErrorCannotGetLock:
        return tr("Another app is installing software. Try again when it has finished.");
    case Transaction::ErrorTransactionCancelled:
        return tr("Cancelled.");
    case Transaction::ErrorDepResolutionFailed:
    case Transaction::ErrorCannotRemoveSystemPackage:
        return removing ? tr("Other parts of Wild OS need this app, so it has to stay.")
                        : tr("This app needs parts Wild OS can't provide right now. Try again after your next update.");
    default:
        return tr("Something went wrong. Try again in a few minutes.");
    }
}
