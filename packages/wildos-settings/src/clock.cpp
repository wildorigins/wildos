#include "clock.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFileSystemWatcher>
#include <QTimeZone>

#include "securetime.h"

#include <algorithm>

namespace
{
const QString service = QStringLiteral("org.freedesktop.timedate1");
const QString path = QStringLiteral("/org/freedesktop/timedate1");
const QString interfaceName = QStringLiteral("org.freedesktop.timedate1");

QString offsetText(int seconds)
{
    const int minutes = std::abs(seconds) / 60;
    return QStringLiteral("(UTC%1%2:%3)")
        .arg(seconds < 0 ? QLatin1Char('-') : QLatin1Char('+'))
        .arg(minutes / 60, 2, 10, QLatin1Char('0'))
        .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}
}

Clock::Clock(QObject *parent)
    : QObject(parent)
{
    readSecureTime();
    // sdwdate's marker for having set the clock appears in this folder, so
    // the page changes from "being set" to "set" as it happens.
    auto watch = new QFileSystemWatcher({QStringLiteral("/run/sdwdate")}, this);
    connect(watch, &QFileSystemWatcher::directoryChanged, this, [this] {
        readSecureTime();
    });
    QDBusConnection bus = QDBusConnection::systemBus();
    auto names = bus.interface();
    // timedated starts on demand, so it counts as there when it can start.
    m_available = names && (names->isServiceRegistered(service) || names->activatableServiceNames().value().contains(service));
    if (!m_available) {
        m_timeZone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
        return;
    }
    bus.connect(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    read();
}

void Clock::readSecureTime()
{
    const bool running = SecureTime::running();
    const bool set = SecureTime::setThisBoot();
    if (running != m_secureTime || set != m_secureTimeSet) {
        m_secureTime = running;
        m_secureTimeSet = set;
        Q_EMIT changed();
    }
}

void Clock::read()
{
    auto message = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
    message << interfaceName;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            m_timeZone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
            Q_EMIT changed();
            return;
        }
        const QVariantMap values = reply.value();
        m_timeZone = values.value(QStringLiteral("Timezone")).toString();
        m_automatic = values.value(QStringLiteral("NTP")).toBool();
        m_canAutomatic = values.value(QStringLiteral("CanNTP")).toBool();
        m_synchronised = values.value(QStringLiteral("NTPSynchronized")).toBool();
        Q_EMIT changed();
    });
}

void Clock::onPropertiesChanged(const QString &, const QVariantMap &, const QStringList &)
{
    read();
}

void Clock::call(const QString &method, const QVariantList &arguments)
{
    auto message = QDBusMessage::createMethodCall(service, path, interfaceName, method);
    message.setArguments(arguments);
    // polkit may ask for a password, which can take a while.
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            const QString name = reply.error().name();
            if (name.endsWith(QLatin1String("AccessDenied")) || name.endsWith(QLatin1String("InteractiveAuthorizationRequired"))) {
                Q_EMIT failed(QStringLiteral("Nothing was changed. An administrator password is needed for this."));
            } else {
                Q_EMIT failed(QStringLiteral("Wild OS could not change the clock. Try again in a moment."));
            }
        }
        read();
    });
}

QVariantList Clock::timeZones() const
{
    struct Zone {
        int offset;
        QString id;
        QString text;
    };
    QList<Zone> zones;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const auto ids = QTimeZone::availableTimeZoneIds();
    for (const QByteArray &raw : ids) {
        const QString id = QString::fromUtf8(raw);
        // Region/City names only; the old aliases (EST5EDT, Etc/GMT+3) are
        // the same zones under names nobody looks for.
        if (!id.contains(QLatin1Char('/')) || id.startsWith(QLatin1String("Etc/")) || id.startsWith(QLatin1String("SystemV/"))) {
            continue;
        }
        const QTimeZone zone(raw);
        const int offset = zone.offsetFromUtc(now);
        QString place = id.section(QLatin1Char('/'), -1);
        place.replace(QLatin1Char('_'), QLatin1Char(' '));
        const QString region = id.section(QLatin1Char('/'), 0, 0);
        zones.append({offset, id, QStringLiteral("%1 %2, %3").arg(offsetText(offset), place, region)});
    }
    std::sort(zones.begin(), zones.end(), [](const Zone &a, const Zone &b) {
        return a.offset != b.offset ? a.offset < b.offset : a.text < b.text;
    });
    QVariantList list;
    for (const Zone &zone : std::as_const(zones)) {
        list.append(QVariantMap{{QStringLiteral("value"), zone.id}, {QStringLiteral("text"), zone.text}});
    }
    return list;
}

void Clock::setTimeZone(const QString &id)
{
    if (id.isEmpty() || id == m_timeZone) {
        return;
    }
    call(QStringLiteral("SetTimezone"), {id, true});
}

void Clock::setAutomatic(bool on)
{
    call(QStringLiteral("SetNTP"), {on, true});
}

void Clock::setDateTime(int day, int month, int year, int hour, int minute)
{
    const QDateTime when(QDate(year, month, day), QTime(hour, minute));
    if (!when.isValid()) {
        Q_EMIT failed(QStringLiteral("That is not a date and time Wild OS can set."));
        return;
    }
    call(QStringLiteral("SetTime"), {QVariant::fromValue<qlonglong>(when.toMSecsSinceEpoch() * 1000), false, true});
}
