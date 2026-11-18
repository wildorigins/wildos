#include "winmode.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace WinMode
{
bool installedOnThisPc()
{
    return !QStandardPaths::findExecutable(QStringLiteral("wildos-winmode")).isEmpty();
}

Status status()
{
    Status s;
    if (!installedOnThisPc()) {
        return s;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.WindowsMode1"), QStringLiteral("/org/wildos/WindowsMode1"),
                                                       QStringLiteral("org.wildos.WindowsMode1.Host"), QStringLiteral("Status"));
    // Never starts the service only to ask: Task Manager refreshes often,
    // and a service that isn't running means Windows mode isn't either.
    call.setAutoStartService(false);
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 1000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return s;
    }
    const QJsonObject o = QJsonDocument::fromJson(reply.arguments().constFirst().toString().toUtf8()).object();
    s.available = true;
    s.vm = o.value(QStringLiteral("vm")).toString();
    const QJsonObject qemu = o.value(QStringLiteral("qemu")).toObject();
    s.pid = qemu.value(QStringLiteral("pid")).toInteger();
    s.cpuSeconds = qemu.value(QStringLiteral("cpu_seconds")).toDouble();
    s.memory = qemu.value(QStringLiteral("rss_bytes")).toInteger();
    for (const QJsonValue &w : o.value(QStringLiteral("windows")).toArray()) {
        const QString image = w.toObject().value(QStringLiteral("image")).toString();
        if (!image.isEmpty() && !s.openPrograms.contains(image)) {
            s.openPrograms << image;
        }
    }
    s.whenIdle = o.value(QStringLiteral("when_idle")).toString();
    s.idleSeconds = o.value(QStringLiteral("idle_seconds")).toInt(60);
    return s;
}

QList<App> installed()
{
    QList<App> apps;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wildos/winmode/apps.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return apps;
    }
    const QJsonObject registry = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = registry.constBegin(); it != registry.constEnd(); ++it) {
        const QJsonObject a = it.value().toObject();
        apps << App{it.key(), a.value(QStringLiteral("name")).toString(it.key()), QStringLiteral("wildos-winmode-") + it.key(),
                    a.value(QStringLiteral("target")).toString()};
    }
    return apps;
}

QString appFor(const QList<App> &apps, const QString &program)
{
    const QString stem = program.section(QLatin1Char('\\'), -1).section(QLatin1Char('.'), 0, 0);
    for (const App &app : apps) {
        if (app.target.compare(program, Qt::CaseInsensitive) == 0) {
            return app.id;
        }
    }
    for (const App &app : apps) {
        if (app.target.section(QLatin1Char('\\'), -1).section(QLatin1Char('.'), 0, 0).compare(stem, Qt::CaseInsensitive) == 0) {
            return app.id;
        }
    }
    return {};
}

QString stateText(const QString &vm)
{
    if (vm == QLatin1String("running")) {
        return QCoreApplication::translate("WinMode", "Running");
    }
    if (vm == QLatin1String("paused") || vm == QLatin1String("pmsuspended")) {
        return QCoreApplication::translate("WinMode", "Paused");
    }
    if (vm == QLatin1String("saved")) {
        return QCoreApplication::translate("WinMode", "Saved to disk");
    }
    if (vm.isEmpty()) {
        return QCoreApplication::translate("WinMode", "Not set up");
    }
    return QCoreApplication::translate("WinMode", "Off");
}

QString idleText(const QString &whenIdle, int seconds)
{
    const QString after = seconds % 60 == 0 ? (seconds == 60 ? QCoreApplication::translate("WinMode", "1 minute") : QCoreApplication::translate("WinMode", "%1 minutes").arg(seconds / 60))
                                            : QCoreApplication::translate("WinMode", "%1 seconds").arg(seconds);
    if (whenIdle == QLatin1String("keep")) {
        return QCoreApplication::translate("WinMode", "Paused after %1").arg(after);
    }
    if (whenIdle == QLatin1String("save")) {
        return QCoreApplication::translate("WinMode", "Saved to disk after %1").arg(after);
    }
    return QCoreApplication::translate("WinMode", "Paused or saved after %1").arg(after);
}
}
