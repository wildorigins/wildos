#pragma once

#include <QPointer>
#include <QUrl>
#include <qtermwidget.h>

class TermLinks;

// One shell: QTermWidget, the emulator LXQt's terminal is built on, set up
// from the profile and the appearance settings, with the Windows Terminal
// clipboard habits layered on top (Ctrl+C copies a selection, Ctrl+V
// pastes, optional copy on select and paste on right-click).
class TerminalView : public QTermWidget
{
    Q_OBJECT

public:
    TerminalView(const QVariantMap &profile, const QString &directory, const QStringList &command, QWidget *parent = nullptr);

    QString profileId() const { return m_profileId; }
    QString iconName() const { return m_icon; }
    QString displayTitle() const;
    QString currentDirectory();

    void applyConfig();
    void copy();
    void paste();
    // Sends text as typed into the terminal, wrapped for bracketed paste
    // so a shell does not run it line by line.
    void sendPaste(QString text);
    bool hasSelection();
    void zoomBy(int step);
    void resetZoom();
    void setOutlined(bool on);

    // The link under the point the context menu was asked for, if any.
    QUrl linkAt(const QPoint &pos) const;

Q_SIGNALS:
    void focused();
    void titleUpdated();
    void contextMenuWanted(const QPoint &pos);
    void pasteNeedsConfirming(const QString &text);
    void closed();
    // A window shortcut typed in the terminal, by name.
    void windowKey(const QString &action);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *display() const;

    QString m_profileId;
    QString m_profileName;
    QString m_icon;
    QPointer<QWidget> m_display;
    QWidget *m_outline = nullptr;
    TermLinks *m_links = nullptr;
    int m_zoom = 0;
    // The selection Ctrl+C last copied: pressing it again with the same
    // selection still in place sends the interrupt, as in Windows Terminal
    // once the selection is gone.
    QString m_copiedText;
    bool m_copiedByCtrlC = false;
};
