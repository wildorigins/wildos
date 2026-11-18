#include "windowsapps.h"
#include "processes.h"
#include "winmode.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

WindowsApps::WindowsApps(QObject *parent)
    : QObject(parent)
{
    connect(Processes::instance(), &Processes::updated, this, &WindowsApps::reload);
    reload();
}

void WindowsApps::reload()
{
    const WinMode::Status &status = Processes::instance()->winModeStatus();
    const QList<WinMode::App> installed = WinMode::installed();
    QStringList open;
    for (const QString &program : status.openPrograms) {
        open << WinMode::appFor(installed, program);
    }
    QVariantList apps;
    for (const WinMode::App &app : installed) {
        const bool on = open.contains(app.id);
        apps << QVariantMap{
            {QStringLiteral("id"), app.id},
            {QStringLiteral("name"), app.name},
            {QStringLiteral("icon"), app.icon},
            {QStringLiteral("publisher"), QString()},
            {QStringLiteral("running"), on},
            {QStringLiteral("status"), on ? QCoreApplication::translate("WindowsApps", "Open") : QCoreApplication::translate("WindowsApps", "Not open")},
            {QStringLiteral("memory"), 0.0},
            {QStringLiteral("cpu"), 0.0},
        };
    }
    if (apps != m_apps) {
        m_apps = apps;
        Q_EMIT appsChanged();
    }
}

void WindowsApps::open(const QString &id)
{
    if (id.isEmpty() || id.contains(QLatin1Char('/'))) {
        return;
    }
    // Its Start entry, so it starts the way Start starts it, with the
    // taskbar's launch feedback.
    const QString entry = QStringLiteral("wildos-winmode-%1").arg(id);
    if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation, entry + QStringLiteral(".desktop")).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("kstart")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("kstart"), {QStringLiteral("--application"), entry});
        return;
    }
    QProcess::startDetached(QStringLiteral("wildos-winmode"), {QStringLiteral("launch"), id});
}

void WindowsApps::openSettings()
{
    QProcess::startDetached(QStringLiteral("wildos-settings"), {QStringLiteral("--page"), QStringLiteral("windowsapps")});
}
