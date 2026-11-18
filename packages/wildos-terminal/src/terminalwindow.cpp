#include "terminalwindow.h"
#include "config.h"
#include "tab.h"
#include "terminalview.h"
#include "windowframe.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QScreen>
#include <QShortcut>
#include <QStackedWidget>
#include <QWindow>

namespace
{
// Space between the terminal and the window edge. The bottom corners are
// rounded, and this keeps the terminal's square corners inside them.
constexpr int inset = 5;
constexpr int grip = 5;
// The emulator's own padding (setMargin) and its scroll bar.
constexpr int textMargin = 6;
constexpr int scrollBarWidth = 12;
}

TerminalWindow::TerminalWindow(const QString &directory, const QStringList &command, QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));
    setMinimumSize(480, 280);

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
    m_chrome->setSource(QUrl(QStringLiteral("qrc:/qt/qml/WildOS/Terminal/qml/Chrome.qml")));
    if (m_chrome->status() == QQuickWidget::Error) {
        for (const auto &e : m_chrome->errors()) {
            qWarning().noquote() << e.toString();
        }
    }
    m_chrome->raise();

    Config *config = Config::instance();
    connect(config, &Config::changed, this, &TerminalWindow::applyConfig);
    connect(config, &Config::accentChanged, this, [this] {
        for (Tab *tab : std::as_const(m_tabs)) {
            for (TerminalView *v : tab->views()) {
                v->update();
            }
        }
    });

    setupShortcuts();

    Tab *tab = new Tab(false, new QWidget, this);
    addTab(tab, 0);
    TerminalView *view = createView(config->defaultProfile(), directory, command);
    tab->addFirst(view);
    setCurrentIndex(0);

    resize(launchSize());
}

TerminalWindow::~TerminalWindow()
{
    // Shells end as their widgets go; nothing should react to that now.
    for (Tab *tab : std::as_const(m_tabs)) {
        disconnect(tab, nullptr, this, nullptr);
        for (TerminalView *v : tab->views()) {
            disconnect(v, nullptr, this, nullptr);
        }
    }
    // The QML layer reads this window; it goes first so it never sees the
    // window half destroyed.
    delete m_chrome;
    m_chrome = nullptr;
}

QSize TerminalWindow::launchSize()
{
    Config *config = Config::instance();
    const QFontMetrics fm(config->terminalFont());
    const int cell = qRound(fm.horizontalAdvance(QStringLiteral("MMMMMMMMMM")) / 10.0);
    const int width = config->launchColumns() * cell + 2 * textMargin + scrollBarWidth + 2 * inset + 2;
    const int height = config->launchRows() * fm.height() + 2 * textMargin + titleHeight + inset + 2;
    QSize size(width, height);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        size = size.boundedTo(screen->availableSize() - QSize(40, 40));
    }
    return size;
}

void TerminalWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_frame && windowHandle()) {
        m_frame = new WindowFrame(windowHandle(), this);
        updateBlur();
    }
}

QList<QObject *> TerminalWindow::tabs() const
{
    QList<QObject *> list;
    for (Tab *t : m_tabs) {
        list.append(t);
    }
    return list;
}

Tab *TerminalWindow::tabAt(int index) const
{
    return index >= 0 && index < m_tabs.size() ? m_tabs[index] : nullptr;
}

QObject *TerminalWindow::currentTab() const
{
    return tabAt(m_current);
}

TerminalView *TerminalWindow::currentView() const
{
    Tab *tab = tabAt(m_current);
    return tab && !tab->isSettings() ? tab->current() : nullptr;
}

int TerminalWindow::terminalTabCount() const
{
    int count = 0;
    for (Tab *t : m_tabs) {
        count += t->isSettings() ? 0 : 1;
    }
    return count;
}

bool TerminalWindow::maximised() const
{
    return windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
}

bool TerminalWindow::fullScreen() const
{
    return windowState() & Qt::WindowFullScreen;
}

bool TerminalWindow::blurAvailable() const
{
    return m_frame && m_frame->blurAvailable();
}

