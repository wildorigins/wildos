#include "securetime.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace SecureTime
{
bool running()
{
    const QString systemd = QStringLiteral("org.freedesktop.systemd1");
    auto getUnit = QDBusMessage::createMethodCall(systemd, QStringLiteral("/org/freedesktop/systemd1"), QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                  QStringLiteral("GetUnit"));
    getUnit << QStringLiteral("sdwdate.service");
    const QDBusMessage unit = QDBusConnection::systemBus().call(getUnit, QDBus::Block, 3000);
    // Not loaded means not installed, masked, or never started.
    if (unit.type() != QDBusMessage::ReplyMessage) {
        return false;
    }
    auto get = QDBusMessage::createMethodCall(systemd, unit.arguments().value(0).value<QDBusObjectPath>().path(),
                                              QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    get << QStringLiteral("org.freedesktop.systemd1.Unit") << QStringLiteral("ActiveState");
    const QDBusMessage reply = QDBusConnection::systemBus().call(get, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        return false;
    }
    const QString state = reply.arguments().value(0).value<QDBusVariant>().variant().toString();
    return state == QLatin1String("active") || state == QLatin1String("activating") || state == QLatin1String("reloading");
}

bool setThisBoot()
{
    return QFile::exists(QStringLiteral("/run/sdwdate/first_success"));
}

bool settingNow()
{
    return !setThisBoot() && running();
}

qint64 secondsBehind(const QString &aptDetails)
{
    static const QRegularExpression stamp(QStringLiteral("not live until (\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\dZ)"),
                                          QRegularExpression::CaseInsensitiveOption);
    qint64 newest = 0;
    auto it = stamp.globalMatch(aptDetails);
    while (it.hasNext()) {
        const QDateTime at = QDateTime::fromString(it.next().captured(1), Qt::ISODate);
        if (at.isValid()) {
            newest = std::max(newest, at.toSecsSinceEpoch());
        }
    }
    return newest > 0 ? std::max<qint64>(0, newest - QDateTime::currentSecsSinceEpoch()) : 0;
}

QString durationText(qint64 seconds)
{
    // Rounded down: the signing times only show the clock is at least this
    // far behind.
    auto count = [](qint64 n, const char *one, const char *many) {
        return QStringLiteral("%1 %2").arg(n).arg(QLatin1String(n == 1 ? one : many));
    };
    if (seconds >= 2 * 86400) {
        return count(seconds / 86400, "day", "days");
    }
    if (seconds >= 3600) {
        return count(seconds / 3600, "hour", "hours");
    }
    return count(std::max<qint64>(1, seconds / 60), "minute", "minutes");
}
}
