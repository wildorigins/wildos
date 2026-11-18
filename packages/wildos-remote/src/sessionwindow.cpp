#include "sessionwindow.h"
#include "askpass.h"
#include "filebrowser.h"
#include "hosts.h"
#include "keychain.h"
#include "paths.h"
#include "secrets.h"
#include "settings.h"
#include "terminalview.h"
#include "windowframe.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace
{
constexpr int inset = 6;
constexpr int grip = 5;
}

SessionTab::SessionTab(const QVariantMap &host, QObject *parent)
    : QObject(parent)
    , m_local(host.isEmpty())
    , m_hostId(host.value(QStringLiteral("id")).toString())
    , m_host(host)
{
    m_page = new QWidget;
    m_page->setAttribute(Qt::WA_TranslucentBackground);
    auto *layout = new QVBoxLayout(m_page);
    layout->setContentsMargins(0, 0, 0, 0);
    m_splitter = new QSplitter(Qt::Horizontal, m_page);
    m_splitter->setHandleWidth(2);
    m_splitter->setStyleSheet(QStringLiteral("QSplitter::handle { background: rgba(255,255,255,0.08); }"));
    layout->addWidget(m_splitter);

    if (m_local) {
        m_title = tr("Local");
        m_colour = QStringLiteral("#8b93a8");
        m_userHost = qEnvironmentVariable("USER") + QLatin1Char('@') + QSysInfo::machineHostName();
        m_auth = tr("This PC");
    } else {
        Hosts *hosts = Hosts::instance();
        m_title = host.value(QStringLiteral("name")).toString();
        const QVariantMap ssh = host.value(QStringLiteral("ssh")).toMap();
        m_colour = ssh.value(QStringLiteral("colour")).toString();
        const QString user = host.value(QStringLiteral("user")).toString();
        m_userHost = (user.isEmpty() ? QString() : user + QLatin1Char('@')) + hosts->hostName(host);
        const QString identity = ssh.value(QStringLiteral("identity")).toString();
        m_auth = identity.isEmpty() ? QStringLiteral("SSH") : tr("SSH, key “%1”").arg(Keychain::instance()->label(identity));
        for (const QVariant &v : ssh.value(QStringLiteral("forwards")).toList()) {
            const QVariantMap f = v.toMap();
            const QString target = f.value(QStringLiteral("remoteHost")).toString();
            m_forwards.append(tr("localhost:%1 to %2").arg(f.value(QStringLiteral("localPort")).toInt())
                                  .arg(target.isEmpty() || target == QLatin1String("localhost") ? m_title : target));
        }
        m_passwordKey = hosts->passwordKey(host);
        m_files = new FileBrowser(m_title, this);
        measureLatency();
    }
    makeView(true);
}

SessionTab::~SessionTab()
{
    delete m_page;
}

QStringList SessionTab::sshArgs(bool withForwards) const
{
    Hosts *hosts = Hosts::instance();
    const QVariantMap ssh = m_host.value(QStringLiteral("ssh")).toMap();
    QStringList args{
        // Every tab, split and file view to the same host shares one
        // connection: one sign-in, and the file view needs no other.
        QStringLiteral("-o"), QStringLiteral("ControlMaster=auto"),
        QStringLiteral("-o"), QStringLiteral("ControlPath=") + Paths::runtime() + QStringLiteral("/cm-%C"),
        QStringLiteral("-o"), QStringLiteral("ControlPersist=no"),
        // Only a key already in known_hosts: Remote asked about anything
        // new before this ran, and a changed key is never taken.
        QStringLiteral("-o"), QStringLiteral("StrictHostKeyChecking=yes"),
        QStringLiteral("-o"), QStringLiteral("UpdateHostKeys=no"),
        QStringLiteral("-p"), QString::number(hosts->port(m_host)),
    };
    // A quiet check every 30 seconds, so a router or firewall does not drop
    // a tab left alone; it can be switched off in Remote's settings.
    if (RemoteSettings::instance()->keepAlive()) {
        args << QStringLiteral("-o") << QStringLiteral("ServerAliveInterval=30");
    }
    const QString identity = ssh.value(QStringLiteral("identity")).toString();
    if (!identity.isEmpty()) {
        args << QStringLiteral("-i") << identity << QStringLiteral("-o") << QStringLiteral("IdentitiesOnly=yes");
    }
    if (withForwards) {
        for (const QVariant &v : ssh.value(QStringLiteral("forwards")).toList()) {
            const QVariantMap f = v.toMap();
            QString target = f.value(QStringLiteral("remoteHost")).toString();
            if (target.isEmpty()) {
                target = QStringLiteral("localhost");
            }
            // Bound to this PC only, never to the network.
            args << QStringLiteral("-L")
                 << QStringLiteral("127.0.0.1:%1:%2:%3").arg(f.value(QStringLiteral("localPort")).toInt()).arg(target).arg(f.value(QStringLiteral("remotePort")).toInt());
        }
    }
    const QString user = m_host.value(QStringLiteral("user")).toString();
    if (!user.isEmpty()) {
        args << QStringLiteral("-l") << user;
    }
    args << QStringLiteral("--") << hosts->hostName(m_host);
    return args;
}

