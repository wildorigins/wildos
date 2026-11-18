#pragma once

#include <QKeySequence>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QWindow>

class KKeySequenceRecorder;


// Keyboard shortcuts that work everywhere (Settings > Bluetooth and devices
// > Keyboard > Keyboard shortcuts). The desktop's shortcut service,
// kglobalaccel, keeps them and hands each to the program that acts on it;
// this reads and changes them there, so a change counts at once and nothing
// has to restart.
//
// Wild OS's own shortcuts are a fixed list under names a person would use.
// The service knows many more, most of them KWin's for things Wild OS does
// not offer; those stay as they are. "Your shortcuts" start an app or run a
// command: the service starts whatever a .desktop file names, so a command
// gets a small one of its own in the account's kglobalaccel folder.
class Shortcuts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Each: id, group (a heading), name, changed (differs from what Wild
    // OS sets) and keys, one entry per shortcut it has: keys (the form the
    // calls below take, "Meta+D") and parts (as drawn, ["Super", "D"]).
    Q_PROPERTY(QVariantList system READ system NOTIFY changed)
    // Each: id, name, icon, command (empty for an app), keys.
    Q_PROPERTY(QVariantList own READ own NOTIFY changed)
    Q_PROPERTY(bool anyChanged READ anyChanged NOTIFY changed)
    // A key press is being waited for; shown is what is held so far.
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QStringList shown READ shownParts NOTIFY shownChanged)

public:
    explicit Shortcuts(QObject *parent = nullptr);

    QVariantList system() const { return m_system; }
    QVariantList own() const { return m_own; }
    bool anyChanged() const;
    bool recording() const;

    Q_INVOKABLE void reload();

    // Waits for the next key combination typed into the window. While it
    // waits the desktop's own shortcuts are held back, so pressing one that
    // is taken records it instead of running it. recorded() carries it in
    // the form setKeys() and the others take.
    Q_INVOKABLE void record(QWindow *window);
    Q_INVOKABLE void stopRecording();

    // What already uses the keys, as its name, or nothing. An id is left out
    // of the search, so keeping a shortcut's own keys is not a clash.
    Q_INVOKABLE QString usedBy(const QString &keys, const QString &exceptId = {}) const;

    // replace: the keys become the only ones; otherwise they are added to
    // those it has. Whatever else used them gives them up.
    Q_INVOKABLE void setKeys(const QString &id, const QString &keys, bool replace);
    Q_INVOKABLE void removeKeys(const QString &id, const QString &keys);
    Q_INVOKABLE void reset(const QString &id);
    Q_INVOKABLE void resetAll();

    // Installed apps to choose from: id, name, icon.
    Q_INVOKABLE QVariantList apps() const;
    Q_INVOKABLE void addApp(const QString &app, const QString &keys);
    Q_INVOKABLE void addCommand(const QString &name, const QString &command, const QString &keys);
    Q_INVOKABLE void removeOwn(const QString &id);

    // "Meta+D" as the design draws a shortcut, one key per chip:
    // ["Super", "D"]; and as one phrase, "Super + D".
    Q_INVOKABLE QStringList parts(const QString &keys) const;
    Q_INVOKABLE QString display(const QString &keys) const;
    QStringList shownParts() const;

Q_SIGNALS:
    void changed();
    void recordingChanged();
    void shownChanged();
    void recorded(const QString &keys);

private:
    QStringList actionId(const QString &id) const;
    QList<QKeySequence> keysOf(const QString &id, bool defaults = false) const;
    void apply(const QString &id, const QList<QKeySequence> &keys);
    void release(const QKeySequence &keys, const QString &exceptId);
    QStringList ownIds() const;
    void setOwnIds(const QStringList &ids);
    void registerOwn(const QString &component, const QString &name, const QString &keys);

    QVariantList m_system;
    QVariantList m_own;
    KKeySequenceRecorder *m_recorder = nullptr;
};
