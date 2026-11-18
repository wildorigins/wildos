#include "firstboot.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>

#include <algorithm>

namespace
{
const QString fallbackLanguage = QStringLiteral("en_GB.UTF-8");

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// /etc/default/locale, as the installer (or SetLocale) wrote it.
QString localeSetting(const QString &key)
{
    const auto lines = readText(QStringLiteral("/etc/default/locale")).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(key + QLatin1Char('='))) {
            QString value = line.mid(key.size() + 1).trimmed();
            value.remove(QLatin1Char('"'));
            return value;
        }
    }
    return {};
}

QLocale localeOf(const QString &name)
{
    return QLocale(name.section(QLatin1Char('.'), 0, 0));
}

QString nameOf(const QString &locale)
{
    const QLocale l = localeOf(locale);
    if (l.language() == QLocale::C) {
        return {};
    }
    const QString language = QLocale::languageToString(l.language());
    return locale.contains(QLatin1Char('_')) ? QStringLiteral("%1 (%2)").arg(language, QLocale::territoryToString(l.territory())) : language;
}

int standardOffset(const QString &zone)
{
    const QTimeZone tz(zone.toUtf8());
    return tz.isValid() ? tz.standardTimeOffset(QDateTime::currentDateTimeUtc()) : 0;
}

// "(UTC+01:00) Paris", the way Windows lists zones, with the standard
// offset so the list does not change with summer time.
QString zoneText(const QString &zone)
{
    const int offset = standardOffset(zone);
    const int minutes = std::abs(offset) / 60;
    return QStringLiteral("(UTC%1%2:%3) %4")
        .arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
        .arg(minutes / 60, 2, 10, QLatin1Char('0'))
        .arg(minutes % 60, 2, 10, QLatin1Char('0'))
        .arg(zone.section(QLatin1Char('/'), -1).replace(QLatin1Char('_'), QLatin1Char(' ')));
}

QDBusMessage setupCall(const QString &method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.wildos.Setup1"), QStringLiteral("/org/wildos/Setup1"), QStringLiteral("org.wildos.Setup1"), method);
}

QString errorText(const QDBusError &error)
{
    // The service's own errors are already one plain sentence.
    if (error.name().startsWith(QLatin1String("org.wildos.Setup1."))) {
        return error.message();
    }
    return QCoreApplication::translate("FirstBoot", "Wild OS could not reach its setup service. Restart the computer to try again.");
}
}

FirstBoot::FirstBoot(QObject *parent)
    : QObject(parent)
    , m_wifi(new Wifi(this))
{
    readLocales();
    readZones();
    pickDefaults();

    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    m_computerName = QStringLiteral("DESKTOP-");
    for (int i = 0; i < 7; ++i) {
        m_computerName += QLatin1Char(alphabet[QRandomGenerator::system()->bounded(int(sizeof(alphabet) - 1))]);
    }

    // The preview shows the time now, in the chosen zone.
    m_clock.setInterval(15000);
    connect(&m_clock, &QTimer::timeout, this, &FirstBoot::previewChanged);
    m_clock.start();
}

void FirstBoot::readLocales()
{
    QSet<QString> seen;
    QList<QPair<QString, QString>> named;
    const auto lines = readText(QStringLiteral("/usr/share/i18n/SUPPORTED")).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString locale = line.section(QLatin1Char(' '), 0, 0);
        if (!locale.endsWith(QLatin1String(".UTF-8")) || locale.contains(QLatin1Char('@'))) {
            continue;
        }
        const QString name = nameOf(locale);
        if (name.isEmpty() || seen.contains(name)) {
            continue;
        }
        seen.insert(name);
        named.append({name, locale});
    }
    if (named.isEmpty()) {
        named.append({nameOf(fallbackLanguage), fallbackLanguage});
    }
    std::sort(named.begin(), named.end(), [](const auto &a, const auto &b) {
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });
    for (const auto &entry : std::as_const(named)) {
        m_languages.append(QVariantMap{{QStringLiteral("value"), entry.second}, {QStringLiteral("text"), entry.first}});
    }
}