TerminalView *SessionTab::makeView(bool first)
{
    TerminalView *view;
    if (m_local) {
        QString shell = qEnvironmentVariable("SHELL");
        if (shell.isEmpty()) {
            shell = QStringLiteral("/bin/bash");
        }
        view = new TerminalView(shell, {}, QStringLiteral("local"));
    } else if (const QStringList exec = m_host.value(QStringLiteral("exec")).toStringList(); !exec.isEmpty()) {
        view = new TerminalView(exec.first(), exec.mid(1), QString());
    } else {
        const QString token = signInToken();
        view = new TerminalView(QStringLiteral("ssh"), sshArgs(first), m_hostId, AskPass::instance()->environment(token));
        connect(view, &QObject::destroyed, AskPass::instance(), [token] {
            AskPass::instance()->release(token);
        });
    }
    m_splitter->addWidget(view);
    m_views.append(view);
    m_current = view;
    connect(view, &TerminalView::suggestionsChanged, this, &SessionTab::suggestionsChanged);
    connect(view, &TerminalView::focused, this, [this, view] {
        m_current = view;
        Q_EMIT suggestionsChanged();
    });
    connect(view, &TerminalView::contextMenuWanted, this, [this, view](const QPoint &position) {
        m_current = view;
        m_menuLink = view->linkAt(position);
        const QPoint p = view->mapTo(m_page, position);
        Q_EMIT contextMenuRequested(p.x(), p.y(), view->hasSelection(), m_menuLink.isValid());
    });
    connect(view, &TerminalView::pasteNeedsConfirming, this, [this, view](const QString &text) {
        m_pasteView = view;
        m_pasteText = text;
        QString body = text.trimmed();
        body.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        body.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        Q_EMIT pasteConfirmationNeeded(int(body.count(QLatin1Char('\n'))) + 1);
    });
    connect(view, &TerminalView::exitedChanged, this, [this, view] {
        // A second pane that ends just goes; the tab ends with its last.
        if (m_views.size() > 1) {
            m_views.removeOne(view);
            view->deleteLater();
            m_current = m_views.constFirst();
        }
        Q_EMIT changed();
        Q_EMIT endedChanged();
    });
    return view;
}

TerminalView *SessionTab::current() const
{
    return m_current ? m_current.data() : (m_views.isEmpty() ? nullptr : m_views.constFirst());
}

QString SessionTab::size() const
{
    if (TerminalView *v = current()) {
        return QStringLiteral("%1 × %2").arg(v->screenColumnsCount()).arg(v->screenLinesCount());
    }
    return {};
}

bool SessionTab::ended() const
{
    return !m_views.isEmpty() && m_views.constFirst()->exited() && m_views.size() == 1;
}

QVariantList SessionTab::suggestions() const
{
    TerminalView *v = current();
    return v ? v->suggestions() : QVariantList();
}

int SessionTab::selected() const
{
    TerminalView *v = current();
    return v ? v->selected() : 0;
}

QRect SessionTab::cursor() const
{
    TerminalView *v = current();
    if (!v) {
        return {};
    }
    const QRect r = v->cursorRect();
    // In the page's coordinates, which the window places in the terminal area.
    return QRect(v->mapTo(m_page, r.topLeft()), r.size());
}

