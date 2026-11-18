#pragma once

#include <QList>
#include <QPointer>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>

#include <memory>

class FileBrowser;
class QQuickWidget;
class QSplitter;
class QStackedWidget;
class TerminalView;
class WindowFrame;

// One tab of the terminal window (RemoteTerminal.dc, RemoteFiles.dc): a
// host over SSH, or a shell on this PC. A host's tab can be split into two
// terminals and can show its files instead of its terminal.
class SessionTab : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString colour READ colour CONSTANT)
    Q_PROPERTY(bool local READ local CONSTANT)
    Q_PROPERTY(QString hostId READ hostId CONSTANT)
    Q_PROPERTY(QString userHost READ userHost CONSTANT)
    Q_PROPERTY(QString auth READ auth CONSTANT)
    Q_PROPERTY(QStringList forwards READ forwards CONSTANT)
    Q_PROPERTY(QString latency READ latency NOTIFY changed)
    Q_PROPERTY(QString size READ size NOTIFY changed)
    Q_PROPERTY(bool ended READ ended NOTIFY changed)
    Q_PROPERTY(bool filesOpen READ filesOpen NOTIFY changed)
    Q_PROPERTY(bool split READ split NOTIFY changed)
    Q_PROPERTY(FileBrowser *files READ files CONSTANT)
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY suggestionsChanged)
    Q_PROPERTY(int selected READ selected NOTIFY suggestionsChanged)
    Q_PROPERTY(QRect cursor READ cursor NOTIFY suggestionsChanged)

public:
    SessionTab(const QVariantMap &host, QObject *parent = nullptr);
    ~SessionTab() override;

    QString title() const { return m_title; }
    QString colour() const { return m_colour; }
    bool local() const { return m_local; }
    QString hostId() const { return m_hostId; }
    QString userHost() const { return m_userHost; }
    QString auth() const { return m_auth; }
    QStringList forwards() const { return m_forwards; }
    QString latency() const { return m_latency; }
    QString size() const;
    bool ended() const;
    bool filesOpen() const { return m_filesOpen; }
    bool split() const { return m_views.size() > 1; }
    FileBrowser *files() const { return m_files; }
    QVariantList suggestions() const;
    int selected() const;
    QRect cursor() const;

    QWidget *page() const { return m_page; }
    TerminalView *current() const;
    // The ssh arguments that reach this host, ending with the destination.
    QStringList sshArgs(bool withForwards) const;

    Q_INVOKABLE void reconnect();
    Q_INVOKABLE void setFilesOpen(bool open);
    Q_INVOKABLE void toggleSplit();
    Q_INVOKABLE void accept(int index);
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void run(const QString &command);
    Q_INVOKABLE void focusTerminal();
    // The right-click menu's actions, on the pane it was opened on.
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void openLink();
    Q_INVOKABLE void copyLink();
    // The answer to pasteConfirmationNeeded.
    Q_INVOKABLE void answerPaste(bool paste);

Q_SIGNALS:
    void changed();
    void suggestionsChanged();
    // A right click in a terminal, in the page's coordinates (as cursor).
    void contextMenuRequested(int x, int y, bool canCopy, bool hasLink);
    // A paste of this many lines waits for answerPaste.
    void pasteConfirmationNeeded(int lines);
    void endedChanged();

private:
    TerminalView *makeView(bool first);
    struct SignIn;
    // A token for one ssh (AskPass), and the check that its password worked.
    QString signInToken();
    void confirmSignIn(const std::shared_ptr<SignIn> &signIn, int watch, const QString &password, bool save, int tries);
    void measureLatency();

    QString m_title;
    QString m_colour;
    bool m_local = false;
    QString m_hostId;
    QVariantMap m_host;
    QString m_passwordKey;
    QString m_userHost;
    QString m_auth;
    QStringList m_forwards;
    QString m_latency;
    bool m_filesOpen = false;
    QWidget *m_page = nullptr;
    QSplitter *m_splitter = nullptr;
    QList<TerminalView *> m_views;
    QPointer<TerminalView> m_current;
    // The link the right-click menu was opened on.
    QUrl m_menuLink;
    QPointer<TerminalView> m_pasteView;
    QString m_pasteText;
    FileBrowser *m_files = nullptr;
};

// The terminal window: tabs of hosts in the title bar, the terminals
// (widgets) below, and everything else (tab bar, snippets, the status bar,
// files, menus and dialogs) as QML in a transparent layer over them, as in
// Wild OS Terminal. The layer takes the pointer only where it draws.
class SessionWindow : public QWidget
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QList<QObject *> tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentChanged)
    Q_PROPERTY(SessionTab *currentTab READ currentTab NOTIFY currentChanged)
    Q_PROPERTY(bool maximised READ maximised NOTIFY stateChanged)
    Q_PROPERTY(bool windowActive READ windowActive NOTIFY stateChanged)
    Q_PROPERTY(bool blurAvailable READ blurAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool snippetsOpen READ snippetsOpen WRITE setSnippetsOpen NOTIFY layoutChanged)
    Q_PROPERTY(bool chromeFull READ chromeFull WRITE setChromeFull NOTIFY layoutChanged)
    Q_PROPERTY(QRect overlay READ overlay WRITE setOverlay NOTIFY layoutChanged)
    Q_PROPERTY(QRect terminalArea READ terminalArea NOTIFY layoutChanged)

public:
    static SessionWindow *instance(bool create = true);
    ~SessionWindow() override;

    static constexpr int titleHeight = 46;
    static constexpr int statusHeight = 32;
    static constexpr int snippetsWidth = 332;
    static constexpr int radius = 14;

    QList<QObject *> tabs() const;
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    SessionTab *currentTab() const;
    bool maximised() const;
    bool windowActive() const { return isActiveWindow(); }
    bool blurAvailable() const;
    bool snippetsOpen() const { return m_snippetsOpen; }
    void setSnippetsOpen(bool open);
    bool chromeFull() const { return m_chromeFull; }
    void setChromeFull(bool full);
    QRect overlay() const { return m_overlay; }
    void setOverlay(const QRect &rect);
    QRect terminalArea() const;

    // Opens a host (or this PC, for an empty map) in a new tab.
    void open(const QVariantMap &host);

    Q_INVOKABLE void openHost(const QString &hostId);
    Q_INVOKABLE void openLocal();
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void minimise();
    Q_INVOKABLE void toggleMaximised();
    Q_INVOKABLE void startMove();
    Q_INVOKABLE void startResize(int edges);
    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE void showHome();
    // Remote's settings, which are a page of the home window.
    Q_INVOKABLE void showSettings();
    Q_INVOKABLE void editHost(const QString &hostId);
    // Runs a snippet in every terminal tab.
    Q_INVOKABLE void runEverywhere(const QString &command);
    Q_INVOKABLE int terminalTabCount() const;
    // Keyboard to the layer over the terminals, for a dialog in it.
    Q_INVOKABLE void focusChrome();

Q_SIGNALS:
    void tabsChanged();
    void currentChanged();
    void stateChanged();
    void layoutChanged();
    void homeRequested(const QString &hostId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    SessionWindow();
    void layoutChildren();
    void updateMask();
    void updateBlur();

    QQuickWidget *m_chrome = nullptr;
    QStackedWidget *m_stack = nullptr;
    WindowFrame *m_frame = nullptr;
    QList<SessionTab *> m_tabs;
    int m_current = -1;
    bool m_snippetsOpen = true;
    bool m_chromeFull = false;
    QRect m_overlay;
};
