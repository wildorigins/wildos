#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQmlParserStatus>
#include <QQuickItem>

// Put inside a password field that can show what is typed (TextField's
// revealable): whatever is copied or cut from it goes on the clipboard
// marked x-kde-passwordManagerHint=secret, as password managers mark what
// they copy. The clipboard history leaves it out, and with Advanced
// protection's "Clipboard forgets passwords" on it is wiped after 60
// seconds. Ctrl+C, Ctrl+X, Ctrl+Insert and Shift+Delete are taken here;
// TextMenu calls copy() and cut() for its Copy and Cut.
//
//     TextField { revealable: true; UI.SecretCopy {} }
class SecretCopy : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(bool enabled MEMBER m_enabled NOTIFY enabledChanged)

public:
    explicit SecretCopy(QObject *parent = nullptr);

    void classBegin() override { }
    void componentComplete() override;

    Q_INVOKABLE void copy();
    Q_INVOKABLE void cut();

Q_SIGNALS:
    void enabledChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QQuickItem> m_field;
    bool m_enabled = true;
};
