#include "activityhistory.h"

#include <KApplicationTrader>
#include <KConfig>
#include <KConfigGroup>
#include <KService>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QStandardPaths>

namespace
{
const QString pluginsrc = QStringLiteral("kactivitymanagerd-pluginsrc");
// definitions.h SQLITE_PLUGIN_CONFIG_KEY
const QString scoring = QStringLiteral("Plugin-org.kde.ActivityManager.Resources.Scoring");

// kcm_recentFiles.h WhatToRemember
enum WhatToRemember {
    AllApplications = 0,
    SpecificApplications = 1,
    NoApplications = 2,
};

QVariantMap describe(const QString &name)
{
    const KService::Ptr service = KService::serviceByDesktopName(name);
    return {{QStringLiteral("id"), name},
            {QStringLiteral("name"), service ? service->name() : name},
            {QStringLiteral("icon"), service ? service->icon() : QStringLiteral("application-x-executable")}};
}
}

ActivityHistory::ActivityHistory(QObject *parent)
    : QObject(parent)
{
}

bool ActivityHistory::remember() const
{
    const KConfig config(pluginsrc, KConfig::NoGlobals);
    return config.group(scoring).readEntry("what-to-remember", int(AllApplications)) != NoApplications;
}

int ActivityHistory::keepMonths() const
{
    const KConfig config(pluginsrc, KConfig::NoGlobals);
    return config.group(scoring).readEntry("keep-history-for", 0);
}

QStringList ActivityHistory::blockedNames() const
{
    const KConfig config(pluginsrc, KConfig::NoGlobals);
    return config.group(scoring).readEntry("blocked-applications", QStringList());
}

QVariantList ActivityHistory::blocked() const
{
    QVariantList out;
    for (const QString &name : blockedNames()) {
        out << describe(name);
    }
    return out;
}

QVariantList ActivityHistory::apps() const
{
    const QStringList taken = blockedNames();
    const KService::List services = KApplicationTrader::query([&taken](const KService::Ptr &service) {
        return !service->noDisplay() && !taken.contains(service->desktopEntryName());
    });
    QVariantList out;
    for (const KService::Ptr &service : services) {
        out << QVariantMap{{QStringLiteral("value"), service->desktopEntryName()}, {QStringLiteral("text"), service->name()}};
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("text")).toString().localeAwareCompare(b.toMap().value(QStringLiteral("text")).toString()) < 0;
    });
    return out;
}

void ActivityHistory::writeMode(bool on, const QStringList &blocked)
{
    KConfig plugins(pluginsrc, KConfig::NoGlobals);
    KConfigGroup group = plugins.group(scoring);
    // A blocked list only counts in "specific applications" mode, with
    // blocked-by-default off so the list holds the apps left out
    // (StatsPlugin::acceptedEvent).
    const int mode = !on ? NoApplications : blocked.isEmpty() ? AllApplications : SpecificApplications;
    group.writeEntry("what-to-remember", mode);
    group.writeEntry("blocked-by-default", false);
    group.writeEntry("blocked-applications", blocked);
    plugins.sync();

    // The recent files module turns the scoring plugin off with "do not
    // remember" (RecentFilesKcm::save).
    KConfig daemon(QStringLiteral("kactivitymanagerdrc"), KConfig::NoGlobals);
    daemon.group(QStringLiteral("Plugins")).writeEntry("org.kde.ActivityManager.ResourceScoringEnabled", on);
    daemon.sync();

    // KRecentDocument::add reads this in every KDE app before it adds a file.
    KConfig globals(QStringLiteral("kdeglobals"));
    globals.group(QStringLiteral("RecentDocuments")).writeEntry("UseRecent", on, KConfig::Notify);
    globals.sync();
    Q_EMIT changed();
}

void ActivityHistory::setRemember(bool on)
{
    writeMode(on, blockedNames());
}

void ActivityHistory::setKeepMonths(int months)
{
    KConfig plugins(pluginsrc, KConfig::NoGlobals);
    plugins.group(scoring).writeEntry("keep-history-for", qMax(0, months));
    plugins.sync();
    Q_EMIT changed();
}

void ActivityHistory::block(const QString &app)
{
    QStringList list = blockedNames();
    if (!app.isEmpty() && !list.contains(app)) {
        list << app;
        list.sort();
        writeMode(remember(), list);
    }
}

void ActivityHistory::unblock(const QString &app)
{
    QStringList list = blockedNames();
    if (list.removeAll(app) > 0) {
        writeMode(remember(), list);
    }
}

void ActivityHistory::clear()
{
    if (m_clearing) {
        return;
    }
    m_clearing = true;
    Q_EMIT clearingChanged();

    // KRecentDocument::clear() is exactly this.
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel"));

    // RecentFilesKcm::forgetAll: every activity, everything.
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.ActivityManager"), QStringLiteral("/ActivityManager/Resources/Scoring"),
                                                  QStringLiteral("org.kde.ActivityManager.ResourcesScoring"), QStringLiteral("DeleteRecentStats"));
    message << QString() << 0 << QStringLiteral("everything");
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<> reply = *call;
        call->deleteLater();
        m_clearing = false;
        Q_EMIT clearingChanged();
        if (reply.isError()) {
            Q_EMIT failed(tr("The activity service did not answer. Try again in a moment."));
        } else {
            Q_EMIT cleared();
        }
    });
}

void ActivityHistory::reload()
{
    Q_EMIT changed();
}
