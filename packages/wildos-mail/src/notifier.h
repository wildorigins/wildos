#pragma once

#include <QHash>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// New mail notifications through the desktop's notification server, with
// Mark as read, Archive and a reply typed right in the notification
// (MailCalMsNotify). Choosing the notification itself opens the message.
class Notifier : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool enabled MEMBER m_enabled NOTIFY enabledChanged)
    // Whether notifications carry on while Mail's window is closed
    // (wildos-mail --background).
    Q_PROPERTY(bool background MEMBER m_background NOTIFY backgroundChanged)

public:
    explicit Notifier(QObject *parent = nullptr);

Q_SIGNALS:
    void enabledChanged();
    void backgroundChanged();
    void openRequested(const QString &key);

private Q_SLOTS:
    void onNewMail(const QString &key, const QString &from, const QString &subject, const QString &preview);
    void onAction(uint id, const QString &action);
    void onReplied(uint id, const QString &text);
    void onClosed(uint id, uint reason);

private:
    void quickReply(const QString &key, const QString &text);

    QHash<uint, QString> m_keys;
    bool m_enabled = true;
    bool m_background = true;
};
