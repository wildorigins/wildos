#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>

class Restic;

// Setting a drive up: open it, then either make a new encrypted
// repository in a "WildOS Backup" folder on it, with a password made here
// and kept in the keyring, or join the backups already on it (asking for
// their password if the keyring doesn't have it, as on a new PC). Also
// showing the password, and checking that backups can still be read.
class Setup : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "idle", "working", "needPassword", "created" (show the password to
    // write down), "added" or "failed"
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString step READ step NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
    Q_PROPERTY(QString driveName READ driveName NOTIFY changed)
    // Only while it is being shown.
    Q_PROPERTY(QString password READ password NOTIFY passwordChanged)
    Q_PROPERTY(bool revealing READ revealing NOTIFY passwordChanged)
    Q_PROPERTY(QString revealError READ revealError NOTIFY passwordChanged)

    Q_PROPERTY(bool checking READ checking NOTIFY checkChanged)
    Q_PROPERTY(QString checkResult READ checkResult NOTIFY checkChanged)
    Q_PROPERTY(QString checkDetail READ checkDetail NOTIFY checkChanged)
    Q_PROPERTY(bool checkOk READ checkOk NOTIFY checkChanged)

public:
    explicit Setup(QObject *parent);
    ~Setup() override;
    static Setup *instance();
    static Setup *create(QQmlEngine *, QJSEngine *);

    QString state() const
    {
        return m_state;
    }
    QString step() const
    {
        return m_step;
    }
    QString error() const
    {
        return m_error;
    }
    // The backup tool's own words, for Details, when `error` (or
    // `checkResult`) is only the general sentence.
    QString errorDetail() const
    {
        return m_errorDetail;
    }
    QString checkDetail() const
    {
        return m_checkDetail;
    }
    QString driveName() const
    {
        return m_driveName;
    }
    QString password() const
    {
        return QString::fromUtf8(m_shown);
    }
    bool revealing() const
    {
        return m_revealing;
    }
    QString revealError() const
    {
        return m_revealError;
    }
    bool checking() const
    {
        return m_checking;
    }
    QString checkResult() const
    {
        return m_checkResult;
    }
    bool checkOk() const
    {
        return m_checkOk;
    }

    Q_INVOKABLE void useDrive(const QString &uuid);
    Q_INVOKABLE void givePassword(const QString &password);
    // Done with the dialog: a new drive's first backup starts.
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void reveal();
    Q_INVOKABLE void hidePassword();
    Q_INVOKABLE void check();

Q_SIGNALS:
    void changed();
    void passwordChanged();
    void checkChanged();

private:
    void setState(const QString &state, const QString &step = QString());
    void failed(const QString &error, const QString &detail = QString());
    void opened(const QString &mountPoint, const QString &error);
    void verify(const QByteArray &password, bool fromKeyring);
    void createRepository();
    void makeFolderAsAdmin(const QString &mountPoint);
    void initRepository();
    void added(bool created);

    QString m_state = QStringLiteral("idle");
    QString m_step;
    QString m_error;
    QString m_errorDetail;
    QString m_uuid;
    QString m_driveName;
    QString m_kind;
    QString m_repo;
    QByteArray m_password;
    QByteArray m_shown;
    bool m_revealing = false;
    QString m_revealError;
    bool m_checking = false;
    bool m_checkOk = false;
    QString m_checkResult;
    QString m_checkDetail;
    QPointer<Restic> m_restic;
};