void FirstBoot::readZones()
{
    QList<QString> ids;
    const auto lines = readText(QStringLiteral("/usr/share/zoneinfo/zone.tab")).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (line.startsWith(QLatin1Char('#')) || f.size() < 3 || !QTimeZone::isTimeZoneIdAvailable(f[2].toUtf8())) {
            continue;
        }
        if (!m_countryOfZone.contains(f[2])) {
            m_countryOfZone.insert(f[2], f[0]);
            ids << f[2];
        }
    }
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) {
        const int oa = standardOffset(a);
        const int ob = standardOffset(b);
        return oa != ob ? oa < ob : QString::localeAwareCompare(a.section(QLatin1Char('/'), -1), b.section(QLatin1Char('/'), -1)) < 0;
    });
    for (const QString &id : std::as_const(ids)) {
        m_zoneList.append(QVariantMap{{QStringLiteral("value"), id}, {QStringLiteral("text"), zoneText(id)}});
    }
}

static bool hasLanguage(const QVariantList &languages, const QString &value)
{
    return std::any_of(languages.cbegin(), languages.cend(), [&value](const QVariant &v) {
        return v.toMap().value(QStringLiteral("value")).toString() == value;
    });
}

// Everything as the installer left it: the language and its formats, and
// the zone when it set one. Without a zone, the formats' country gives it.
void FirstBoot::pickDefaults()
{
    m_installedLanguage = localeSetting(QStringLiteral("LANG"));
    m_installedFormats = localeSetting(QStringLiteral("LC_TIME"));
    if (m_installedFormats.isEmpty()) {
        m_installedFormats = m_installedLanguage;
    }
    m_language = hasLanguage(m_languages, m_installedLanguage) ? m_installedLanguage : fallbackLanguage;
    m_formats = hasLanguage(m_languages, m_installedFormats) ? m_installedFormats : m_language;

    const QString system = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    if (m_countryOfZone.contains(system)) {
        m_timeZone = system;
        m_zoneSource = QStringLiteral("installer");
        return;
    }
    const QString country = QLocale::territoryToCode(localeOf(m_formats).territory());
    // zone.tab lists a country's most populous zone first.
    const auto lines = readText(QStringLiteral("/usr/share/zoneinfo/zone.tab")).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QStringList f = line.split(QLatin1Char('\t'));
        if (f.size() >= 3 && f[0] == country && m_countryOfZone.contains(f[2])) {
            m_timeZone = f[2];
            break;
        }
    }
    if (m_timeZone.isEmpty()) {
        m_timeZone = QStringLiteral("Europe/London");
    }
    m_zoneSource = QStringLiteral("language");
}

void FirstBoot::setLanguage(const QString &language)
{
    if (language == m_language || !hasLanguage(m_languages, language)) {
        return;
    }
    // Formats follow the language until someone sets them apart.
    const bool together = m_formats == m_language;
    m_language = language;
    if (together) {
        m_formats = language;
    }
    Q_EMIT regionChanged();
    Q_EMIT previewChanged();
}

void FirstBoot::setFormats(const QString &formats)
{
    if (formats == m_formats || !hasLanguage(m_languages, formats)) {
        return;
    }
    m_formats = formats;
    Q_EMIT regionChanged();
    Q_EMIT previewChanged();
}

void FirstBoot::setTimeZone(const QString &zone)
{
    if (zone == m_timeZone || !m_countryOfZone.contains(zone)) {
        return;
    }
    m_timeZone = zone;
    m_zoneSource = QStringLiteral("chosen");
    Q_EMIT regionChanged();
    Q_EMIT previewChanged();
}

QVariantMap FirstBoot::preview() const
{
    const QLocale locale = localeOf(m_formats);
    const QTimeZone tz(m_timeZone.toUtf8());
    const QDateTime now = QDateTime::currentDateTimeUtc().toTimeZone(tz.isValid() ? tz : QTimeZone::UTC);
    QString measurement;
    switch (locale.measurementSystem()) {
    case QLocale::ImperialUKSystem:
        measurement = tr("Metric, with miles for distance");
        break;
    case QLocale::ImperialUSSystem:
        measurement = tr("US customary: inches, pounds and miles");
        break;
    default:
        measurement = tr("Metric");
    }
    return {
        {QStringLiteral("date"), locale.toString(now.date(), QLocale::LongFormat) + QStringLiteral(" · ") + locale.toString(now.date(), QLocale::ShortFormat)},
        {QStringLiteral("time"), locale.toString(now.time(), QLocale::ShortFormat)},
        {QStringLiteral("numbers"), locale.toString(1234.56, 'f', 2)},
        {QStringLiteral("currency"), locale.toCurrencyString(1234.56)},
        {QStringLiteral("measurements"), measurement},
    };
}

