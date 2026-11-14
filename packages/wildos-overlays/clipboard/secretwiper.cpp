#include "secretwiper.h"

#include <KSystemClipboard>

#include <QCryptographicHash>
#include <QFile>
#include <QMimeData>

namespace
{
const QString storeDir = QStringLiteral("/etc/wildos");
const QString storeFile = QStringLiteral("/etc/wildos/advanced-protection");
const QString hint = QStringLiteral("x-kde-passwordManagerHint");
constexpr int forgetAfter = 60 * 1000;
}

SecretWiper::SecretWiper(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(forgetAfter);
    connect(&m_timer, &QTimer::timeout, this, &SecretWiper::wipe);
    connect(KSystemClipboard::instance(), &KSystemClipboard::changed, this, &SecretWiper::clipboardChanged);
    // The store is replaced as a whole, so its folder is what changes.
    m_watcher.addPath(storeDir);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &SecretWiper::readChoice);
    readChoice();
    clipboardChanged(QClipboard::Clipboard);
}

void SecretWiper::readChoice()
{
    bool on = true;
    QFile file(storeFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            if (line.startsWith("clipboard_forget=")) {
                on = line.mid(17) != "off";
            }
        }
    }
    if (on == m_on) {
        return;
    }
    m_on = on;
    if (!on) {
        m_timer.stop();
        m_secret.clear();
    } else {
        clipboardChanged(QClipboard::Clipboard);
    }
}

QByteArray SecretWiper::fingerprint() const
{
    const QMimeData *data = KSystemClipboard::instance()->mimeData(QClipboard::Clipboard);
    if (!data) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &format : data->formats()) {
        hash.addData(format.toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(data->data(format));
    }
    return hash.result();
}

void SecretWiper::clipboardChanged(QClipboard::Mode mode)
{
    if (mode != QClipboard::Clipboard || !m_on) {
        return;
    }
    const QMimeData *data = KSystemClipboard::instance()->mimeData(QClipboard::Clipboard);
    if (data && data->data(hint) == "secret") {
        m_secret = fingerprint();
        m_timer.start();
    } else {
        m_timer.stop();
        m_secret.clear();
    }
}

void SecretWiper::wipe()
{
    if (!m_on || m_secret.isEmpty()) {
        return;
    }
    // Only the entry that was copied: anything copied since stays.
    if (fingerprint() == m_secret) {
        KSystemClipboard::instance()->clear(QClipboard::Clipboard);
    }
    m_secret.clear();
}
