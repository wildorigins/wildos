#include "games.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>

namespace
{
const QString gameMode = QStringLiteral("com.feralinteractive.GameMode");
const QString gameModePath = QStringLiteral("/com/feralinteractive/GameMode");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");

bool steamGameRunning()
{
    // The words are separate arguments, so a nought sits between them.
    const QByteArray sign = QByteArray("SteamLaunch") + '\0' + QByteArray("AppId=");
    const QString root = qEnvironmentVariable("WILDOS_COOLER_SYSROOT") + QStringLiteral("/proc");
    const QStringList entries = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (!entry.at(0).isDigit()) {
            continue;
        }
        QFile commandLine(root + QLatin1Char('/') + entry + QStringLiteral("/cmdline"));
        if (commandLine.open(QIODevice::ReadOnly) && commandLine.read(1024).contains(sign)) {
            return true;
        }
    }
    return false;
}
}

Games::Games(QObject *parent)
    : QObject(parent)
{
    m_timer.setInterval(10000);
    m_timer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Games::look);
}

void Games::watch(bool watch)
{
    if (watch == m_timer.isActive()) {
        return;
    }
    if (!watch) {
        m_timer.stop();
        set(false, 0);
        return;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!m_connected) {
        m_connected = bus.connect(gameMode, gameModePath, properties, QStringLiteral("PropertiesChanged"), this,
                                  SLOT(gameModeChanged(QString, QVariantMap, QStringList)));
    }
    // GameMode starts when it is first asked for; asking how many games it
    // has must not be what starts it.
    QDBusMessage ask = QDBusMessage::createMethodCall(gameMode, gameModePath, properties, QStringLiteral("Get"));
    ask.setArguments({gameMode, QStringLiteral("ClientCount")});
    ask.setAutoStartService(false);
    auto *asked = new QDBusPendingCallWatcher(bus.asyncCall(ask), this);
    connect(asked, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (!reply.isError() && m_timer.isActive()) {
            set(m_steam, reply.value().variant().toInt());
        }
    });
    m_timer.start();
    look();
}

void Games::gameModeChanged(const QString &, const QVariantMap &changed, const QStringList &)
{
    const auto count = changed.constFind(QStringLiteral("ClientCount"));
    if (count != changed.constEnd() && m_timer.isActive()) {
        set(m_steam, count->toInt());
    }
}

void Games::look()
{
    set(steamGameRunning(), m_gameMode);
}

void Games::set(bool steam, int gameMode)
{
    const bool was = running();
    m_steam = steam;
    m_gameMode = gameMode;
    if (running() != was) {
        Q_EMIT runningChanged();
    }
}
