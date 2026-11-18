#include "clock.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFile>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QRegularExpression>
#include <QTimeZone>

#include "securetime.h"

#include <algorithm>

namespace
{
const QString service = QStringLiteral("org.freedesktop.timedate1");
const QString path = QStringLiteral("/org/freedesktop/timedate1");
const QString interfaceName = QStringLiteral("org.freedesktop.timedate1");
const QString sourceHelper = QStringLiteral("/usr/libexec/wildos/settings-time-source");
// Both written by the helper: the first is the choice of a time server,
// the second names the server when it is not Debian's.
const QString sourceChoice = QStringLiteral("/etc/wildos/time-server");
const QString serverFile = QStringLiteral("/etc/systemd/timesyncd.conf.d/50-wildos.conf");

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
    readTimeSource();
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

void Clock::readTimeSource()
{
    const QString source = QFile::exists(sourceChoice) ? QStringLiteral("server") : QStringLiteral("tor");
    QString server;
    QFile file(serverFile);
    if (file.open(QIODevice::ReadOnly)) {
        static const QRegularExpression line(QStringLiteral("^NTP=(\\S+)\\s*$"), QRegularExpression::MultilineOption);
        server = line.match(QString::fromUtf8(file.readAll())).captured(1);
    }
    if (source != m_timeSource || server != m_timeServer) {
        m_timeSource = source;
        m_timeServer = server;
        Q_EMIT changed();
    }
}

bool Clock::serverNameValid(const QString &server) const
{
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9.:][A-Za-z0-9.:-]{0,252}$"));
    return server.isEmpty() || name.match(server).hasMatch();
}

void Clock::setTimeSource(const QString &source, const QString &server)
{
    const bool toServer = source == QLatin1String("server");
    if (m_sourceBusy || (!toServer && source != QLatin1String("tor")) || (toServer && !serverNameValid(server))
        || (source == m_timeSource && (!toServer || server == m_timeServer))) {
        return;
    }
    m_sourceBusy = true;
    Q_EMIT changed();
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code) {
        process->deleteLater();
        m_sourceBusy = false;
        readTimeSource();
        readSecureTime();
        Q_EMIT changed();
        if (code == 126 || code == 127) {
            Q_EMIT failed(tr("Nothing was changed. An administrator password is needed for this."));
        } else if (code != 0) {
            Q_EMIT failed(tr("Wild OS could not change where the clock is set from. Try again in a moment."));
        }
        read();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_sourceBusy = false;
            Q_EMIT changed();
            Q_EMIT failed(tr("Wild OS could not change where the clock is set from. Try again in a moment."));
        }
    });
    QStringList arguments{sourceHelper, source};
    if (toServer && !server.isEmpty()) {
        arguments << server;
    }
    process->start(QStringLiteral("pkexec"), arguments);
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
                Q_EMIT failed(tr("Nothing was changed. An administrator password is needed for this."));
            } else {
                Q_EMIT failed(tr("Wild OS could not change the clock. Try again in a moment."));
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
        Q_EMIT failed(tr("That is not a date and time Wild OS can set."));
        return;
    }
    call(QStringLiteral("SetTime"), {QVariant::fromValue<qlonglong>(when.toMSecsSinceEpoch() * 1000), false, true});
}
