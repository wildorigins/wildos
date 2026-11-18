#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <memory>

class FakeKeyboard;
class QMimeData;
class QTimer;

// The clipboard and emoji panel (Win+V, Win+full stop). It runs from sign-in
// so the clipboard history keeps everything copied since then, and a
// shortcut only shows or hides it. Picking something puts it on the
// clipboard, gives the keyboard back to the app you were in and presses
// Ctrl+V there (KWin's fake input, see FakeKeyboard). Emoji and symbols go
// in the same way without staying on the clipboard or in its history: what
// was copied before comes back a moment later. The panel closes after a
// clipboard item, as Windows' does, and stays open after an emoji or a
// symbol so several can go in: it steps off the screen for the moment the
// keys take, which is what gives the app the keyboard, and comes back.
class PanelApp : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY shownChanged)
    Q_PROPERTY(QString tab READ tab WRITE setTab NOTIFY tabChanged)
    Q_PROPERTY(bool canPaste READ canPaste CONSTANT)
    Q_PROPERTY(QStringList recentEmoji READ recentEmoji NOTIFY recentEmojiChanged)
    Q_PROPERTY(int skinTone READ skinTone WRITE setSkinTone NOTIFY skinToneChanged)
    Q_PROPERTY(int opened READ opened NOTIFY shownChanged)
    Q_PROPERTY(QString home READ home CONSTANT)
    // The panel is off the screen for a moment so the app has the keyboard.
    Q_PROPERTY(bool typing READ typing NOTIFY typingChanged)

public:
    explicit PanelApp(QObject *parent = nullptr);
    ~PanelApp() override;

    static PanelApp *instance();

    bool shown() const { return m_shown; }
    void setShown(bool shown);
    QString tab() const { return m_tab; }
    void setTab(const QString &tab);
    bool canPaste() const;
    QStringList recentEmoji() const { return m_recent; }
    int skinTone() const { return m_tone; }
    void setSkinTone(int tone);
    int opened() const { return m_opened; }
    QString home() const;
    bool typing() const { return m_typing; }

    // --emoji opens on the emoji tab, nothing (or --clipboard) on the
    // clipboard; the same shortcut again hides it. --daemon starts hidden.
    // --clear empties the history, the pins and the clipboard itself,
    // without showing anything (the panic key).
    void handle(const QStringList &arguments);

    // After the clipboard has been set: hide, then paste into the app.
    // `terminal` uses Ctrl+Shift+V, which terminals paste with.
    Q_INVOKABLE void pasteIntoApp(bool terminal);
    // An emoji or symbol, typed into the app through the clipboard.
    Q_INVOKABLE void typeText(const QString &text, bool terminal);
    Q_INVOKABLE void addRecentEmoji(const QString &emoji);
    Q_INVOKABLE void copyText(const QString &text);
    // After the history has gone: what is on the clipboard and the
    // middle-click selection goes too.
    Q_INVOKABLE void clearClipboard();

Q_SIGNALS:
    void shownChanged();
    void tabChanged();
    void recentEmojiChanged();
    void skinToneChanged();
    void clearRequested();
    void typingChanged();

private:
    void setTyping(bool typing);
    void typeNext();

    // Emoji picked faster than they can be typed wait their turn.
    struct Typed {
        QString text;
        bool terminal;
    };
    QList<Typed> m_queue;
    bool m_typing = false;
    bool m_busy = false;
    // What was on the clipboard before the first of them, put back once
    // the last has gone in.
    QMimeData *m_before = nullptr;
    QTimer *m_restore = nullptr;
    QTimer *m_takeBack = nullptr;
    bool m_shown = false;
    QString m_tab = QStringLiteral("clipboard");
    QStringList m_recent;
    int m_tone = 0;
    int m_opened = 0;
    std::unique_ptr<FakeKeyboard> m_keyboard;
};
