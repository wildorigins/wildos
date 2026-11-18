#include "prefs.h"

#include <KConfig>
#include <KConfigGroup>
#include <KShell>

#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

Prefs::Prefs(QObject *parent)
    : QObject(parent)
{
    KConfig config(QStringLiteral("wildos-taskmanagerrc"));
    const KConfigGroup group = config.group(QStringLiteral("General"));
    m_startPage = group.readEntry("StartPage", m_startPage);
    m_updateSpeed = group.readEntry("UpdateSpeed", m_updateSpeed);
    m_showSystem = group.readEntry("ShowSystemProcesses", m_showSystem);
}

void Prefs::save()
{
    KConfig config(QStringLiteral("wildos-taskmanagerrc"));
    KConfigGroup group = config.group(QStringLiteral("General"));
    group.writeEntry("StartPage", m_startPage);
    group.writeEntry("UpdateSpeed", m_updateSpeed);
    group.writeEntry("ShowSystemProcesses", m_showSystem);
    config.sync();
}

QString Prefs::sortColumn(const QString &table, const QString &fallback) const
{
    KConfig config(QStringLiteral("wildos-taskmanagerrc"));
    const KConfigGroup group = config.group(QStringLiteral("Sort"));
    return group.readEntry(table + QStringLiteral("Column"), fallback);
}

bool Prefs::sortDescending(const QString &table) const
{
    KConfig config(QStringLiteral("wildos-taskmanagerrc"));
    const KConfigGroup group = config.group(QStringLiteral("Sort"));
    return group.readEntry(table + QStringLiteral("Descending"), false);
}

void Prefs::setSort(const QString &table, const QString &column, bool descending)
{
    KConfig config(QStringLiteral("wildos-taskmanagerrc"));
    KConfigGroup group = config.group(QStringLiteral("Sort"));
    if (group.readEntry(table + QStringLiteral("Column"), QString()) == column && group.readEntry(table + QStringLiteral("Descending"), false) == descending) {
        return;
    }
    group.writeEntry(table + QStringLiteral("Column"), column);
    group.writeEntry(table + QStringLiteral("Descending"), descending);
    config.sync();
}

void Prefs::setStartPage(const QString &page)
{
    if (page == m_startPage) {
        return;
    }
    m_startPage = page;
    save();
    Q_EMIT changed();
}

void Prefs::setUpdateSpeed(const QString &speed)
{
    if (speed == m_updateSpeed) {
        return;
    }
    m_updateSpeed = speed;
    save();
    Q_EMIT changed();
}

int Prefs::updateInterval() const
{
    if (m_updateSpeed == QLatin1String("high")) {
        return 500;
    }
    if (m_updateSpeed == QLatin1String("low")) {
        return 4000;
    }
    return 1000;
}

void Prefs::setShowSystem(bool show)
{
    if (show == m_showSystem) {
        return;
    }
    m_showSystem = show;
    save();
    Q_EMIT changed();
}

QString Prefs::runTask(const QString &command)
{
    KShell::Errors error = KShell::NoError;
    const QStringList args = KShell::splitArgs(command.trimmed(), KShell::TildeExpand, &error);
    if (error != KShell::NoError || args.isEmpty()) {
        return QCoreApplication::translate("Prefs", "Check the name and try again.");
    }
    if (QStandardPaths::findExecutable(args.constFirst()).isEmpty() && !args.constFirst().contains(QLatin1Char('/'))) {
        return QCoreApplication::translate("Prefs", "Wild OS cannot find “%1”. Check the name and try again.").arg(args.constFirst());
    }
    // kstart gives it an app scope of its own, as Start does, so it shows
    // under Apps and can be ended as one.
    if (!QStandardPaths::findExecutable(QStringLiteral("kstart")).isEmpty()) {
        if (QProcess::startDetached(QStringLiteral("kstart"), QStringList{QStringLiteral("--")} + args)) {
            return {};
        }
    }
    return QProcess::startDetached(args.constFirst(), args.mid(1)) ? QString() : QCoreApplication::translate("Prefs", "That could not be started.");
}

void Prefs::openSettings(const QString &page)
{
    QProcess::startDetached(QStringLiteral("wildos-settings"), {QStringLiteral("--page"), page});
}
