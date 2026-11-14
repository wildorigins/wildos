#include "protection.h"

#include <KConfigGroup>
#include <QFile>
#include <QFileSystemWatcher>

namespace
{
const QString storeDir = QStringLiteral("/etc/wildos");
const QString store = QStringLiteral("/etc/wildos/advanced-protection");
const char group[] = "RemoveDetails";
}

Protection *Protection::instance()
{
    static Protection *protection = new Protection;
    return protection;
}

Protection *Protection::create(QQmlEngine *, QJSEngine *)
{
    auto protection = instance();
    QJSEngine::setObjectOwnership(protection, QJSEngine::CppOwnership);
    return protection;
}

Protection::Protection(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_config(KSharedConfig::openConfig(QStringLiteral("wildosfilesrc")))
{
    // The helper replaces the file rather than writing into it, so the
    // folder is watched for it coming back.
    m_watcher->addPath(storeDir);
    if (QFile::exists(store)) {
        m_watcher->addPath(store);
    }
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &Protection::read);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &Protection::read);
    read();
}

void Protection::read()
{
    if (QFile::exists(store) && !m_watcher->files().contains(store)) {
        m_watcher->addPath(store);
    }
    bool scrub = true;
    bool box = true;
    QFile file(store);
    if (file.open(QIODevice::ReadOnly)) {
        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            const qsizetype eq = line.indexOf('=');
            if (eq <= 0) {
                continue;
            }
            const QByteArray key = line.left(eq).trimmed();
            const bool on = line.mid(eq + 1).trimmed() != "off";
            if (key == "scrub_files") {
                scrub = on;
            } else if (key == "safebox") {
                box = on;
            }
        }
    }
    if (scrub != m_scrubFiles || box != m_safeBox) {
        m_scrubFiles = scrub;
        m_safeBox = box;
        Q_EMIT changed();
    }
}

void Protection::write(const char *key, bool value)
{
    KConfigGroup(m_config, QString::fromLatin1(group)).writeEntry(key, value);
    m_config->sync();
    Q_EMIT choicesChanged();
}

bool Protection::removeLocation() const
{
    return KConfigGroup(m_config, QString::fromLatin1(group)).readEntry("Location", true);
}

void Protection::setRemoveLocation(bool on)
{
    write("Location", on);
}

bool Protection::removeDevice() const
{
    return KConfigGroup(m_config, QString::fromLatin1(group)).readEntry("Device", true);
}

void Protection::setRemoveDevice(bool on)
{
    write("Device", on);
}

bool Protection::removeAuthors() const
{
    return KConfigGroup(m_config, QString::fromLatin1(group)).readEntry("Authors", true);
}

void Protection::setRemoveAuthors(bool on)
{
    write("Authors", on);
}

bool Protection::safeBoxExplained() const
{
    return KConfigGroup(m_config, QStringLiteral("SafeBox")).readEntry("Explained", false);
}

void Protection::setSafeBoxExplained(bool done)
{
    KConfigGroup(m_config, QStringLiteral("SafeBox")).writeEntry("Explained", done);
    m_config->sync();
    Q_EMIT choicesChanged();
}
