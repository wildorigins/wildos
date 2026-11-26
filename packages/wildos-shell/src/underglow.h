#pragma once

#include "underglowsettings.h"

#include <KConfigWatcher>

#include <QColor>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>

// The taskbar underglow as KWin's org.wildos.underglow script draws it:
// what Settings chose in wildosrc [Underglow], and whether the effect may run
// now. Battery and Reduce motion are read here; whether an app is full
// screen only KWin knows, so the script sets fullScreen.
class Underglow : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged)
    // "accent", "custom" or "rgb".
    Q_PROPERTY(QString colour READ colour NOTIFY settingsChanged)
    Q_PROPERTY(QColor custom READ custom NOTIFY settingsChanged)
    // The chosen effect: "steady", "breathing" or "flowing".
    Q_PROPERTY(QString effect READ effect NOTIFY settingsChanged)
    Q_PROPERTY(bool fullScreen READ fullScreen WRITE setFullScreen NOTIFY fullScreenChanged)
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY onBatteryChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY reducedMotionChanged)
    // The effect to draw now, after the steady fallbacks.
    Q_PROPERTY(QString running READ running NOTIFY runningChanged)
    // Bumped whenever the floating taskbar's pills move; read with pills().
    Q_PROPERTY(int pillsRevision READ pillsRevision NOTIFY pillsChanged)

public:
    explicit Underglow(QObject *parent = nullptr);

    bool enabled() const { return m_settings.enabled; }
    QString colour() const { return UnderglowSettings::colourName(m_settings.colour); }
    QColor custom() const { return m_settings.custom; }
    QString effect() const { return UnderglowSettings::effectName(m_settings.effect); }
    bool fullScreen() const { return m_fullScreen; }
    void setFullScreen(bool fullScreen);
    bool onBattery() const { return m_onBattery; }
    bool reducedMotion() const { return m_reducedMotion; }
    QString running() const { return m_running; }
    int pillsRevision() const { return m_pillsRevision; }

    // One screen's floating pills as rects relative to its taskbar window,
    // from what the panel published (PillMask::publish); empty when unknown.
    Q_INVOKABLE QVariantList pills(const QString &screen) const;
    Q_INVOKABLE qreal pillRadius(const QString &screen) const;

    Q_INVOKABLE QList<qreal> glowRows(int reach) const { return UnderglowSettings::glowRows(reach); }
    Q_INVOKABLE QList<qreal> spectrumCopyRows(int reach) const { return UnderglowSettings::spectrumCopyRows(reach); }
    // 0 to 1 through the spectrum's six second slide, from the wall clock.
    Q_INVOKABLE qreal flowPhase() const { return UnderglowSettings::flowPhase(QDateTime::currentMSecsSinceEpoch()); }

Q_SIGNALS:
    void settingsChanged();
    void fullScreenChanged();
    void onBatteryChanged();
    void reducedMotionChanged();
    void runningChanged();
    void pillsChanged();

private Q_SLOTS:
    void upowerPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void readSettings();
    void readMotion();
    void readBattery();
    void setOnBattery(bool onBattery);
    void update();
    void readPills();

    KConfigWatcher::Ptr m_wildos;
    KConfigWatcher::Ptr m_globals;
    UnderglowSettings::Settings m_settings;
    bool m_fullScreen = false;
    bool m_onBattery = false;
    bool m_reducedMotion = false;
    QString m_running = QStringLiteral("steady");
    QFileSystemWatcher m_pillsWatcher;
    QJsonObject m_pills;
    int m_pillsRevision = 0;
};
