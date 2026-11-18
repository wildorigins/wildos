#include "session.h"

#include <KIdleTime>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace
{
// "After ten minutes" on board 10.9. WILDOS_COOLER_IDLE_SECONDS shortens it
// for tests.
int idleAfterMs()
{
    bool ok = false;
    const int seconds = qEnvironmentVariableIntValue("WILDOS_COOLER_IDLE_SECONDS", &ok);
    return ok && seconds > 0 ? seconds * 1000 : 10 * 60 * 1000;
}

const QString screenSaver = QStringLiteral("org.freedesktop.ScreenSaver");
const QString login = QStringLiteral("org.freedesktop.login1");
// logind's name for the session of whoever asks, which for a program
// started by the user's own systemd is the session on their screen.
const QString loginSession = QStringLiteral("/org/freedesktop/login1/session/auto");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
}

Session::Session(QObject *parent)
    : QObject(parent)
{
    // The lock screen says when it comes and goes. Asking for the state now
    // must not start anything, so the call may not launch its target.
    QDBusConnection session = QDBusConnection::sessionBus();
    session.connect(QString(), QStringLiteral("/ScreenSaver"), screenSaver, QStringLiteral("ActiveChanged"), this, SLOT(screenLocked(bool)));
    QDBusMessage ask = QDBusMessage::createMethodCall(screenSaver, QStringLiteral("/ScreenSaver"), screenSaver, QStringLiteral("GetActive"));
    ask.setAutoStartService(false);
    auto *asked = new QDBusPendingCallWatcher(session.asyncCall(ask), this);
    connect(asked, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<bool> reply = *watcher;
        if (!reply.isError()) {
            screenLocked(reply.value());
        }
    });

    QDBusConnection system = QDBusConnection::systemBus();
    system.connect(login, loginSession, properties, QStringLiteral("PropertiesChanged"), this, SLOT(loginChanged(QString, QVariantMap, QStringList)));
    QDBusMessage active = QDBusMessage::createMethodCall(login, loginSession, properties, QStringLiteral("Get"));
    active.setArguments({QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Active")});
    auto *answered = new QDBusPendingCallWatcher(system.asyncCall(active), this);
    connect(answered, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        // Without logind (a test container) the session counts as in front.
        if (!reply.isError()) {
            loginChanged({}, {{QStringLiteral("Active"), reply.value().variant()}}, {});
        }
    });

    connect(KIdleTime::instance(), &KIdleTime::timeoutReached, this, [this](int id) {
        if (id == m_idleTimeout) {
            KIdleTime::instance()->catchNextResumeEvent();
            setIdle(true);
        }
    });
    connect(KIdleTime::instance(), &KIdleTime::resumingFromIdle, this, [this]() {
        setIdle(false);
    });
}

void Session::screenLocked(bool locked)
{
    if (locked != m_locked) {
        m_locked = locked;
        Q_EMIT changed();
    }
}

void Session::loginChanged(const QString &, const QVariantMap &changed, const QStringList &)
{
    const auto active = changed.constFind(QStringLiteral("Active"));
    if (active != changed.constEnd() && active->toBool() != m_inFront) {
        m_inFront = active->toBool();
        Q_EMIT this->changed();
    }
}

void Session::watchIdle(bool watch)
{
    if (watch == (m_idleTimeout >= 0)) {
        return;
    }
    if (watch) {
        m_idleTimeout = KIdleTime::instance()->addIdleTimeout(idleAfterMs());
    } else {
        KIdleTime::instance()->removeIdleTimeout(m_idleTimeout);
        KIdleTime::instance()->stopCatchingResumeEvent();
        m_idleTimeout = -1;
        setIdle(false);
    }
}

void Session::setIdle(bool idle)
{
    if (idle != m_idle) {
        m_idle = idle;
        Q_EMIT idleChanged();
    }
}