void SessionTab::reconnect()
{
    for (TerminalView *v : std::as_const(m_views)) {
        v->deleteLater();
    }
    m_views.clear();
    makeView(true);
    focusTerminal();
    if (m_files && m_filesOpen) {
        m_files->retry();
    }
    Q_EMIT changed();
}

void SessionTab::setFilesOpen(bool open)
{
    if (m_local || open == m_filesOpen) {
        return;
    }
    m_filesOpen = open;
    if (open && !m_files->remoteReady()) {
        m_files->connectTo(sshArgs(false), m_passwordKey);
    }
    m_splitter->setVisible(!open);
    Q_EMIT changed();
}

void SessionTab::toggleSplit()
{
    if (m_views.size() > 1) {
        TerminalView *last = m_views.takeLast();
        last->deleteLater();
        m_current = m_views.constFirst();
    } else {
        makeView(false);
    }
    focusTerminal();
    Q_EMIT changed();
}

void SessionTab::accept(int index)
{
    if (TerminalView *v = current()) {
        v->accept(index);
    }
}

void SessionTab::dismiss()
{
    if (TerminalView *v = current()) {
        v->dismiss();
    }
}

void SessionTab::run(const QString &command)
{
    if (TerminalView *v = current()) {
        v->run(command);
        v->setFocus();
    }
}

void SessionTab::focusTerminal()
{
    if (TerminalView *v = current()) {
        v->setFocus();
    }
}

void SessionTab::copy()
{
    if (TerminalView *v = current()) {
        v->copy();
    }
}

void SessionTab::paste()
{
    if (TerminalView *v = current()) {
        v->paste();
        // A paste that is being checked first has the keyboard in its
        // dialog; the terminal gets it back with the answer.
        if (m_pasteText.isEmpty()) {
            v->setFocus();
        }
    }
}

void SessionTab::answerPaste(bool paste)
{
    if (paste && m_pasteView) {
        m_pasteView->sendPaste(m_pasteText);
    }
    m_pasteText.clear();
    m_pasteView = nullptr;
    focusTerminal();
}

void SessionTab::clear()
{
    if (TerminalView *v = current()) {
        v->clear();
        v->setFocus();
    }
}

void SessionTab::openLink()
{
    if (m_menuLink.isValid()) {
        QDesktopServices::openUrl(m_menuLink);
    }
}

void SessionTab::copyLink()
{
    if (m_menuLink.isValid()) {
        // As written in the terminal: the default form decodes parts of an
        // address, and a sign-in link has to arrive as it was sent.
        QGuiApplication::clipboard()->setText(m_menuLink.toString(QUrl::FullyEncoded));
    }
}

