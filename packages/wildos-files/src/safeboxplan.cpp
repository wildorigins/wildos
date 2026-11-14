#include "safeboxplan.h"

#include "programs.h"

#include <KApplicationTrader>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUrl>

namespace
{
bool runsInBox(const KService::Ptr &service)
{
    if (!service || service->noDisplay()) {
        return false;
    }
    const QString exec = service->exec();
    const QString program = exec.section(QLatin1Char(' '), 0, 0);
    if (program.endsWith(QLatin1String("flatpak")) || service->property<QString>(QStringLiteral("X-Flatpak")).length() > 0) {
        return false;
    }
    // App Bridge's Windows programs and Windows mode itself live in the
    // Windows virtual PC, which a box can't reach.
    if (service->storageId().startsWith(QLatin1String("wildos-winapp-")) || program.endsWith(QLatin1String("wild-run"))
        || program.endsWith(QLatin1String("wildos-winmode"))) {
        return false;
    }
    // Something that only opens a terminal has no window of its own here.
    return !service->terminal();
}
}

SafeBoxPlan safeBoxPlan(const QString &path)
{
    SafeBoxPlan plan;
    const QFileInfo info(path);
    if (!info.isFile()) {
        plan.message = QStringLiteral("Only files can open in the safe box.");
        return plan;
    }
    const QMimeType type = QMimeDatabase().mimeTypeForFile(info);
    if (type.inherits(QStringLiteral("application/x-ms-dos-executable")) || type.inherits(QStringLiteral("application/x-msi"))
        || type.inherits(QStringLiteral("application/x-msdownload")) || type.inherits(QStringLiteral("application/vnd.microsoft.portable-executable"))) {
        plan.message = QStringLiteral("Windows programs can't run in the safe box.");
        return plan;
    }
    if (isLinuxProgram(QUrl::fromLocalFile(info.absoluteFilePath()), type)) {
        const quint16 machine = elfMachine(info.absoluteFilePath());
        if (machine != 0 && hostMachine != 0 && machine != hostMachine) {
            plan.message = QStringLiteral("%1 is made for a different type of processor, so it cannot run on this PC.").arg(info.fileName());
            return plan;
        }
        plan.ok = true;
        plan.program = true;
        plan.appName = info.completeBaseName();
        return plan;
    }
    const KService::Ptr preferred = KApplicationTrader::preferredService(type.name());
    if (runsInBox(preferred)) {
        plan.app = preferred;
    } else {
        const auto services = KApplicationTrader::queryByMimeType(type.name());
        for (const KService::Ptr &service : services) {
            if (runsInBox(service)) {
                plan.app = service;
                break;
            }
        }
    }
    if (!plan.app) {
        plan.message = preferred ? QStringLiteral("%1 can't run in the safe box, and no other app opens this kind of file.").arg(preferred->name())
                                 : QStringLiteral("No app opens this kind of file.");
        return plan;
    }
    plan.ok = true;
    plan.appName = plan.app->name();
    return plan;
}
