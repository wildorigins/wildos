#include "terminalview.h"
#include "settings.h"
#include "snippets.h"
#include "termlinks.h"

#include <QApplication>
#include <algorithm>
#include <QClipboard>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QDir>
#include <QKeyEvent>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTimer>

namespace
{
const QString scrollBarStyle = QStringLiteral(
    "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; border: none; }"
    "QScrollBar::handle:vertical { background: rgba(255,255,255,0.28); border-radius: 3px; min-height: 32px; margin: 0px 2px; }"
    "QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.40); margin: 0px 1px; }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; border: none; background: none; }"
    "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }");
}

TerminalView::TerminalView(const QString &program, const QStringList &args, const QString &historyKey, QProcessEnvironment env, QWidget *parent)
    : QTermWidget(0, parent)
    , m_historyKey(historyKey)
{
    setShellProgram(program);
    setArgs(args);
    setWorkingDirectory(QDir::homePath());
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    env.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    env.remove(QStringLiteral("DESKTOP_STARTUP_ID"));
    env.remove(QStringLiteral("XDG_ACTIVATION_TOKEN"));
    setEnvironment(env.toStringList());

    setTerminalSizeHint(false);
    setFlowControlEnabled(false);
    setFlowControlWarningEnabled(false);
    setConfirmMultilinePaste(false);
    setMotionAfterPasting(2);
    setScrollBarPosition(QTermWidget::ScrollBarRight);
    setMargin(8);
    // The output stays when ssh ends, so the reason can be read.
    setAutoClose(false);
    if (QScrollBar *bar = findChild<QScrollBar *>()) {
        bar->setAttribute(Qt::WA_TranslucentBackground);
        bar->setAutoFillBackground(false);
        bar->setStyleSheet(scrollBarStyle);
    }

    m_display = display();
    if (m_display) {
        m_display->installEventFilter(this);
        m_links = new TermLinks(this, m_display);
        connect(m_links, &TermLinks::activated, this, [](const QUrl &url) {
            QDesktopServices::openUrl(url);
        });
    }
    // The look and the copy and paste habits are Terminal's settings, and
    // follow them as they change.
    applySettings();
    connect(RemoteSettings::instance(), &RemoteSettings::terminalChanged, this, &TerminalView::applySettings);
    connect(this, &QTermWidget::copyAvailable, this, [this](bool) {
        m_copied = false;
    });
    connect(this, &QTermWidget::termGetFocus, this, &TerminalView::focused);
    connect(this, &QTermWidget::receivedData, this, &TerminalView::received);
    connect(this, &QTermWidget::finished, this, [this] {
        m_exited = true;
        dismiss();
        Q_EMIT exitedChanged();
    });
    // Right click opens Remote's menu (links, Copy, Paste, Clear), as in
    // Terminal, or pastes when that is chosen there; Shift then still
    // opens the menu.
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        if (RemoteSettings::instance()->pasteOnRightClick() && !(QApplication::keyboardModifiers() & Qt::ShiftModifier)) {
            paste();
        } else {
            Q_EMIT contextMenuWanted(position);
        }
    });
    startShellProgram();
}

