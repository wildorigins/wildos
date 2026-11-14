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
// keys arrive, short enough not to be noticed.
constexpr int focusReturnDelay = 120;
// The app reads the clipboard when it gets Ctrl+V; give it time before
// the earlier contents go back.
constexpr int restoreDelay = 700;

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
    }
    Q_EMIT shownChanged();
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
    auto *clipboard = KSystemClipboard::instance();
    QMimeData *before = copyOf(clipboard->mimeData(QClipboard::Clipboard));

    auto *mime = new QMimeData;
    mime->setText(text);
    // Klipper leaves anything marked this way out of the history.
    mime->setData(QStringLiteral("x-kde-passwordManagerHint"), QByteArrayLiteral("secret"));
    clipboard->setMimeData(mime, QClipboard::Clipboard);

    if (!canPaste()) {
        // Nothing can press Ctrl+V for us, so it stays on the clipboard.
        delete before;
        setShown(false);
        return;
    }
    pasteIntoApp(terminal);
    QTimer::singleShot(focusReturnDelay + restoreDelay, this, [before] {
        if (before && !before->formats().isEmpty()) {
            KSystemClipboard::instance()->setMimeData(before, QClipboard::Clipboard);
        } else {
            delete before;
        }
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