void TerminalWindow::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_tabs.size()) {
        return;
    }
    const bool changed = index != m_current;
    m_current = index;
    m_stack->setCurrentWidget(m_tabs[index]->page());
    if (changed) {
        Q_EMIT currentChanged();
    }
    updateTitle();
    updateBlur();
    update();
    focusTerminal();
}

TerminalView *TerminalWindow::createView(const QString &profileId, const QString &directory, const QStringList &command)
{
    return new TerminalView(Config::instance()->profile(profileId), directory, command);
}

void TerminalWindow::watchView(Tab *tab, TerminalView *view)
{
    connect(view, &TerminalView::closed, this, [this, tab, view] {
        closeView(tab, view);
    });
    connect(view, &TerminalView::windowKey, this, [this](const QString &action) {
        if (action == QLatin1String("split-right")) {
            split(true);
        } else if (action == QLatin1String("split-down")) {
            split(false);
        } else if (action == QLatin1String("pane-left")) {
            focusNeighbour(Qt::LeftEdge);
        } else if (action == QLatin1String("pane-right")) {
            focusNeighbour(Qt::RightEdge);
        } else if (action == QLatin1String("pane-up")) {
            focusNeighbour(Qt::TopEdge);
        } else if (action == QLatin1String("pane-down")) {
            focusNeighbour(Qt::BottomEdge);
        } else if (action.startsWith(QLatin1String("tab-"))) {
            // 9 is the last tab, whatever the count, as in browsers.
            const int n = action.mid(4).toInt();
            setCurrentIndex(n == 9 ? int(m_tabs.size()) - 1 : n - 1);
        }
    });
    connect(view, &TerminalView::contextMenuWanted, this, [this, view](const QPoint &pos) {
        m_menuView = view;
        m_menuLink = view->linkAt(pos);
        const QPoint p = view->mapTo(this, pos);
        Tab *owner = tabAt(m_current);
        Q_EMIT contextMenuRequested(p.x(), p.y(), view->hasSelection(), m_menuLink.isValid(), owner && owner->paneCount() > 1);
    });
    connect(view, &TerminalView::pasteNeedsConfirming, this, [this, view](const QString &text) {
        m_pasteView = view;
        m_pasteText = text;
        QString body = text.trimmed();
        Q_EMIT pasteConfirmationNeeded(body.count(QLatin1Char('\n')) + 1);
    });
}

void TerminalWindow::addTab(Tab *tab, int at)
{
    at = std::clamp(at, 0, int(m_tabs.size()));
    m_tabs.insert(at, tab);
    m_stack->addWidget(tab->page());
    connect(tab, &Tab::viewAdded, this, [this, tab](TerminalView *view) {
        watchView(tab, view);
    });
    connect(tab, &Tab::changed, this, &TerminalWindow::updateTitle);
    if (m_current >= at && m_current >= 0) {
        ++m_current;
    }
    Q_EMIT tabsChanged();
}

void TerminalWindow::removeTab(Tab *tab)
{
    const int index = m_tabs.indexOf(tab);
    if (index < 0) {
        return;
    }
    m_tabs.removeAt(index);
    m_stack->removeWidget(tab->page());
    tab->page()->deleteLater();
    tab->deleteLater();
    if (m_tabs.isEmpty()) {
        m_current = -1;
        Q_EMIT tabsChanged();
        m_closing = true;
        close();
        return;
    }
    // Closing the tab in front shows the one to its right, or the new last
    // one, as browsers and Windows Terminal do.
    int next = m_current;
    if (index < m_current) {
        next = m_current - 1;
    } else if (index == m_current) {
        next = std::min(index, int(m_tabs.size()) - 1);
    }
    m_current = -1;
    Q_EMIT tabsChanged();
    setCurrentIndex(next);
}

void TerminalWindow::closeView(Tab *tab, TerminalView *view)
{
    if (!m_tabs.contains(tab)) {
        return;
    }
    if (tab->remove(view)) {
        removeTab(tab);
    }
}

