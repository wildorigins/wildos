#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

// The lights of this PC, for the Lighting page.
//
// Cooler's window never talks to a light. Everything here is asked of
// wildos-lighting over the session bus (io.wildos.Lighting): it is the one
// program that sets lights, through OpenRGB's server on the loopback, and
// it keeps the effect running and the names, shapes and counts saved with
// this window closed. See packages/wildos-lighting/src/lights.h.
class Lighting : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "missing" (wildos-lighting is not running, which is how it is
    // without OpenRGB installed), "off" (the lighting switch is off),
    // "starting", "ready", or "failed" (OpenRGB did not start).
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // { effect, brightness, speed, offWhenLocked, effects: [{ id, offered, why }] }
    Q_PROPERTY(QVariantMap everything READ everything NOTIFY changed)
    // [{ key, kind, name, controlled, why, white, still, own, effect,
    //    brightness, speed, effects, lights,
    //    zones: [{ index, reported, name, shape, lights, resizable, min,
    //              max, split, white, flashing }] }]
    Q_PROPERTY(QVariantList parts READ parts NOTIFY changed)
    // For each part, for each zone, the colour of each light as it is now.
    Q_PROPERTY(QVariantList colours READ colours NOTIFY coloursChanged)
    // The page is on screen: only then are moving colours followed.
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY watchingChanged)

public:
    explicit Lighting(QObject *parent = nullptr);

    QString state() const { return m_state; }
    QVariantMap everything() const { return m_everything; }
    QVariantList parts() const { return m_parts; }
    QVariantList colours() const { return m_colours; }
    bool watching() const { return m_watching; }
    void setWatching(bool watching);

    Q_INVOKABLE void turnOn();
    Q_INVOKABLE void setEverything(const QString &effect, int brightness, int speed);
    Q_INVOKABLE void setOffWhenLocked(bool off);
    Q_INVOKABLE void setPartOwn(const QString &part, bool own);
    Q_INVOKABLE void setPart(const QString &part, const QString &effect, int brightness, int speed);
    Q_INVOKABLE void setZoneName(const QString &part, int zone, const QString &name);
    Q_INVOKABLE void setZoneShape(const QString &part, int zone, const QString &shape);
    Q_INVOKABLE void setZoneCount(const QString &part, int zone, int count);
    Q_INVOKABLE void setZoneSplit(const QString &part, int zone, int sections);
    // Zone -1 is every zone of the part.
    Q_INVOKABLE void flash(const QString &part, int zone);

Q_SIGNALS:
    void changed();
    void coloursChanged();
    void watchingChanged();

private Q_SLOTS:
    void refresh();

private:
    void call(const QString &method, const QVariantList &arguments);
    void later(const QString &method, const QVariantList &arguments);
    void fetchColours();
    void setMissing();
    void updatePolling();

    QString m_state = QStringLiteral("missing");
    QVariantMap m_everything;
    QVariantList m_parts;
    QVariantList m_colours;
    bool m_moving = false;
    bool m_watching = false;
    bool m_asking = false;
    QTimer m_poll;
    // A slider being dragged asks many times a second; the newest of each
    // kind goes out when this fires.
    QTimer m_send;
    QList<QPair<QString, QVariantList>> m_pending;
};
