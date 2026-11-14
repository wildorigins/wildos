#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQmlParserStatus>
#include <QQuickItem>

// Put inside a text field to stop anything being pasted into it: Ctrl+V,
// Shift+Insert, a middle click (the selection) and text dropped on it. Wild
// OS's own password boxes (signing in, unlocking, approving a change, the
// disk password) take only what is typed, so a password never has to sit
// on the clipboard where any app can read it.
//
//     TextInput { echoMode: TextInput.Password; UI.NoPaste {} }
//
// TextMenu leaves Paste out of the right-click menu of a field that has one.
class NoPaste : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(bool enabled MEMBER m_enabled NOTIFY enabledChanged)

public:
    explicit NoPaste(QObject *parent = nullptr);

    void classBegin() override { }
    void componentComplete() override;

Q_SIGNALS:
    void enabledChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<QQuickItem> m_field;
    bool m_enabled = true;
};
