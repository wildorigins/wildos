#pragma once

#include <QObject>
#include <QVariantMap>

// What the person's session is doing, as far as the cooler's screen cares:
// whether it is locked, whether it is the one in front (another account may
// have been switched to), and whether the PC has been left alone for a
// while. Everything arrives as a signal; nothing is polled.
class Session : public QObject
{
    Q_OBJECT

public:
    explicit Session(QObject *parent = nullptr);

    bool locked() const { return m_locked; }
    bool inFront() const { return m_inFront; }
    bool idle() const { return m_idle; }

    // Starts or stops waiting for the PC to be left alone. While it is not
    // watched, idle() is false.
    void watchIdle(bool watch);

Q_SIGNALS:
    void changed();
    void idleChanged();

private Q_SLOTS:
    void screenLocked(bool locked);
    void loginChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void setIdle(bool idle);

    bool m_locked = false;
    bool m_inFront = true;
    bool m_idle = false;
    int m_idleTimeout = -1;
};