void SessionTab::measureLatency()
{
    // One TCP handshake, timed, when the tab opens: the round trip to the
    // server, without keeping anything open.
    auto *socket = new QTcpSocket(this);
    auto *clock = new QElapsedTimer;
    clock->start();
    // Looking the name up isn't the round trip.
    connect(socket, &QTcpSocket::hostFound, this, [clock] {
        clock->restart();
    });
    connect(socket, &QTcpSocket::connected, this, [this, socket, clock] {
        m_latency = tr("%1 ms").arg(std::max<qint64>(1, clock->elapsed()));
        delete clock;
        socket->abort();
        socket->deleteLater();
        Q_EMIT changed();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [socket, clock] {
        delete clock;
        socket->deleteLater();
    });
    socket->connectToHost(Hosts::instance()->hostName(m_host), quint16(Hosts::instance()->port(m_host)));
}

// One ssh's sign-in. The first password prompt is answered from the
// keyring when one is saved; the window asks for anything else, and for the
// password again when the server turns it down.
struct SessionTab::SignIn {
    int passwords = 0;
    // A password is saved for this account.
    bool saved = false;
    // The last password ssh was given came from the keyring.
    bool fromKeyring = false;
    // Moves on whenever an answer being checked is overtaken.
    int watch = 0;
};

QString SessionTab::signInToken()
{
    auto signIn = std::make_shared<SignIn>();
    const QVariantMap details{
        {QStringLiteral("save"), true},
        {QStringLiteral("hostId"), m_hostId},
        {QStringLiteral("name"), m_title},
        {QStringLiteral("address"), m_userHost},
    };
    AskPass::Options options;
    options.title = m_title;
    options.context = QStringLiteral("terminal");
    options.provider = [key = m_passwordKey, signIn, details](const QString &prompt, int, const std::function<void(const AskPass::Lookup &)> &done) {
        if (!AskPass::isPassword(prompt)) {
            done({});
            return;
        }
        ++signIn->passwords;
        // Asked again: the answer being checked was wrong.
        ++signIn->watch;
        const bool keyringFailed = std::exchange(signIn->fromKeyring, false);
        auto ask = [signIn, details, done, keyringFailed] {
            QVariantMap d = details;
            d.insert(QStringLiteral("saved"), signIn->saved);
            d.insert(QStringLiteral("retry"), signIn->passwords > 1);
            d.insert(QStringLiteral("savedFailed"), keyringFailed);
            done({std::nullopt, d});
        };
        if (signIn->passwords > 1) {
            ask();
            return;
        }
        Secrets::instance()->read(key, [signIn, done, ask](const QString &secret, bool found) {
            signIn->saved = found;
            if (found) {
                signIn->fromKeyring = true;
                done({secret, {}});
            } else {
                ask();
            }
        });
    };
    options.answered = [this, guard = QPointer(this), signIn](const QString &prompt, const QString &text, bool save) {
        // Only a password is ever kept, never a code or a passphrase, and
        // only once the server has taken it.
        if (!guard || !AskPass::isPassword(prompt) || (!save && !signIn->saved)) {
            return;
        }
        confirmSignIn(signIn, ++signIn->watch, text, save, 0);
    };
    return AskPass::instance()->newToken(options);
}

void SessionTab::confirmSignIn(const std::shared_ptr<SignIn> &signIn, int watch, const QString &password, bool save, int tries)
{
    // ssh opens its shared connection only once the server has signed it
    // in, so the connection being there means the password worked.
    if (signIn->watch != watch || tries > 240) {
        return;
    }
    QStringList args = sshArgs(false);
    const qsizetype end = args.lastIndexOf(QStringLiteral("--"));
    args.insert(end >= 0 ? end : args.size() - 1, QStringLiteral("check"));
    args.insert(end >= 0 ? end : args.size() - 1, QStringLiteral("-O"));
    args.prepend(QStringLiteral("BatchMode=yes"));
    args.prepend(QStringLiteral("-o"));
    auto *check = new QProcess(this);
    check->setProcessChannelMode(QProcess::MergedChannels);
    connect(check, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus status) {
        check->deleteLater();
        if (signIn->watch != watch) {
            return;
        }
        if (status == QProcess::NormalExit && code == 0) {
            ++signIn->watch;
            if (save) {
                Secrets::instance()->write(m_passwordKey, password);
                signIn->saved = true;
            } else {
                // Typed again without saving: the saved one was wrong.
                Secrets::instance()->remove(m_passwordKey);
                signIn->saved = false;
            }
            return;
        }
        QTimer::singleShot(500, this, [=, this] {
            confirmSignIn(signIn, watch, password, save, tries + 1);
        });
    });
    connect(check, &QProcess::errorOccurred, check, [check](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            check->deleteLater();
        }
    });
    check->start(QStringLiteral("ssh"), args);
}

SessionWindow *SessionWindow::instance(bool create)
{
    static QPointer<SessionWindow> window;
    if (!window && create) {
        window = new SessionWindow;
    }
    return window;
}