void TerminalView::applySettings()
{
    const RemoteSettings *settings = RemoteSettings::instance();
    setColorScheme(settings->schemePath());
    QFont font = settings->font();
    if (m_zoom != 0) {
        font.setPointSizeF(std::max(4.0, font.pointSizeF() + m_zoom));
    }
    setTerminalFont(font);
    const QString shape = settings->cursorShape();
    setKeyboardCursorShape(shape == QLatin1String("block") ? KeyboardCursorShape::BlockCursor
                           : shape == QLatin1String("underline") ? KeyboardCursorShape::UnderlineCursor
                                                                  : KeyboardCursorShape::IBeamCursor);
    setBlinkingCursor(settings->cursorBlink());
    setBoldIntense(settings->boldBright());
    setHistorySize(settings->scrollback());
    if (m_links) {
        m_links->setColour(settings->foreground());
    }
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

QUrl TerminalView::linkAt(const QPoint &position) const
{
    return m_links ? m_links->linkAt(position) : QUrl();
}

QRect TerminalView::cursorRect() const
{
    if (!m_display) {
        return {};
    }
    const QRect r = m_display->inputMethodQuery(Qt::ImCursorRectangle).toRect();
    return QRect(m_display->mapTo(this, r.topLeft()), r.size());
}

void TerminalView::received(const QString &text)
{
    // Only the printable part matters: the line as the shell shows it back.
    static const QRegularExpression escapes(QStringLiteral("\x1b\\[[0-9;?]*[ -/]*[@-~]|\x1b\\][^\x07\x1b]*(\x07|\x1b\\\\)|\x1b[@-_]|[\x00-\x08\x0e-\x1f]"));
    m_echo += QString(text).remove(escapes);
    if (m_echo.size() > 16384) {
        const qsizetype drop = m_echo.size() - 8192;
        m_echo.remove(0, drop);
        m_echoDropped += drop;
    }
}

void TerminalView::lineEntered()
{
    // The shell shows the line back as it is typed, but over a slow link
    // the last of it can arrive after Enter: it is looked for a moment
    // later, in what the terminal has shown since the line began.
    const QString line = m_line;
    const qint64 from = m_lineStart;
    m_lineStart = m_echoDropped + m_echo.size();
    if (!m_lineValid || line.trimmed().isEmpty() || m_historyKey.isEmpty()) {
        return;
    }
    QTimer::singleShot(400, this, [this, line, from] {
        const qint64 at = std::max<qint64>(0, from - m_echoDropped);
        if (m_echo.mid(at).contains(line.trimmed())) {
            Snippets::instance()->record(m_historyKey, line);
        }
    });
}

void TerminalView::updateSuggestions()
{
    QVariantList next;
    if (m_lineValid && !m_hidden && !m_historyKey.isEmpty()) {
        next = Snippets::instance()->suggest(m_historyKey, m_line);
    }
    if (next != m_suggestions) {
        m_suggestions = next;
        m_selected = 0;
        Q_EMIT suggestionsChanged();
    }
}

void TerminalView::dismiss()
{
    m_hidden = true;
    if (!m_suggestions.isEmpty()) {
        m_suggestions.clear();
        Q_EMIT suggestionsChanged();
    }
}

void TerminalView::setSelected(int index)
{
    if (index >= 0 && index < m_suggestions.size() && index != m_selected) {
        m_selected = index;
        Q_EMIT suggestionsChanged();
    }
}

void TerminalView::accept(int index)
{
    if (index < 0 || index >= m_suggestions.size()) {
        return;
    }
    const QVariantMap s = m_suggestions[index].toMap();
    const QString completion = s.value(QStringLiteral("completion")).toString();
    if (completion.startsWith(m_line) && !s.value(QStringLiteral("replace")).toBool()) {
        sendText(completion.mid(m_line.size()));
    } else {
        // Ctrl+U clears what is typed before the cursor, in bash, zsh and
        // fish alike.
        sendText(QStringLiteral("\x15") + completion);
    }
    m_line = completion;
    dismiss();
    if (m_display) {
        m_display->setFocus();
    }
}

void TerminalView::run(const QString &command)
{
    sendText(QStringLiteral("\x15") + command + QLatin1Char('\r'));
    m_line.clear();
    m_lineValid = true;
    m_lineStart = m_echoDropped + m_echo.size();
    dismiss();
    m_hidden = false;
}

void TerminalView::copy()
{
    const QString text = selectedText(true);
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void TerminalView::zoom(int step)
{
    m_zoom = step == 0 ? 0 : std::clamp(m_zoom + step, -6, 30);
    applySettings();
}

void TerminalView::paste()
{
    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty()) {
        return;
    }
    QString body = text;
    while (body.endsWith(QLatin1Char('\n')) || body.endsWith(QLatin1Char('\r'))) {
        body.chop(1);
    }
    if (RemoteSettings::instance()->warnMultilinePaste() && (body.contains(QLatin1Char('\n')) || body.contains(QLatin1Char('\r')))) {
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
    m_lineValid = false;
}

bool TerminalView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_display) {
        return QTermWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        // The window's own shortcuts (Ctrl+Shift+..., and Ctrl+, for
        // settings) win over the emulator.
        const bool settingsKey = key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_Comma;
        if (settingsKey || ((key->modifiers() & Qt::ControlModifier) && (key->modifiers() & Qt::ShiftModifier))) {
            event->ignore();
            return true;
        }
        return QTermWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        // Selecting text copies it, as in PuTTY, unless that is switched off
        // in Terminal's settings. The widget finishes the selection in its
        // own release handler, which runs after this filter.
        if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton && RemoteSettings::instance()->copyOnSelect()) {
            QTimer::singleShot(0, this, [this] {
                if (hasSelection()) {
                    copy();
                    // Already copied, so Ctrl+C stops the running command
                    // instead of copying the selection again.
                    m_copiedText = selectedText(true);
                    m_copied = true;
                }
            });
        }
        return QTermWidget::eventFilter(watched, event);
    }
    if (event->type() != QEvent::KeyPress) {
        return QTermWidget::eventFilter(watched, event);
    }
    auto *key = static_cast<QKeyEvent *>(event);
    const auto mods = key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);

    if (!m_suggestions.isEmpty()) {
        switch (key->key()) {
        case Qt::Key_Tab:
            accept(m_selected);
            return true;
        case Qt::Key_Down:
            setSelected((m_selected + 1) % m_suggestions.size());
            return true;
        case Qt::Key_Up:
            setSelected((m_selected + m_suggestions.size() - 1) % m_suggestions.size());
            return true;
        case Qt::Key_Escape:
            dismiss();
            return true;
        default:
            break;
        }
    }

    // Ctrl+V pastes. Ctrl+C stops the running command: selecting already
    // copied, and a selection left on screen must never turn the way to stop
    // something into Copy. With Terminal's "Ctrl+C copies and Ctrl+V pastes"
    // set, Ctrl+C copies a selection that has not been copied yet, once, as
    // it does there, and switched off, Ctrl+V goes to the program like any
    // other key. Ctrl+= and Ctrl+- zoom, Ctrl+0 resets.
    if (mods == Qt::ControlModifier && !(key->modifiers() & Qt::ShiftModifier)) {
        if (key->key() == Qt::Key_C && RemoteSettings::instance()->ctrlCopies()) {
            const QString text = selectedText(true);
            if (!text.isEmpty() && !(m_copied && text == m_copiedText)) {
                QApplication::clipboard()->setText(text);
                m_copiedText = text;
                m_copied = true;
                return true;
            }
        }
        if (key->key() == Qt::Key_V && RemoteSettings::instance()->ctrlPastes()) {
            paste();
            return true;
        }
        if (key->key() == Qt::Key_Equal || key->key() == Qt::Key_Plus) {
            zoom(1);
            return true;
        }
        if (key->key() == Qt::Key_Minus) {
            zoom(-1);
            return true;
        }
        if (key->key() == Qt::Key_0) {
            zoom(0);
            return true;
        }
    }
    if (mods == Qt::ControlModifier && key->modifiers() & Qt::ShiftModifier) {
        if (key->key() == Qt::Key_C) {
            copy();
            return true;
        }
        if (key->key() == Qt::Key_V) {
            paste();
            return true;
        }
    }

    switch (key->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        lineEntered();
        m_line.clear();
        m_lineValid = true;
        dismiss();
        m_hidden = false;
        break;
    case Qt::Key_Backspace:
        if (!m_line.isEmpty()) {
            m_line.chop(1);
        }
        m_hidden = false;
        QTermWidget::eventFilter(watched, event);
        updateSuggestions();
        return false;
    default:
        if (mods == Qt::NoModifier && !key->text().isEmpty() && key->text().at(0).isPrint()) {
            m_line += key->text();
            const bool result = QTermWidget::eventFilter(watched, event);
            updateSuggestions();
            return result;
        }
        // Arrows, Tab, Ctrl+anything: the line is no longer one Remote can
        // follow.
        if (key->key() != Qt::Key_Shift && key->key() != Qt::Key_Control && key->key() != Qt::Key_Alt && key->key() != Qt::Key_Meta) {
            m_lineValid = false;
            dismiss();
        }
        break;
    }
    return QTermWidget::eventFilter(watched, event);
}
