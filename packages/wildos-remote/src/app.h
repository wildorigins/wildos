#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QUrl>

// What the home window asks of the rest of Remote: opening terminals, and
// hearing when the terminal window wants the home window (to check a host
// before a new tab, or to edit one).
class App : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT)
    // For the automated screen test only: WILDOS_REMOTE_TEST names a QML
    // file loaded into the home window that drives it step by step,
    // WILDOS_REMOTE_TEST_SHOT a command that saves the screen (given a name).
    Q_PROPERTY(QUrl testScript READ testScript CONSTANT)
    // Started by Windows mode for its desktop: the home window stays hidden
    // unless a question needs asking.
    Q_PROPERTY(bool desktopOnly READ desktopOnly CONSTANT)

public:
    static App *instance();
    static App *create(QQmlEngine *, QJSEngine *);

    QString version() const;
    bool desktopOnly() const { return m_desktopOnly; }
    void setDesktopOnly(bool on) { m_desktopOnly = on; }

    // After the host's identity checked out: a tab in the terminal window,
    // with the key unlocked first when the keyring has its passphrase.
    Q_INVOKABLE void openTerminal(const QString &hostId);
    Q_INVOKABLE void openLocal();
    // ssh-copy-id in a terminal tab: the server asks for its password
    // there, as it would in any terminal.
    Q_INVOKABLE void sendKey(const QString &keyPath, const QString &hostId);
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE QString homePath() const;
    // Full screen on the screen named, for All my monitors. Qt asks the
    // compositor for full screen on the screen a window is already on,
    // whatever screen it was given, so every monitor's window filled the
    // first screen. False when the window or screen can't be found.
    Q_INVOKABLE bool fullScreenOn(QQuickWindow *window, const QString &screenName);

    QUrl testScript() const;
    Q_INVOKABLE void testShot(const QString &name);
    // Types into the focused widget as a keyboard would, through its event
    // filters (the terminal's suggestions listen there).
    Q_INVOKABLE void testType(const QString &text);
    Q_INVOKABLE void testKey(int key);
    Q_INVOKABLE QObject *testSessionWindow() const;
    // Runs a shell command in the test rig (dropping a server mid-session).
    Q_INVOKABLE void testRun(const QString &command);

    // From the command line or a second launch: an ssh:// or rdp:// link.
    void openArgument(const QString &arg);

Q_SIGNALS:
    // The home window takes these over.
    void connectRequested(const QString &hostId);
    void editRequested(const QString &hostId);
    void quickConnectRequested(const QVariantMap &address);
    void raiseRequested();
    void settingsRequested();

private:
    explicit App(QObject *parent = nullptr);
    void fromSession(const QString &what);

    bool m_desktopOnly = false;
};