SessionWindow::SessionWindow()
    : QWidget(nullptr)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(tr("Remote"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("org.wildos.remote")));
    setMinimumSize(640, 360);

    m_stack = new QStackedWidget(this);
    m_stack->setAttribute(Qt::WA_TranslucentBackground);
    m_stack->setAutoFillBackground(false);

    m_chrome = new QQuickWidget(this);
    m_chrome->setAttribute(Qt::WA_AlwaysStackOnTop);
    m_chrome->setAttribute(Qt::WA_TranslucentBackground);
    m_chrome->setClearColor(Qt::transparent);
    m_chrome->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_chrome->setFocusPolicy(Qt::ClickFocus);
    m_chrome->rootContext()->setContextProperty(QStringLiteral("host"), this);
    m_chrome->setSource(QUrl(QStringLiteral("qrc:/qt/qml/WildOS/Remote/SessionChrome.qml")));
    if (m_chrome->status() == QQuickWidget::Error) {
        for (const auto &e : m_chrome->errors()) {
            qWarning().noquote() << e.toString();
        }
    }
    m_chrome->raise();

    connect(RemoteSettings::instance(), &RemoteSettings::terminalChanged, this, qOverload<>(&QWidget::update));

    auto add = [this](const QString &key, auto slot) {
        auto *s = new QShortcut(QKeySequence(key), this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, slot);
    };
    add(QStringLiteral("Ctrl+Shift+T"), [this] { openLocal(); });
    add(QStringLiteral("Ctrl+Shift+W"), [this] { closeTab(m_current); });
    add(QStringLiteral("Ctrl+,"), [this] { showSettings(); });
    add(QStringLiteral("Ctrl+Tab"), [this] { setCurrentIndex((m_current + 1) % std::max<qsizetype>(1, m_tabs.size())); });
    add(QStringLiteral("Ctrl+Shift+Tab"), [this] { setCurrentIndex((m_current + m_tabs.size() - 1) % std::max<qsizetype>(1, m_tabs.size())); });

    resize(1384, 802);
}

SessionWindow::~SessionWindow()
{
    for (SessionTab *t : std::as_const(m_tabs)) {
        disconnect(t, nullptr, this, nullptr);
    }
    delete m_chrome;
    m_chrome = nullptr;
    qDeleteAll(m_tabs);
}

void SessionWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_frame && windowHandle()) {
        m_frame = new WindowFrame(windowHandle(), this);
        updateBlur();
    }
}

QList<QObject *> SessionWindow::tabs() const
{
    QList<QObject *> list;
    for (SessionTab *t : m_tabs) {
        list.append(t);
    }
    return list;
}

SessionTab *SessionWindow::currentTab() const
{
    return m_current >= 0 && m_current < m_tabs.size() ? m_tabs[m_current] : nullptr;
}

int SessionWindow::terminalTabCount() const
{
    int n = 0;
    for (SessionTab *t : m_tabs) {
        n += t->ended() ? 0 : 1;
    }
    return n;
}

void SessionWindow::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_tabs.size()) {
        return;
    }
    m_current = index;
    m_stack->setCurrentWidget(m_tabs[index]->page());
    setWindowTitle(tr("%1, Remote").arg(m_tabs[index]->title()));
    Q_EMIT currentChanged();
    Q_EMIT layoutChanged();
    updateMask();
    m_tabs[index]->focusTerminal();
}

void SessionWindow::open(const QVariantMap &host)
{
    auto *tab = new SessionTab(host, this);
    m_stack->addWidget(tab->page());
    connect(tab, &SessionTab::changed, this, [this, tab] {
        if (tab == currentTab()) {
            updateMask();
            Q_EMIT layoutChanged();
        }
    });
    m_tabs.append(tab);
    Q_EMIT tabsChanged();
    setCurrentIndex(int(m_tabs.size()) - 1);
    layoutChildren();
    if (!isVisible()) {
        show();
    }
    raise();
    activateWindow();
}

void SessionWindow::openHost(const QString &hostId)
{
    const QVariantMap h = Hosts::instance()->host(hostId);
    if (!h.isEmpty()) {
        Q_EMIT homeRequested(hostId);
    }
}

void SessionWindow::openLocal()
{
    open({});
}

void SessionWindow::closeTab(int index)
{
    if (index < 0 || index >= m_tabs.size()) {
        return;
    }
    SessionTab *tab = m_tabs.takeAt(index);
    m_stack->removeWidget(tab->page());
    tab->deleteLater();
    Q_EMIT tabsChanged();
    if (m_tabs.isEmpty()) {
        m_current = -1;
        close();
        return;
    }
    setCurrentIndex(std::min(index, int(m_tabs.size()) - 1));
}

void SessionWindow::runEverywhere(const QString &command)
{
    for (SessionTab *t : std::as_const(m_tabs)) {
        if (!t->ended()) {
            t->run(command);
        }
    }
}

