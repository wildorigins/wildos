#include "panelapp.h"

#include "fakekeyboard.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <KSystemClipboard>

#include <QDir>
#include <QMimeData>
#include <QTimer>

#include <algorithm>

namespace
{
PanelApp *s_instance = nullptr;

KConfigGroup settings()
{
    return KSharedConfig::openConfig(QStringLiteral("wildosclipboardrc"))->group(QStringLiteral("Emoji"));
}

// Long enough for KWin to hand the keyboard back to the app before the
// keys arrive. A picture of the panel stays on the screen meanwhile.
constexpr int focusReturnDelay = 160;
// The app reads the clipboard when it gets Ctrl+V; give it time before
// the earlier contents go back.
constexpr int restoreDelay = 700;
// Between two emoji picked in quick succession: the app has the keyboard
// already, and only has to have read the clipboard.
constexpr int nextDelay = 60;
// After the last one, before the panel comes back: the keys have gone by
// then, and the app reads the clipboard whether or not it has the keyboard.
constexpr int takeBackDelay = 40;

QMimeData *copyOf(const QMimeData *source)
{
    if (!source) {
        return nullptr;
    }
    auto *copy = new QMimeData;
    for (const QString &format : source->formats()) {
        copy->setData(format, source->data(format));
    }
    return copy;
}
}

PanelApp::PanelApp(QObject *parent)
    : QObject(parent)
    , m_keyboard(std::make_unique<FakeKeyboard>())
{
    s_instance = this;
    m_recent = settings().readEntry("Recent", QStringList());
    m_tone = std::clamp(settings().readEntry("SkinTone", 0), 0, 5);

    m_restore = new QTimer(this);
    m_restore->setSingleShot(true);
    m_restore->setInterval(restoreDelay);
    connect(m_restore, &QTimer::timeout, this, [this] {
        QMimeData *before = m_before;
        m_before = nullptr;
        if (before && !before->formats().isEmpty()) {
            KSystemClipboard::instance()->setMimeData(before, QClipboard::Clipboard);
        } else {
            delete before;
        }
    });
    m_takeBack = new QTimer(this);
    m_takeBack->setSingleShot(true);
    m_takeBack->setInterval(takeBackDelay);
    connect(m_takeBack, &QTimer::timeout, this, [this] {
        setTyping(false);
    });
}

PanelApp::~PanelApp()
{
    s_instance = nullptr;
}

PanelApp *PanelApp::instance()
{
    return s_instance;
}

QString PanelApp::home() const
{
    return QDir::homePath();
}

bool PanelApp::canPaste() const
{
    return m_keyboard->available();
}

void PanelApp::setShown(bool shown)
{
    if (m_shown == shown) {
        return;
    }
    m_shown = shown;
    if (shown) {
        ++m_opened;
    } else {
        // Closed while lending the keyboard: nothing to take back.
        m_takeBack->stop();
        setTyping(false);
    }
    Q_EMIT shownChanged();
}

void PanelApp::setTyping(bool typing)
{
    if (m_typing == typing) {
        return;
    }
    m_typing = typing;
    Q_EMIT typingChanged();
}

void PanelApp::setTab(const QString &tab)
{
    if (m_tab == tab) {
        return;
    }
    m_tab = tab;
    Q_EMIT tabChanged();
}

void PanelApp::setSkinTone(int tone)
{
    tone = std::clamp(tone, 0, 5);
    if (m_tone == tone) {
        return;
    }
    m_tone = tone;
    settings().writeEntry("SkinTone", tone);
    settings().sync();
    Q_EMIT skinToneChanged();
}

void PanelApp::handle(const QStringList &arguments)
{
    if (arguments.contains(QStringLiteral("--clear"))) {
        setShown(false);
        Q_EMIT clearRequested();
        return;
    }
    if (arguments.contains(QStringLiteral("--daemon"))) {
        return;
    }
    const QString wanted = arguments.contains(QStringLiteral("--emoji")) ? QStringLiteral("emoji") : QStringLiteral("clipboard");
    // The same shortcut again closes it, as on Windows; the other one
    // switches tabs.
    if (m_shown && m_tab == wanted) {
        setShown(false);
        return;
    }
    setTab(wanted);
    setShown(true);
}

void PanelApp::clearClipboard()
{
    auto *clipboard = KSystemClipboard::instance();
    clipboard->clear(QClipboard::Clipboard);
    clipboard->clear(QClipboard::Selection);
}

void PanelApp::pasteIntoApp(bool terminal)
{
    setShown(false);
    if (!canPaste()) {
        return;
    }
    QTimer::singleShot(focusReturnDelay, this, [this, terminal] {
        m_keyboard->paste(terminal);
    });
}

void PanelApp::typeText(const QString &text, bool terminal)
{
    if (!canPaste()) {
        // Nothing can press Ctrl+V for us, so it stays on the clipboard.
        auto *mime = new QMimeData;
        mime->setText(text);
        KSystemClipboard::instance()->setMimeData(mime, QClipboard::Clipboard);
        setShown(false);
        return;
    }
    m_queue.append({text, terminal});
    typeNext();
}

void PanelApp::typeNext()
{
    if (m_busy || m_queue.isEmpty()) {
        return;
    }
    m_busy = true;
    const Typed next = m_queue.takeFirst();

    auto *clipboard = KSystemClipboard::instance();
    if (!m_before) {
        m_before = copyOf(clipboard->mimeData(QClipboard::Clipboard));
    }
    m_restore->stop();
    auto *mime = new QMimeData;
    mime->setText(next.text);
    // Klipper leaves anything marked this way out of the history.
    mime->setData(QStringLiteral("x-kde-passwordManagerHint"), QByteArrayLiteral("secret"));
    clipboard->setMimeData(mime, QClipboard::Clipboard);

    // The first has to wait for the app to get the keyboard.
    const int wait = m_typing ? nextDelay : focusReturnDelay;
    m_takeBack->stop();
    setTyping(true);
    QTimer::singleShot(wait, this, [this, terminal = next.terminal] {
        m_keyboard->paste(terminal);
        QTimer::singleShot(nextDelay, this, [this] {
            m_busy = false;
            if (!m_queue.isEmpty()) {
                typeNext();
                return;
            }
            m_restore->start();
            if (m_shown) {
                m_takeBack->start();
            }
        });
    });
}

void PanelApp::copyText(const QString &text)
{
    auto *mime = new QMimeData;
    mime->setText(text);
    KSystemClipboard::instance()->setMimeData(mime, QClipboard::Clipboard);
}

void PanelApp::addRecentEmoji(const QString &emoji)
{
    m_recent.removeAll(emoji);
    m_recent.prepend(emoji);
    while (m_recent.size() > 24) {
        m_recent.removeLast();
    }
    settings().writeEntry("Recent", m_recent);
    settings().sync();
    Q_EMIT recentEmojiChanged();
}
