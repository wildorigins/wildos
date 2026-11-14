// The panic key (Settings > Privacy and security > Advanced protection). The
// shortcut lives in KWin so it works whatever has the keyboard, a full-screen
// game included. The work runs as a user service, which this only starts:
// it then carries on even if KWin is busy with the lock.
//
// Loaded only while the switch is on ("panic-key switch", which
// wildos-panic-key.path runs when it changes), so with it off Ctrl+Alt+End
// reaches apps, such as Remote, which sends it on as Ctrl+Alt+Del.

function panic() {
    callDBus("org.freedesktop.systemd1", "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
             "StartUnit", "wildos-panic.service", "replace");
}

registerShortcut("Wild OS panic key", "Lock the screen, clear the clipboard and cut the network", "Ctrl+Alt+End", panic);
