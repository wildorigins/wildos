#pragma once

#include <QByteArray>
#include <QClipboard>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

// Advanced protection's "Clipboard forgets passwords" (clipboard_forget in
// /etc/wildos/advanced-protection, on unless it says off). Anything put on
// the clipboard marked x-kde-passwordManagerHint=secret, as password
// managers and Wild OS's own password boxes mark what they copy, is wiped
// 60 seconds later, if the clipboard still holds it then. Lives in the
// clipboard panel because that runs in every session from sign-in.
class SecretWiper : public QObject
{
    Q_OBJECT

public:
    explicit SecretWiper(QObject *parent = nullptr);

private:
    void readChoice();
    void clipboardChanged(QClipboard::Mode mode);
    void wipe();
    QByteArray fingerprint() const;

    QFileSystemWatcher m_watcher;
    QTimer m_timer;
    QByteArray m_secret;
    bool m_on = true;
};
