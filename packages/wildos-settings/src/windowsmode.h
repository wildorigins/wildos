#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

class QDBusInterface;

// Settings > Windows apps > Windows mode (WinModeSettings.dc). Everything
// comes from the Windows mode service, org.wildos.WindowsMode1 (package
// wildos-winmode): its status is one JSON object,
// re-read whenever the service says it changed. Calls that can fail answer
// with a sentence to show, or nothing.
class WindowsMode : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Whether wildos-winmode is installed at all.
    Q_PROPERTY(bool installed READ installed CONSTANT)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    // none, setup, failed or ready; empty until the service answers
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    // The memory limits this PC can offer: [{value, text}]
    Q_PROPERTY(QVariantList memoryChoices READ memoryChoices CONSTANT)
    // A product key is going in; it can take minutes (Windows starting,
    // then Microsoft's activation servers).
    Q_PROPERTY(bool activating READ activating NOTIFY activatingChanged)
    // The power action under way from Home's Windows mode card ("save",
    // "shutdown"), or empty. Saving or shutting down can take minutes.
    Q_PROPERTY(QString powerAction READ powerAction NOTIFY powerActionChanged)
    // Remove Windows mode is under way: the administrator's password, then
    // deleting Windows.
    Q_PROPERTY(bool removing READ removing NOTIFY removingChanged)

public:
    explicit WindowsMode(QObject *parent = nullptr);

    bool installed() const;
    QVariantMap status() const { return m_status; }
    QString state() const { return m_status.value(QStringLiteral("state")).toString(); }
    QVariantList memoryChoices() const;
    bool activating() const { return m_activating; }
    QString powerAction() const { return m_powerAction; }
    bool removing() const { return m_removing; }

    Q_INVOKABLE QString setOption(const QString &key, const QString &value);
    // Answers with productKeyDone: "" once activated, or why not.
    Q_INVOKABLE void setProductKey(const QString &key);
    Q_INVOKABLE bool keyShapeValid(const QString &key) const;
    Q_INVOKABLE QString checkUpdates();
    Q_INVOKABLE QString openDesktop();
    // Activating with a licence on the person's Microsoft account: Windows
    // mode adds a second Windows account just for it and opens the Windows
    // desktop signed in to it. status.activation follows it (state:
    // starting, open, ending or "", result: {activated, message}). Both
    // answer with accountActivationDone: "" or why not.
    Q_INVOKABLE void activateWithAccount();
    Q_INVOKABLE void endAccountActivation();
    // That account's password, which Windows asks for once while it becomes
    // a Microsoft account. "" when there is no such account.
    Q_INVOKABLE QString activationPassword();
    Q_INVOKABLE QString repair();
    // The Windows account's password, for when Windows itself asks for it
    // (activation, an installer that wants an administrator). "" when it
    // isn't in the keyring.
    Q_INVOKABLE QString windowsPassword(const QString &account) const;
    // Onto the clipboard marked as a secret, so clipboard history skips it.
    Q_INVOKABLE void copySecret(const QString &text) const;
    // Deletes Windows and its programs; the documents in the shared folders
    // stay. Answers with removeDone: "" when done, or why not.
    Q_INVOKABLE void remove();
    // The setup app (9.1 to 9.7).
    Q_INVOKABLE void openSetup();
    // pause, save, resume, start, restart or shutdown, as the taskbar's
    // Windows mode panel does. Answers with powerDone: "" when done, or a
    // sentence to show. The service asks first when work could be lost.
    Q_INVOKABLE void power(const QString &action);
    // "27.4 GB" from bytes; "" for nothing.
    Q_INVOKABLE QString size(double bytes) const;

Q_SIGNALS:
    void statusChanged();
    void activatingChanged();
    void productKeyDone(const QString &result);
    void accountActivationDone(const QString &result);
    void powerActionChanged();
    void powerDone(const QString &result);
    void removingChanged();
    void removeDone(const QString &result);

private Q_SLOTS:
    void refresh();

private:
    QString call(const QString &method, const QVariantList &args = {}, int timeoutMs = 25000);
    void callAsync(const QString &method, int timeoutMs, void (WindowsMode::*done)(const QString &));

    QDBusInterface *m_service = nullptr;
    QVariantMap m_status;
    bool m_activating = false;
    QString m_powerAction;
    bool m_removing = false;
};
