#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

// The few things a control needs from the system beyond drawing: whether a
// Wild OS tool is installed and starting it (the text menu's Emoji row
// opens the emoji panel), and the window menu a right click on a title bar
// opens, which Wild OS's KWin plugin draws and serves on the session bus,
// a person's password hint and whether there is a fingerprint reader.
class System : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit System(QObject *parent = nullptr);

    Q_INVOKABLE bool canRun(const QString &program) const;
    Q_INVOKABLE bool run(const QString &program, const QStringList &arguments = {}) const;
    Q_INVOKABLE bool hasWindowMenu() const;
    Q_INVOKABLE void showWindowMenu() const;
    // A double click on a resize edge (Qt::Edges): KWin stretches the window
    // to the screen's height or width, as on Windows.
    Q_INVOKABLE void maximizeEdge(int edges) const;
    // The hint a person set with their password (Welcome, Settings), shown
    // by the sign-in and lock screens after a wrong attempt.
    Q_INVOKABLE QString passwordHint(const QString &userName = {}) const;
    // A fingerprint reader fprintd can use. The sign-in screen runs as its
    // own user and can't see whose finger is enrolled, so it offers the
    // reader whenever there is one, unless Sign-in options has "Sign in and
    // unlock" off.
    Q_INVOKABLE bool fingerprintReader() const;
    // Whether a finger may be used for this account now. Advanced
    // protection's "Fingerprint needs your password sometimes" turns it
    // down after every restart until the password is typed once, and again
    // once the chosen hours have passed since (wildos-desktop's
    // fingerprint-turn decides the same way, from the same files).
    Q_INVOKABLE bool fingerprintAllowed(const QString &userName) const;
    // Whether the account running this has a lock screen PIN
    // (wildos-system-dialogs' wildos-pin keeps it; only that it exists can
    // be seen from here).
    Q_INVOKABLE bool pinSet() const;
    // Whether the sign-in screen takes that account's PIN: it has one, and
    // Advanced protection's "Fingerprint needs your password sometimes" is
    // off (pam_wildos_pin sends the PIN to the password while it is on).
    Q_INVOKABLE bool pinSignIn(const QString &userName) const;

    // A PC with two graphics cards, usually a laptop with a power-saving
    // one for the desktop and a faster one that stays off until asked for.
    Q_INVOKABLE bool twoGraphicsCards() const;
    // Whether a right-click menu can offer the faster card for this: an app
    // (its .desktop file, by id or path, with or without "applications:")
    // or a Linux program such as an AppImage (its path or file: address).
    Q_INVOKABLE bool canChooseGraphics(const QString &target) const;
    // The faster card is chosen for it. The choice is a list in wildosrc
    // ([Graphics] Dedicated: an app's .desktop file name, or a program's or
    // a shortcut's path), which Wild OS's KIO reads each time it starts
    // something, so every launcher honours it. An app that asks for the
    // card itself (PrefersNonDefaultGPU) is not offered the choice.
    Q_INVOKABLE bool dedicatedGraphics(const QString &target) const;
    Q_INVOKABLE void setDedicatedGraphics(const QString &target, bool on) const;
};
