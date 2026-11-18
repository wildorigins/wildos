#pragma once

#include <QPointer>
#include <QProcessEnvironment>
#include <QUrl>
#include <QVariantList>
#include <qtermwidget.h>

class TermLinks;

// One terminal: QTermWidget, the emulator Wild OS Terminal is built on,
// running ssh (or the person's shell for a local tab), with suggestions
// layered on top (RemoteTerminal.dc). Its colours, font, cursor and copy
// and paste habits are the ones saved in Terminal's settings
// (RemoteSettings).
//
// Remote can't see the shell's own line, so it follows what is typed since
// the last Enter. Anything that moves the cursor or edits the line in ways
// it can't follow (arrows, Tab completion, Ctrl+R) puts suggestions away
// until the next line. A line counts as history only when the terminal
// shows it back, which a password prompt never does.
class TerminalView : public QTermWidget
{
    Q_OBJECT

public:
    // `environment` is what the program runs with, before the terminal's own
    // settings (TERM and the like) go on top.
    TerminalView(const QString &program, const QStringList &args, const QString &historyKey,
                 QProcessEnvironment environment = QProcessEnvironment::systemEnvironment(), QWidget *parent = nullptr);

    QVariantList suggestions() const { return m_suggestions; }
    int selected() const { return m_selected; }
    // Where the cursor is, in this widget's coordinates.
    QRect cursorRect() const;

    void accept(int index);
    void dismiss();
    void setSelected(int index);
    // Types a command and presses Enter.
    void run(const QString &command);
    void copy();
    void paste();
    // Sends text as typed into the terminal, wrapped for bracketed paste so
    // a shell does not run it line by line.
    void sendPaste(QString text);
    bool hasSelection() const { return !const_cast<TerminalView *>(this)->selectedText(true).isEmpty(); }
    // Bigger or smaller text: +1, -1, or 0 for the size it opened at.
    void zoom(int step);
    bool exited() const { return m_exited; }
    // The link under a point of the widget, if any.
    QUrl linkAt(const QPoint &position) const;

Q_SIGNALS:
    void suggestionsChanged();
    void focused();
    void exitedChanged();
    // A right click, at this position in the widget.
    void contextMenuWanted(const QPoint &position);
    // More than one line is about to be pasted and Terminal's settings ask
    // for a check first.
    void pasteNeedsConfirming(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *display() const;
    void applySettings();
    void updateSuggestions();
    void received(const QString &text);
    void lineEntered();

    QString m_historyKey;
    QPointer<QWidget> m_display;
    TermLinks *m_links = nullptr;
    QString m_line;
    bool m_lineValid = true;
    // What the terminal has shown, escapes removed, the oldest dropped as it
    // grows; positions are counted from the start of the session.
    QString m_echo;
    qint64 m_echoDropped = 0;
    qint64 m_lineStart = 0;
    QVariantList m_suggestions;
    int m_selected = 0;
    bool m_hidden = false;
    bool m_exited = false;
    int m_zoom = 0;
    // The selection last copied, by selecting it or with Ctrl+C: Ctrl+C
    // with the same selection still in place stops the running command.
    QString m_copiedText;
    bool m_copied = false;
};