void TerminalWindow::newTab(const QString &profileId)
{
    Tab *tab = new Tab(false, new QWidget, this);
    addTab(tab, m_current + 1);
    tab->addFirst(createView(profileId.isEmpty() ? Config::instance()->defaultProfile() : profileId, QString(), {}));
    setCurrentIndex(m_tabs.indexOf(tab));
}

void TerminalWindow::newTabAt(int profileIndex)
{
    const QVariantList profiles = Config::instance()->profiles();
    if (profileIndex >= 0 && profileIndex < profiles.size()) {
        newTab(profiles[profileIndex].toMap().value(QStringLiteral("id")).toString());
    }
}

void TerminalWindow::duplicateTab(int index)
{
    Tab *from = tabAt(index);
    if (!from || from->isSettings() || !from->current()) {
        return;
    }
    TerminalView *source = from->current();
    Tab *tab = new Tab(false, new QWidget, this);
    addTab(tab, index + 1);
    tab->addFirst(createView(source->profileId(), source->currentDirectory(), {}));
    setCurrentIndex(m_tabs.indexOf(tab));
}

void TerminalWindow::closeTab(int index)
{
    if (Tab *tab = tabAt(index)) {
        removeTab(tab);
    }
}

void TerminalWindow::closeOtherTabs(int index)
{
    Tab *keep = tabAt(index);
    if (!keep) {
        return;
    }
    const QList<Tab *> all = m_tabs;
    for (Tab *t : all) {
        if (t != keep) {
            removeTab(t);
        }
    }
}

void TerminalWindow::closeTabsToRight(int index)
{
    const QList<Tab *> all = m_tabs.mid(index + 1);
    for (Tab *t : all) {
        removeTab(t);
    }
}

void TerminalWindow::moveTab(int from, int to)
{
    if (from < 0 || from >= m_tabs.size() || to < 0 || to >= m_tabs.size() || from == to) {
        return;
    }
    Tab *current = tabAt(m_current);
    m_tabs.move(from, to);
    m_current = m_tabs.indexOf(current);
    Q_EMIT tabsChanged();
    Q_EMIT currentChanged();
}

void TerminalWindow::nextTab(int step)
{
    if (m_tabs.size() > 1) {
        setCurrentIndex((m_current + step + m_tabs.size()) % m_tabs.size());
    }
}

void TerminalWindow::openSettings()
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->isSettings()) {
            setCurrentIndex(i);
            return;
        }
    }
    Tab *tab = new Tab(true, new QWidget, this);
    addTab(tab, m_current + 1);
    setCurrentIndex(m_tabs.indexOf(tab));
}

void TerminalWindow::split(bool right)
{
    Tab *tab = tabAt(m_current);
    TerminalView *from = currentView();
    if (!tab || !from) {
        return;
    }
    tab->split(createView(from->profileId(), from->currentDirectory(), {}), right ? Qt::Horizontal : Qt::Vertical);
}

void TerminalWindow::closePane()
{
    Tab *tab = tabAt(m_current);
    if (!tab) {
        return;
    }
    if (tab->isSettings() || tab->paneCount() <= 1) {
        removeTab(tab);
    } else {
        closeView(tab, tab->current());
    }
}

void TerminalWindow::focusNeighbour(Qt::Edge edge)
{
    Tab *tab = tabAt(m_current);
    if (tab && !tab->isSettings()) {
        if (TerminalView *v = tab->neighbour(edge)) {
            v->setFocus();
            tab->setCurrent(v);
        }
    }
}

void TerminalWindow::copy()
{
    TerminalView *v = m_menuView ? m_menuView.data() : currentView();
    m_menuView = nullptr;
    if (v) {
        v->copy();
    }
}

void TerminalWindow::paste()
{
    TerminalView *v = m_menuView ? m_menuView.data() : currentView();
    m_menuView = nullptr;
    if (v) {
        v->paste();
    }
}

void TerminalWindow::openLink()
{
    if (m_menuLink.isValid()) {
        QDesktopServices::openUrl(m_menuLink);
    }
    m_menuView = nullptr;
}

