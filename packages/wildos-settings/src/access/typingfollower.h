#pragma once

#include <QObject>
#include <QStringList>

// Typing methods in the session (see keyboard.h). While a layout in the list
// has one, KWin's input method is Fcitx 5 in place of the on-screen
// keyboard, as KWin runs only one; with none left, the on-screen keyboard's
// setting comes back as it was. Switching layout (Windows key + Space, or
// the taskbar) turns the method on for its entry and off for the others.
//
// Fcitx stays the person's own: engines and settings they add in Fcitx's
// settings are kept, and an engine they switched to themselves is never
// switched away from.
class TypingFollower : public QObject
{
    Q_OBJECT

public:
    explicit TypingFollower(QObject *parent = nullptr);

private Q_SLOTS:
    void sync();
    void follow(uint index);
    void fcitxStarted();
    void fcitxStopped();

private:
    void handToFcitx();
    void giveBack();
    void addEngines();
    void restartFcitx();
    QStringList enginesWanted() const;

    QStringList m_methods; // one per KWin layout, empty for a plain one
    uint m_current = 0;
    // An engine downloaded while Fcitx runs is only seen by Fcitx once it
    // starts again; set while KWin restarts it for that.
    bool m_restarting = false;
    QStringList m_restartedFor;
};
