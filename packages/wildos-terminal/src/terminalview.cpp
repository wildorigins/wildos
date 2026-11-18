#include "terminalview.h"
#include "config.h"
#include "schemes.h"
#include "termlinks.h"

#include <QApplication>
#include <QClipboard>
#include <QHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScrollBar>
#include <QTimer>

namespace
{
// The design's ThinScrollBar, in widget form: no track, a slim pale thumb.
const QString scrollBarStyle = QStringLiteral(
    "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px 2px 2px 2px; border: none; }"
    "QScrollBar::handle:vertical { background: rgba(255,255,255,0.28); border-radius: 3px; min-height: 32px; margin: 0px 2px 0px 2px; }"
    "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.40); margin: 0px 1px 0px 1px; }"
    "QScrollBar::handle:vertical:pressed { background: rgba(255,255,255,0.48); margin: 0px 1px 0px 1px; }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; border: none; background: none; }"
    "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");

QString expandHome(const QString &path)
{
    if (path == QLatin1String("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

// Thin accent outline around the pane that has focus when a tab is split,
// as Windows Terminal marks it. It never takes the pointer.
class Outline : public QWidget
{
public:
    explicit Outline(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QColor c = Config::instance()->accent();
        c.setAlphaF(0.85);
        p.setPen(QPen(c, 1));
        p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }
};
}

TerminalView::TerminalView(const QVariantMap &profile, const QString &directory, const QStringList &command, QWidget *parent)
    : QTermWidget(0, parent)
    , m_profileId(profile.value(QStringLiteral("id")).toString())
    , m_profileName(profile.value(QStringLiteral("name")).toString())
    , m_icon(profile.value(QStringLiteral("icon"), QStringLiteral("terminal")).toString())
{
    Config *config = Config::instance();

    QStringList program = command;
    if (program.isEmpty()) {
        program = QProcess::splitCommand(profile.value(QStringLiteral("command")).toString());
    }
    if (program.isEmpty()) {
        program = {config->defaultShell()};
    }
    setShellProgram(program.first());
    setArgs(program.mid(1));

    QString dir = directory;
    if (dir.isEmpty()) {
        dir = expandHome(profile.value(QStringLiteral("directory")).toString());
    }
    if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
        dir = QDir::homePath();
    }
    setWorkingDirectory(dir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    env.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    env.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("wildos-terminal"));
    env.remove(QStringLiteral("DESKTOP_STARTUP_ID"));
    env.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    setEnvironment(env.toStringList());

    // The stock pieces of the widget that would look like another app:
    // the size label while resizing and the flow control warning box.
    // Ctrl+S freezing output surprises people coming from Windows, so XON
    // and XOFF are off as there.
    setTerminalSizeHint(false);
    setFlowControlEnabled(false);
    setFlowControlWarningEnabled(false);
    setConfirmMultilinePaste(false);
    setMotionAfterPasting(2);
    setScrollBarPosition(QTermWidget::ScrollBarRight);
    setMargin(6);
    setAutoClose(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    if (QScrollBar *bar = findChild<QScrollBar *>()) {
        bar->setAttribute(Qt::WA_TranslucentBackground);
        bar->setAutoFillBackground(false);
        // Nothing to scroll back to yet: the bar keeps its room, so the
        // columns do not change, but shows no thumb.
        auto style = [bar](int max) {
            bar->setStyleSheet(max > 0 ? scrollBarStyle : QString(scrollBarStyle).replace(QStringLiteral("rgba(255,255,255,0.28)"), QStringLiteral("transparent")));
        };
        style(bar->maximum());
        connect(bar, &QScrollBar::rangeChanged, bar, [style](int, int max) {
            style(max);
        });
    }

    applyConfig();

    m_display = display();
    if (m_display) {
        m_display->installEventFilter(this);
        m_links = new TermLinks(this, m_display);
        m_links->setColour(Schemes::find(config->scheme()).foreground);
        connect(m_links, &TermLinks::activated, this, [](const QUrl &url) {
            QDesktopServices::openUrl(url);
        });
    }

    m_outline = new Outline(this);
    m_outline->hide();

    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (Config::instance()->pasteOnRightClick() && !(QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
            paste();
        } else {
            Q_EMIT contextMenuWanted(pos);
        }
    });
    connect(this, &QTermWidget::termGetFocus, this, &TerminalView::focused);
    connect(this, &QTermWidget::titleChanged, this, &TerminalView::titleUpdated);
    connect(this, &QTermWidget::finished, this, &TerminalView::closed);
    connect(this, &QTermWidget::copyAvailable, this, [this](bool) {
        m_copiedByCtrlC = false;
    });

    startShellProgram();
}

QWidget *TerminalView::display() const
{
    for (QWidget *w : findChildren<QWidget *>()) {
        if (QByteArray(w->metaObject()->className()).contains("TerminalDisplay")) {
            return w;
        }
    }
    return nullptr;
}

void TerminalView::applyConfig()
{
    Config *config = Config::instance();
    setColorScheme(config->schemePath());
    setTerminalOpacity(config->opacity() / 100.0);
    QFont font = config->terminalFont();
    if (m_zoom != 0) {
        font.setPointSize(std::max(4, font.pointSize() + m_zoom));
    }
    setTerminalFont(font);
    const QString shape = config->cursorShape();
    setKeyboardCursorShape(shape == QLatin1String("block") ? KeyboardCursorShape::BlockCursor
                           : shape == QLatin1String("underline") ? KeyboardCursorShape::UnderlineCursor
                                                                  : KeyboardCursorShape::IBeamCursor);
    setBlinkingCursor(config->cursorBlink());
    setBoldIntense(config->boldBright());
    setHistorySize(config->scrollback());
    if (m_links) {
        m_links->setColour(Schemes::find(config->scheme()).foreground);
    }
}

QString TerminalView::displayTitle() const
{
    const QString t = title().trimmed();
    return t.isEmpty() ? m_profileName : t;
}

QString TerminalView::currentDirectory()
{
    const QString dir = workingDirectory();
    return QFileInfo(dir).isDir() ? dir : QString();
}

bool TerminalView::hasSelection()
{
    return !selectedText(true).isEmpty();
}

void TerminalView::copy()
{
    const QString text = selectedText(true);
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text, QClipboard::Clipboard);
    }
}

