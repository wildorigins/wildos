#pragma once

#include <KConfigWatcher>
#include <KSharedConfig>
#include <QColor>
#include <QFont>
#include <QObject>
#include <QQmlEngine>
#include <QString>

// Remote's settings: its own few, kept in ~/.config/wildos-remote/
// settings.json, and its terminal's.
//
// Its own are what a newly added desktop starts with (each host then keeps
// its own choice) and whether ssh keeps an idle connection open.
//
// Its terminal has no settings of its own: it reads the ones Wild OS
// Terminal saves (wildosterminalrc) and follows them as they change, so a
// colour scheme, font or copy and paste habit chosen once holds in both. A
// setting nobody has changed is not in that file, and Remote then keeps
// what it has always done (its own colours, JetBrains Mono at 10.5, a block
// cursor, 10,000 lines, copying on select, no check before pasting), which
// is not always what Terminal does when left alone. Nothing is ever written
// to Terminal's file from here.
class RemoteSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "window", "full" or "all", as a host's own display setting.
    Q_PROPERTY(QString desktopDisplay READ desktopDisplay WRITE setDesktopDisplay NOTIFY changed)
    Q_PROPERTY(bool desktopClipboard READ desktopClipboard WRITE setDesktopClipboard NOTIFY changed)
    Q_PROPERTY(bool keepAlive READ keepAlive WRITE setKeepAlive NOTIFY changed)
    // Remote does not need Terminal installed; without it there are no
    // terminal settings to open.
    Q_PROPERTY(bool terminalInstalled READ terminalInstalled NOTIFY terminalInstalledChanged)

public:
    static RemoteSettings *instance();
    static RemoteSettings *create(QQmlEngine *, QJSEngine *);

    QString desktopDisplay() const { return m_desktopDisplay; }
    void setDesktopDisplay(const QString &display);
    bool desktopClipboard() const { return m_desktopClipboard; }
    void setDesktopClipboard(bool on);
    bool keepAlive() const { return m_keepAlive; }
    void setKeepAlive(bool on);
    bool terminalInstalled() const { return m_terminalInstalled; }
    // Looks again: Terminal can be installed or removed while Remote runs.
    Q_INVOKABLE void checkTerminal();
    // Terminal, with its settings in front.
    Q_INVOKABLE void openTerminalSettings();

    QString schemePath() const { return m_schemePath; }
    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    QFont font() const { return m_font; }
    // "block", "underline" or "bar".
    QString cursorShape() const { return m_cursorShape; }
    bool cursorBlink() const { return m_cursorBlink; }
    bool boldBright() const { return m_boldBright; }
    int scrollback() const { return m_scrollback; }
    bool copyOnSelect() const { return m_copyOnSelect; }
    bool pasteOnRightClick() const { return m_pasteOnRightClick; }
    // Ctrl+C copies a selection that has not been copied yet.
    bool ctrlCopies() const { return m_ctrlCopies; }
    bool ctrlPastes() const { return m_ctrlPastes; }
    bool warnMultilinePaste() const { return m_warnMultilinePaste; }

Q_SIGNALS:
    void changed();
    void terminalChanged();
    void terminalInstalledChanged();

private:
    explicit RemoteSettings(QObject *parent = nullptr);
    void readOwn();
    void writeOwn();
    void readTerminal();

    QString m_desktopDisplay = QStringLiteral("full");
    bool m_desktopClipboard = true;
    bool m_keepAlive = true;
    bool m_terminalInstalled = false;

    KSharedConfigPtr m_terminal;
    KConfigWatcher::Ptr m_watcher;

    QString m_schemePath;
    QColor m_background;
    QColor m_foreground;
    QFont m_font;
    QString m_cursorShape;
    bool m_cursorBlink = true;
    bool m_boldBright = true;
    int m_scrollback = 10000;
    bool m_copyOnSelect = true;
    bool m_pasteOnRightClick = false;
    bool m_ctrlCopies = false;
    bool m_ctrlPastes = true;
    bool m_warnMultilinePaste = false;
};
