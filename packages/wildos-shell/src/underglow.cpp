#include "underglow.h"

#include "pillmask.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRectF>

namespace
{
const QString upowerService = QStringLiteral("org.freedesktop.UPower");
const QString upowerPath = QStringLiteral("/org/freedesktop/UPower");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
}

Underglow::Underglow(QObject *parent)
    : QObject(parent)
    , m_wildos(KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("wildosrc"))))
    , m_globals(KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kdeglobals"))))
{
    connect(m_wildos.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == QLatin1String("Underglow")) {
            readSettings();
        }
    });
    connect(m_globals.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == QLatin1String("KDE")) {
            readMotion();
        }
    });

    // UPower sends OnBattery changes as a properties signal; a laptop
    // unplugged mid-breath settles to steady straight away.
    QDBusConnection::systemBus().connect(upowerService, upowerPath, propertiesInterface, QStringLiteral("PropertiesChanged"), this,
                                         SLOT(upowerPropertiesChanged(QString, QVariantMap, QStringList)));

    // The panel replaces the file rather than writing into it, so the
    // directory is what changes; the file itself is watched once it exists.
    m_pillsWatcher.addPath(QFileInfo(PillMask::pillsPath()).absolutePath());
    connect(&m_pillsWatcher, &QFileSystemWatcher::directoryChanged, this, &Underglow::readPills);
    connect(&m_pillsWatcher, &QFileSystemWatcher::fileChanged, this, &Underglow::readPills);

    readSettings();
    readMotion();
    readBattery();
    readPills();
}

void Underglow::readPills()
{
    const QString path = PillMask::pillsPath();
    QJsonObject pills;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        pills = QJsonDocument::fromJson(file.readAll()).object();
        if (!m_pillsWatcher.files().contains(path)) {
            m_pillsWatcher.addPath(path);
        }
    }
    if (pills != m_pills) {
        m_pills = pills;
        ++m_pillsRevision;
        Q_EMIT pillsChanged();
    }
}

QVariantList Underglow::pills(const QString &screen) const
{
    QVariantList result;
    const QJsonArray pills = m_pills.value(screen).toObject().value(QStringLiteral("pills")).toArray();
    for (const QJsonValue &value : pills) {
        const QJsonArray r = value.toArray();
        if (r.size() == 4) {
            result.append(QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble()));
        }
    }
    return result;
}

qreal Underglow::pillRadius(const QString &screen) const
{
    return m_pills.value(screen).toObject().value(QStringLiteral("radius")).toDouble();
}

void Underglow::readSettings()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    config->reparseConfiguration();
    const KConfigGroup group = config->group(QStringLiteral("Underglow"));
    const UnderglowSettings::Settings settings = UnderglowSettings::parse(group.readEntry("Enabled", false),
                                                                          group.readEntry("Colour", QString()),
                                                                          group.readEntry("Custom", QString()),
                                                                          group.readEntry("Effect", QString()));
    if (settings.enabled != m_settings.enabled || settings.colour != m_settings.colour || settings.custom != m_settings.custom
        || settings.effect != m_settings.effect) {
        m_settings = settings;
        Q_EMIT settingsChanged();
        update();
    }
}

void Underglow::readMotion()
{
    // Settings > Accessibility > Reduce motion sets KDE's animation speed to
    // instant, the same key WildOS.UI's Theme follows.
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    config->reparseConfiguration();
    const bool reduced = config->group(QStringLiteral("KDE")).readEntry("AnimationDurationFactor", 1.0) <= 0.0;
    if (reduced != m_reducedMotion) {
        m_reducedMotion = reduced;
        Q_EMIT reducedMotionChanged();
        update();
    }
}

void Underglow::readBattery()
{
    QDBusMessage call = QDBusMessage::createMethodCall(upowerService, upowerPath, propertiesInterface, QStringLiteral("Get"));
    call << upowerService << QStringLiteral("OnBattery");
    // Asynchronous, so a missing or slow UPower never holds up KWin; until
    // it answers the glow counts as on mains power.
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (!reply.isError()) {
            setOnBattery(reply.value().variant().toBool());
        }
    });
}

void Underglow::upowerPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != upowerService) {
        return;
    }
    const auto it = changed.constFind(QStringLiteral("OnBattery"));
    if (it != changed.constEnd()) {
        setOnBattery(it->toBool());
    } else if (invalidated.contains(QLatin1String("OnBattery"))) {
        readBattery();
    }
}

void Underglow::setOnBattery(bool onBattery)
{
    if (onBattery != m_onBattery) {
        m_onBattery = onBattery;
        Q_EMIT onBatteryChanged();
        update();
    }
}

void Underglow::setFullScreen(bool fullScreen)
{
    if (fullScreen != m_fullScreen) {
        m_fullScreen = fullScreen;
        Q_EMIT fullScreenChanged();
        update();
    }
}

void Underglow::update()
{
    const QString running = UnderglowSettings::effectName(UnderglowSettings::running(m_settings, m_onBattery, m_fullScreen, m_reducedMotion));
    if (running != m_running) {
        m_running = running;
        Q_EMIT runningChanged();
    }
}