void TerminalView::paste()
{
    QString text = QApplication::clipboard()->text(QClipboard::Clipboard);
    if (text.isEmpty()) {
        return;
    }
    QString body = text;
    while (body.endsWith(QLatin1Char('\n')) || body.endsWith(QLatin1Char('\r'))) {
        body.chop(1);
    }
    if (Config::instance()->warnMultilinePaste() && (body.contains(QLatin1Char('\n')) || body.contains(QLatin1Char('\r')))) {
        Q_EMIT pasteNeedsConfirming(text);
        return;
    }
    sendPaste(text);
}

void TerminalView::sendPaste(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
    bracketText(text);
    sendText(text);
    setFocus();
}

void TerminalView::zoomBy(int step)
{
    m_zoom = std::clamp(m_zoom + step, -6, 30);
    applyConfig();
}

void TerminalView::resetZoom()
{
    m_zoom = 0;
    applyConfig();
}

void TerminalView::setOutlined(bool on)
{
    m_outline->setGeometry(rect());
    m_outline->setVisible(on);
    m_outline->raise();
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QTermWidget::resizeEvent(event);
    if (m_outline) {
        m_outline->setGeometry(rect());
    }
}

QUrl TerminalView::linkAt(const QPoint &pos) const
{
    return m_links ? m_links->linkAt(pos) : QUrl();
}

bool TerminalView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_display) {
        return QTermWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ShortcutOverride) {
        // The emulator claims Alt with a letter, the arrows, Tab and a few
        // others before any shortcut sees them. Terminal's own shortcuts
        // (Alt+Shift+= to split, Alt+arrows between panes) must win; any
        // key that is not one of them still reaches the shell as typed.
        event->ignore();
        return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
        // Shortcuts on Alt are matched here rather than with QShortcut: the
        // emulator's key handling gets them first and X keyboard layouts
        // report the shifted symbol (Alt+Shift+= arrives as Alt+Shift++).
        if (mods == (Qt::AltModifier | Qt::ShiftModifier)) {
            if (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Equal) {
                Q_EMIT windowKey(QStringLiteral("split-right"));
                return true;
            }
            if (key->key() == Qt::Key_Minus || key->key() == Qt::Key_Underscore) {
                Q_EMIT windowKey(QStringLiteral("split-down"));
                return true;
            }
        }
        if (mods == Qt::AltModifier) {
            const QHash<int, QString> arrows = {
                {Qt::Key_Left, QStringLiteral("pane-left")}, {Qt::Key_Right, QStringLiteral("pane-right")},
                {Qt::Key_Up, QStringLiteral("pane-up")}, {Qt::Key_Down, QStringLiteral("pane-down")},
            };
            if (arrows.contains(key->key())) {
                Q_EMIT windowKey(arrows.value(key->key()));
                return true;
            }
        }
        if (mods == (Qt::ControlModifier | Qt::AltModifier) && key->key() >= Qt::Key_1 && key->key() <= Qt::Key_9) {
            Q_EMIT windowKey(QStringLiteral("tab-%1").arg(key->key() - Qt::Key_0));
            return true;
        }
        if (mods == Qt::ControlModifier && Config::instance()->ctrlCopyPaste()) {
            if (key->key() == Qt::Key_C) {
                const QString text = selectedText(true);
                if (!text.isEmpty() && !(m_copiedByCtrlC && text == m_copiedText)) {
                    QApplication::clipboard()->setText(text, QClipboard::Clipboard);
                    m_copiedText = text;
                    m_copiedByCtrlC = true;
                    return true;
                }
            } else if (key->key() == Qt::Key_V) {
                paste();
                return true;
            }
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton && Config::instance()->copyOnSelect()) {
            // The widget finishes the selection in its own release handler,
            // which runs after this filter.
            QTimer::singleShot(0, this, [this] {
                copy();
                // Already copied, so the next Ctrl+C stops the running
                // command instead of copying the selection again.
                m_copiedText = selectedText(true);
                m_copiedByCtrlC = !m_copiedText.isEmpty();
            });
        }
    }
    return QTermWidget::eventFilter(watched, event);
}
