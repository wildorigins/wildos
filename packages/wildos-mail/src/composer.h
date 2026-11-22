#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// A message being written. QML fills in the fields; send() builds the
// message, puts it in the Outbox and lets the engine send it, so a message
// written offline simply waits there. Drafts save themselves a moment after
// each change.
class Composer : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString accountId MEMBER m_accountId NOTIFY changed)
    Q_PROPERTY(QString to MEMBER m_to NOTIFY changed)
    Q_PROPERTY(QString cc MEMBER m_cc NOTIFY changed)
    Q_PROPERTY(QString bcc MEMBER m_bcc NOTIFY changed)
    Q_PROPERTY(QString subject MEMBER m_subject NOTIFY changed)
    Q_PROPERTY(QString bodyHtml MEMBER m_bodyHtml NOTIFY changed)
    Q_PROPERTY(QString bodyText MEMBER m_bodyText NOTIFY changed)
    Q_PROPERTY(QString quoteHeader MEMBER m_quoteHeader NOTIFY changed)
    Q_PROPERTY(QString quote MEMBER m_quote NOTIFY changed)
    Q_PROPERTY(QString inReplyTo MEMBER m_inReplyTo NOTIFY changed)
    Q_PROPERTY(QString references MEMBER m_references NOTIFY changed)
    // An answer to an invitation, as iCalendar (METHOD:REPLY), sent as a
    // version of the message calendar programs read.
    Q_PROPERTY(QString calendarReply MEMBER m_calendarReply NOTIFY changed)
    // An automatic reply: marked so (RFC 3834) other programs don't answer it.
    Q_PROPERTY(bool autoReply MEMBER m_autoReply NOTIFY changed)
    Q_PROPERTY(QVariantList attachments READ attachments NOTIFY attachmentsChanged)
    Q_PROPERTY(QString savedAt READ savedAt NOTIFY savedAtChanged)
    Q_PROPERTY(bool sending READ sending NOTIFY sendingChanged)

public:
    explicit Composer(QObject *parent = nullptr);

    QVariantList attachments() const;
    QString savedAt() const
    {
        return m_savedAt;
    }
    bool sending() const
    {
        return m_sending;
    }

    // Starts from a reply or forward (MessageView.replyData) or empty.
    Q_INVOKABLE void start(const QVariantMap &data);
    Q_INVOKABLE void addAttachment(const QUrl &file);
    Q_INVOKABLE void removeAttachment(int index);
    // Checks the addresses first; answers through problem() when one won't
    // do.
    Q_INVOKABLE bool send();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void touch();

Q_SIGNALS:
    void changed();
    void attachmentsChanged();
    void savedAtChanged();
    void sendingChanged();
    void sent();
    void problem(const QString &text);

private:
    struct Attachment {
        QString name;
        QString mimeType;
        QByteArray data;
        bool photoCleaned = false;
        // A photo whose details couldn't be taken out, said so on its tile.
        bool photoUncleaned = false;
    };

    // The message as bytes, ready to send or file as a draft.
    QByteArray build(bool draft, int *cleanedPhotos) const;
    void saveDraft();
    void deleteDraft(const QString &uid);

    QString m_accountId;
    QString m_to, m_cc, m_bcc, m_subject, m_bodyHtml, m_bodyText, m_quoteHeader, m_quote, m_inReplyTo, m_references;
    QString m_calendarReply;
    QList<Attachment> m_attachments;
    QString m_draftUid;
    QString m_savedAt;
    bool m_sending = false;
    bool m_started = false;
    bool m_autoReply = false;
    QTimer m_draftTimer;
};