void FirstBoot::findTimeZone()
{
    if (m_lookupState == QLatin1String("busy")) {
        return;
    }
    m_lookupState = QStringLiteral("busy");
    Q_EMIT lookupChanged();

    auto *network = new QNetworkAccessManager(this);
    // The service the installer's Find it for me always used.
    QNetworkRequest request(QUrl(QStringLiteral("https://geoip.kde.org/v1/calamares")));
    request.setTransferTimeout(10000);
    QNetworkReply *reply = network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, network] {
        const QString zone = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("time_zone")).toString();
        const bool ok = reply->error() == QNetworkReply::NoError && m_countryOfZone.contains(zone);
        reply->deleteLater();
        network->deleteLater();
        if (ok) {
            m_timeZone = zone;
            m_zoneSource = QStringLiteral("internet");
            Q_EMIT regionChanged();
            Q_EMIT previewChanged();
        }
        m_lookupState = ok ? QStringLiteral("done") : QStringLiteral("failed");
        Q_EMIT lookupChanged();
    });
}

void FirstBoot::applyRegion()
{
    if (m_language == m_installedLanguage && m_formats == m_installedFormats) {
        applyZone();
        return;
    }
    QDBusMessage call = setupCall(QStringLiteral("SetLocale"));
    call << m_language << m_formats;
    // Making a locale the computer did not have takes a few seconds.
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<bool> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT regionApplied(false, errorText(reply.error()));
            return;
        }
        m_installedLanguage = m_language;
        m_installedFormats = m_formats;
        applyZone();
    });
}

void FirstBoot::applyZone()
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.timedate1"), QStringLiteral("/org/freedesktop/timedate1"),
                                                       QStringLiteral("org.freedesktop.timedate1"), QStringLiteral("SetTimezone"));
    call << m_timeZone << false;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 30000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            qWarning("could not set the time zone: %s", qPrintable(reply.error().message()));
            Q_EMIT regionApplied(false, tr("Wild OS could not set the time zone. You can set it later in Settings > Time and language."));
        } else {
            Q_EMIT regionApplied(true, QString());
        }
    });
}

bool FirstBoot::encrypted() const
{
    return QFile::exists(QStringLiteral("/etc/wildos/encrypted"));
}

QStringList FirstBoot::pictures() const
{
    QStringList list;
    if (QFile::exists(QStringLiteral("/usr/share/wildos/avatar-default.png"))) {
        list << QStringLiteral("/usr/share/wildos/avatar-default.png");
    }
    const QDir photos(QStringLiteral("/usr/share/plasma/avatars/photos"));
    const auto names = photos.entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        list << photos.filePath(name);
    }
    return list;
}

bool FirstBoot::validComputerName(const QString &name) const
{
    // The same rule the setup service applies: a network host name.
    static const QRegularExpression rule(QStringLiteral("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
    return rule.match(name).hasMatch();
}

void FirstBoot::setState(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    Q_EMIT stateChanged();
}

void FirstBoot::createAccount(const QString &realName, const QString &computerName, const QString &password, bool autoLogin, const QString &hint,
                              const QString &picture)
{
    if (m_state == QLatin1String("creating") || m_state == QLatin1String("finishing") || m_state == QLatin1String("signing-in")) {
        return;
    }
    setState(QStringLiteral("creating"));
    QDBusMessage call = setupCall(QStringLiteral("CreateAccount"));
    call << realName.simplified() << password << computerName << autoLogin << hint.trimmed() << picture;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 120000), this);
    const QString first = realName.simplified().section(QLatin1Char(' '), 0, 0);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, first] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            setState(QStringLiteral("failed"), errorText(reply.error()));
            return;
        }
        m_firstName = first;
        setState(QStringLiteral("created"));
        Q_EMIT accountCreated();
    });
}

void FirstBoot::fail(const QString &message)
{
    setState(QStringLiteral("failed"), message);
}

void FirstBoot::finish(const QVariantMap &plan)
{
    if (m_state == QLatin1String("finishing") || m_state == QLatin1String("signing-in")) {
        return;
    }
    setState(QStringLiteral("finishing"));
    QDBusMessage call = setupCall(QStringLiteral("Finish"));
    call << QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(plan)).toJson(QJsonDocument::Compact));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 60000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            setState(QStringLiteral("failed"), errorText(reply.error()));
            return;
        }
        // The sign-in screen restarts now and takes this session with it.
        setState(QStringLiteral("signing-in"));
        QTimer::singleShot(90000, this, [this] {
            setState(QStringLiteral("failed"), tr("Signing in is taking longer than it should. Restart the computer and sign in with your new password."));
        });
    });
}