void TerminalWindow::copyLink()
{
    if (m_menuLink.isValid()) {
        // As written in the terminal: the default form decodes parts of an
        // address, and a sign-in link has to arrive as it was sent.
        QApplication::clipboard()->setText(m_menuLink.toString(QUrl::FullyEncoded));
    }
    m_menuView = nullptr;
}

void TerminalWindow::answerPaste(bool paste)
{
    if (paste && m_pasteView) {
        m_pasteView->sendPaste(m_pasteText);
    }
    m_pasteText.clear();
    m_pasteView = nullptr;
    focusTerminal();
}

void TerminalWindow::zoom(int step)
{
    if (TerminalView *v = currentView()) {
        if (step == 0) {
            v->resetZoom();
        } else {
            v->zoomBy(step);
        }
    }
}

void TerminalWindow::requestClose()
{
    close();
}

void TerminalWindow::closeNow()
{
    m_closing = true;
    close();
}

void TerminalWindow::closeEvent(QCloseEvent *event)
{
    if (!m_closing && Config::instance()->confirmCloseTabs() && terminalTabCount() > 1) {
        event->ignore();
        Q_EMIT closeConfirmationNeeded(terminalTabCount());
        return;
    }
    QWidget::closeEvent(event);
}

void TerminalWindow::minimise()
{
    showMinimized();
}

void TerminalWindow::toggleMaximised()
{
    if (windowState() & Qt::WindowFullScreen) {
        showNormal();
    } else if (windowState() & Qt::WindowMaximized) {
        showNormal();
    } else {
        showMaximized();
    }
}

void TerminalWindow::toggleFullScreen()
{
    if (windowState() & Qt::WindowFullScreen) {
        showNormal();
    } else {
        showFullScreen();
    }
}

void TerminalWindow::startMove()
{
    if (windowHandle()) {
        windowHandle()->startSystemMove();
    }
}

void TerminalWindow::startResize(int edges)
{
    if (windowHandle()) {
        windowHandle()->startSystemResize(Qt::Edges(edges));
    }
}

void TerminalWindow::focusTerminal()
{
    if (m_chromeFull) {
        Tab *tab = tabAt(m_current);
        if (tab && tab->isSettings()) {
            m_chrome->setFocus();
        }
        return;
    }
    if (TerminalView *v = currentView()) {
        v->setFocus();
    }
}

void TerminalWindow::newWindow()
{
    TerminalView *v = currentView();
    auto *w = new TerminalWindow(v ? v->currentDirectory() : QString(), {});
    if (Config::instance()->launchMaximised()) {
        w->showMaximized();
    } else {
        w->show();
    }
}

void TerminalWindow::setChromeFull(bool full)
{
    if (full == m_chromeFull) {
        return;
    }
    m_chromeFull = full;
    updateMask();
    Q_EMIT chromeFullChanged();
    if (full) {
        m_chrome->setFocus();
    } else {
        focusTerminal();
    }
}

void TerminalWindow::updateMask()
{
    const int w = width();
    const int h = height();
    if (m_chromeFull) {
        m_chrome->clearMask();
        return;
    }
    QRegion region(0, 0, w, titleHeight);
    if (!maximised()) {
        region += QRect(0, 0, grip, h);
        region += QRect(w - grip, 0, grip, h);
        region += QRect(0, h - grip, w, grip);
    }
    m_chrome->setMask(region);
}

void TerminalWindow::updateBlur()
{
    if (!m_frame) {
        return;
    }
    const int r = maximised() ? 0 : radius;
    m_frame->setRadius(r);
    m_frame->setShadow(!maximised());
    const QRegion whole = WindowFrame::roundedRegion(size(), r);
    Tab *tab = tabAt(m_current);
    Config *config = Config::instance();
    const bool glassContent = (tab && tab->isSettings()) || (config->opacity() < 100 && config->blur());
    if (glassContent) {
        m_frame->setBlurRegion(whole);
    } else {
        m_frame->setBlurRegion(whole & QRegion(0, 0, width(), titleHeight));
    }
}

void TerminalWindow::layoutChildren()
{
    m_chrome->setGeometry(rect());
    const int side = maximised() ? 0 : inset;
    m_stack->setGeometry(side, titleHeight, width() - 2 * side, height() - titleHeight - side);
    updateMask();
    updateBlur();
}

void TerminalWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutChildren();
}

void TerminalWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ActivationChange) {
        if (m_frame && isActiveWindow() && m_frame->recheck()) {
            Q_EMIT blurAvailableChanged();
            updateBlur();
        }
        Q_EMIT stateChanged();
    } else if (event->type() == QEvent::WindowStateChange) {
        layoutChildren();
        update();
        Q_EMIT stateChanged();
    }
}

void TerminalWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    Tab *tab = tabAt(m_current);
    if (tab && tab->isSettings()) {
        return;
    }
    // The terminal's own background, under the gap around it, so the
    // terminal and the edge read as one surface with rounded corners.
    Config *config = Config::instance();
    QColor bg = config->schemeBackground();
    bg.setAlphaF(config->opacity() / 100.0);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal r = maximised() ? 0 : radius;
    const QRectF area(0, titleHeight, width(), height() - titleHeight);
    QPainterPath path;
    path.addRoundedRect(QRectF(0, titleHeight - r, width(), height() - titleHeight + r), r, r);
    QPainterPath clip;
    clip.addRect(area);
    path = path.intersected(clip);
    p.fillPath(path, bg);
}

void TerminalWindow::updateTitle()
{
    Tab *tab = tabAt(m_current);
    setWindowTitle(tab ? tab->title() : tr("Terminal"));
}

void TerminalWindow::applyConfig()
{
    for (Tab *tab : std::as_const(m_tabs)) {
        for (TerminalView *v : tab->views()) {
            v->applyConfig();
        }
    }
    updateBlur();
    update();
}

void TerminalWindow::setupShortcuts()
{
    auto add = [this](const QList<QKeySequence> &keys, auto slot) {
        for (const QKeySequence &key : keys) {
            auto *s = new QShortcut(key, this);
            s->setContext(Qt::WindowShortcut);
            s->setAutoRepeat(false);
            connect(s, &QShortcut::activated, this, slot);
        }
    };
    add({QKeySequence(QStringLiteral("Ctrl+Shift+T"))}, [this] { newTab(); });
    add({QKeySequence(QStringLiteral("Ctrl+Shift+W"))}, [this] { closePane(); });
    add({QKeySequence(QStringLiteral("Ctrl+Shift+N"))}, [this] { newWindow(); });
    add({QKeySequence(QStringLiteral("Ctrl+Shift+D"))}, [this] { duplicateTab(m_current); });
    add({QKeySequence(QStringLiteral("Ctrl+Tab"))}, [this] { nextTab(1); });
    add({QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), QKeySequence(QStringLiteral("Ctrl+Shift+Backtab"))}, [this] { nextTab(-1); });
    add({QKeySequence(QStringLiteral("Ctrl+Shift+C"))}, [this] {
        m_menuView = nullptr;
        copy();
    });
    add({QKeySequence(QStringLiteral("Ctrl+Shift+V"))}, [this] {
        m_menuView = nullptr;
        paste();
    });
    add({QKeySequence(QStringLiteral("Ctrl+=")), QKeySequence(QStringLiteral("Ctrl++")), QKeySequence(QStringLiteral("Ctrl+Shift+="))}, [this] { zoom(1); });
    add({QKeySequence(QStringLiteral("Ctrl+-"))}, [this] { zoom(-1); });
    add({QKeySequence(QStringLiteral("Ctrl+0"))}, [this] { zoom(0); });
    add({QKeySequence(QStringLiteral("Ctrl+,"))}, [this] { openSettings(); });
    add({QKeySequence(QStringLiteral("Ctrl+Shift+Space"))}, [this] { Q_EMIT profileMenuRequested(); });
    add({QKeySequence(QStringLiteral("F11"))}, [this] { toggleFullScreen(); });
    for (int i = 1; i <= 9; ++i) {
        add({QKeySequence(QStringLiteral("Ctrl+Shift+%1").arg(i))}, [this, i] { newTabAt(i - 1); });
    }
}
