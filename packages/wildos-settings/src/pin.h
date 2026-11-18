#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

#include <functional>

// The PIN for the sign-in screen and the lock screen, for Settings >
// Accounts > Sign-in options. The PIN
// is kept and checked by wildos-pin, a root service behind
// /run/wildos/pin.socket that only ever answers for the account asking;
// nothing here keeps or logs the PIN or the password.
class Pin : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // The service is there to ask (wildos-system-dialogs 0.1.20).
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // This account has a PIN.
    Q_PROPERTY(bool isSet READ isSet NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
    explicit Pin(QObject *parent = nullptr);

    bool available() const { return m_available; }
    bool isSet() const { return m_set; }
    bool busy() const { return m_busy; }

    // 4 to 12 digits.
    Q_INVOKABLE bool valid(const QString &pin) const;
    // Sets or changes the PIN; the account password is checked first, with
    // the same delay after a wrong one as anywhere else.
    Q_INVOKABLE void save(const QString &password, const QString &pin);
    Q_INVOKABLE void remove();

Q_SIGNALS:
    void changed();
    // "set" or "removed".
    void done(const QString &what);
    void failed(const QString &reason);

private:
    void ask(const QStringList &lines, std::function<void(const QString &)> answered);
    void refresh();

    bool m_available = false;
    bool m_set = false;
    bool m_busy = false;
};