void SessionWindow::focusChrome()
{
    raise();
    activateWindow();
    if (m_chrome) {
        m_chrome->setFocus();
    }
}

void SessionWindow::showHome()
{
    Q_EMIT homeRequested(QString());
}

void SessionWindow::showSettings()
{
    Q_EMIT homeRequested(QStringLiteral("settings"));
}

void SessionWindow::editHost(const QString &hostId)
{
    Q_EMIT homeRequested(QStringLiteral("edit:") + hostId);
}

bool SessionWindow::maximised() const
{
    return windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
}

bool SessionWindow::blurAvailable() const
{
    return m_frame && m_frame->blurAvailable();
}

void SessionWindow::setSnippetsOpen(bool open)
{
    if (open != m_snippetsOpen) {
        m_snippetsOpen = open;
        layoutChildren();
        Q_EMIT layoutChanged();
    }
}

void SessionWindow::setChromeFull(bool full)
{
    if (full != m_chromeFull) {
        m_chromeFull = full;
        updateMask();
        Q_EMIT layoutChanged();
        if (full) {
            m_chrome->setFocus();
        } else if (SessionTab *t = currentTab()) {
            t->focusTerminal();
        }
    }
}

void SessionWindow::setOverlay(const QRect &rect)
{
    if (rect != m_overlay) {
        m_overlay = rect;
        updateMask();
    }
}

QRect SessionWindow::terminalArea() const
{
    const int side = maximised() ? 0 : inset;
    const int right = m_snippetsOpen ? snippetsWidth : side;
    return QRect(side, titleHeight, width() - side - right, height() - titleHeight - statusHeight);
}

void SessionWindow::minimise()
{
    showMinimized();
}

void SessionWindow::toggleMaximised()
{
    maximised() ? showNormal() : showMaximized();
}

void SessionWindow::startMove()
{
    if (windowHandle()) {
        windowHandle()->startSystemMove();
    }
}

void SessionWindow::startResize(int edges)
{
    if (windowHandle()) {
        windowHandle()->startSystemResize(Qt::Edges(edges));
    }
}

void SessionWindow::closeWindow()
{
    close();
}

void SessionWindow::updateMask()
{
    if (!m_chrome) {
        return;
    }
    SessionTab *tab = currentTab();
    // Files are drawn by the layer, over where the terminal would be.
    if (m_chromeFull || (tab && (tab->filesOpen() || tab->ended()))) {
        m_chrome->clearMask();
        return;
    }
    QRegion region(rect());
    region -= terminalArea();
    if (m_overlay.isValid()) {
        region += m_overlay;
    }
    m_chrome->setMask(region);
}

void SessionWindow::updateBlur()
{
    if (!m_frame) {
        return;
    }
    const int r = maximised() ? 0 : radius;
    m_frame->setRadius(r);
    m_frame->setShadow(!maximised());
    m_frame->setBlurRegion(WindowFrame::roundedRegion(size(), r));
}

void SessionWindow::layoutChildren()
{
    m_chrome->setGeometry(rect());
    m_stack->setGeometry(terminalArea());
    // The terminal works out its columns and rows once it has its new size.
    if (SessionTab *t = currentTab()) {
        QTimer::singleShot(60, t, &SessionTab::changed);
    }
    updateMask();
    updateBlur();
    Q_EMIT layoutChanged();
}

void SessionWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void SessionWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (m_frame && isActiveWindow()) {
            m_frame->recheck();
        }
        Q_EMIT stateChanged();
    } else if (event->type() == QEvent::WindowStateChange) {
        layoutChildren();
        update();
        Q_EMIT stateChanged();
    }
}

void SessionWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    // The terminal's own colour (its scheme's background) under the gap
    // around it, so the terminal and the window edge read as one surface.
    p.setRenderHint(QPainter::Antialiasing);
    const qreal r = maximised() ? 0 : radius;
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()), r, r);
    QPainterPath area;
    const QRect t = terminalArea();
    area.addRect(QRectF(0, t.top(), t.right() + 1, t.height()));
    p.fillPath(path.intersected(area), RemoteSettings::instance()->background());
}
