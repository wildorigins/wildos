#include "notifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QSettings>

#include "composer.h"
#include "mailengine.h"
#include "messageview.h"

namespace
{
const QString service = QStringLiteral("org.freedesktop.Notifications");
const QString path = QStringLiteral("/org/freedesktop/Notifications");
}

Notifier::Notifier(QObject *parent)
    : QObject(parent)
{
    m_enabled = QSettings().value(QStringLiteral("notifications/mail"), true).toBool();
    connect(this, &Notifier::enabledChanged, this, [this]() {
        QSettings().setValue(QStringLiteral("notifications/mail"), m_enabled);
    });
    m_background = QSettings().value(QStringLiteral("notifications/background"), true).toBool();
    connect(this, &Notifier::backgroundChanged, this, [this]() {
        QSettings().setValue(QStringLiteral("notifications/background"), m_background);
    });
    connect(MailEngine::instance(), &MailEngine::newMail, this, &Notifier::onNewMail);
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(service, path, service, QStringLiteral("ActionInvoked"), this, SLOT(onAction(uint, QString)));
    bus.connect(service, path, service, QStringLiteral("NotificationReplied"), this, SLOT(onReplied(uint, QString)));
    bus.connect(service, path, service, QStringLiteral("NotificationClosed"), this, SLOT(onClosed(uint, uint)));
}

void Notifier::onNewMail(const QString &key, const QString &from, const QString &subject, const QString &preview)
{
    if (!m_enabled) {
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, service, QStringLiteral("Notify"));
    const QStringList actions = {QStringLiteral("default"), tr("Open"),
                                 QStringLiteral("inline-reply"), tr("Reply"),
                                 QStringLiteral("read"), tr("Mark as read"),
                                 QStringLiteral("archive"), tr("Archive")};
    const QVariantMap hints = {
        {QStringLiteral("desktop-entry"), QStringLiteral("org.wildos.mail")},
        {QStringLiteral("category"), QStringLiteral("email.arrived")},
        {QStringLiteral("x-kde-reply-placeholder-text"), tr("Type a reply")},
        {QStringLiteral("x-kde-reply-submit-button-text"), tr("Send")},
    };
    QString body = subject.toHtmlEscaped();
    if (!preview.isEmpty()) {
        body += QStringLiteral("\n") + preview.left(160).toHtmlEscaped();
    }
    call << QStringLiteral("Mail") << uint(0) << QStringLiteral("org.wildos.mail") << from << body << actions << hints << int(-1);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, key]() {
        QDBusPendingReply<uint> reply = *watcher;
        if (!reply.isError()) {
            m_keys.insert(reply.value(), key);
        }
        watcher->deleteLater();
    });
}

void Notifier::onAction(uint id, const QString &action)
{
    const QString key = m_keys.value(id);
    if (key.isEmpty()) {
        return;
    }
    if (action == QLatin1String("default")) {
        Q_EMIT openRequested(key);
    } else if (action == QLatin1String("read")) {
        MailEngine::instance()->setRead(key, true);
    } else if (action == QLatin1String("archive")) {
        MailEngine::instance()->archive(key);
    }
}

void Notifier::onReplied(uint id, const QString &text)
{
    const QString key = m_keys.value(id);
    if (!key.isEmpty() && !text.trimmed().isEmpty()) {
        quickReply(key, text);
    }
}

void Notifier::onClosed(uint id, uint)
{
    m_keys.remove(id);
}

// A reply typed in the notification goes out as an ordinary reply, with the
// original quoted under it, and the message counts as read.
void Notifier::quickReply(const QString &key, const QString &text)
{
    auto *view = new MessageView(this);
    connect(view, &MessageView::loaded, this, [this, view, key, text]() {
        if (view->loading()) {
            return;
        }
        if (view->error().isEmpty()) {
            Composer composer;
            composer.start(view->replyData(QStringLiteral("reply")));
            composer.setProperty("bodyText", text);
            composer.setProperty("bodyHtml", text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
            composer.send();
            MailEngine::instance()->setRead(key, true);
        }
        view->deleteLater();
    });
    view->setKey(key);
}
