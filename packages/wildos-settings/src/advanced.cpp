#include "advanced.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

namespace
{
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-advanced");
const QString storeDir = QStringLiteral("/etc/wildos");
const QString storeFile = QStringLiteral("/etc/wildos/advanced-protection");
const QString chosenFile = QStringLiteral("/etc/wildos/advanced-protection.chosen");
const QString hookDir = QStringLiteral("/usr/lib/wildos/advanced-protection.d");

// The board defaults, which a missing key stands for (settings-advanced
// keeps the same list).
const QList<QPair<QString, QString>> defaults{
    {QStringLiteral("usb_locked"), QStringLiteral("on")},
    {QStringLiteral("quiet_lock"), QStringLiteral("on")},
    {QStringLiteral("fingerprint_hours"), QStringLiteral("0")},
    {QStringLiteral("restart_locked_hours"), QStringLiteral("0")},
    {QStringLiteral("restart_locked_force"), QStringLiteral("off")},
    {QStringLiteral("radios_idle_off"), QStringLiteral("off")},
    {QStringLiteral("dns"), QStringLiteral("on")},
    {QStringLiteral("dns_strict"), QStringLiteral("off")},
    {QStringLiteral("dns_provider"), QStringLiteral("quad9")},
    {QStringLiteral("vpn_only"), QStringLiteral("off")},
    {QStringLiteral("clipboard_forget"), QStringLiteral("on")},
    {QStringLiteral("presenting"), QStringLiteral("on")},
    {QStringLiteral("scrub_files"), QStringLiteral("on")},
    {QStringLiteral("usb_encrypt"), QStringLiteral("on")},
    {QStringLiteral("guest"), QStringLiteral("off")},
    {QStringLiteral("safebox"), QStringLiteral("on")},
    {QStringLiteral("panic_key"), QStringLiteral("off")},
};

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}
}

Advanced::Advanced(QObject *parent)
    : QObject(parent)
{
    // The helper writes the store, then the chosen list.
    m_settle.setSingleShot(true);
    m_settle.setInterval(150);
    connect(&m_settle, &QTimer::timeout, this, &Advanced::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    refresh();
}

bool Advanced::available() const
{
    return QFileInfo(helper).isExecutable();
}

void Advanced::refresh()
{
    QHash<QString, QString> stored;
    for (const QString &line : readLines(storeFile)) {
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals > 0 && !line.startsWith(QLatin1Char('#'))) {
            stored.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
        }
    }
    QVariantMap values;
    QVariantMap installed;
    for (const auto &[key, fallback] : defaults) {
        const QString value = stored.value(key, fallback);
        bool number = false;
        const int hours = value.toInt(&number);
        values.insert(key, key.endsWith(QLatin1String("_hours")) ? QVariant(number ? hours : fallback.toInt()) : QVariant(value));
        installed.insert(key, QFileInfo(hookDir + QLatin1Char('/') + key).isExecutable());
    }
    QStringList chosen;
    for (const QString &line : readLines(chosenFile)) {
        chosen.append(line.trimmed());
    }

    for (const QString &dir : {storeDir, hookDir}) {
        if (QFileInfo::exists(dir) && !m_watcher.directories().contains(dir)) {
            m_watcher.addPath(dir);
        }
    }
    if (values != m_values || chosen != m_chosen || installed != m_installed) {
        m_values = values;
        m_chosen = chosen;
        m_installed = installed;
        Q_EMIT changed();
    }
}

void Advanced::run(const QStringList &args, const QString &key)
{
    if (m_process) {
        return;
    }
    m_busyKey = key;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::finished, this, [this, key](int code, QProcess::ExitStatus status) {
        m_process->deleteLater();
        m_process = nullptr;
        m_busyKey.clear();
        Q_EMIT busyChanged();
        refresh();
        if (status == QProcess::NormalExit && code == 0) {
            Q_EMIT done(key);
        } else if (status != QProcess::NormalExit || (code != 126 && code != 127)) {
            // 126 and 127: the password prompt was dismissed, which needs no
            // message. The switch goes back to the store's value.
            Q_EMIT failed(key, QStringLiteral("Something went wrong. Nothing was changed."));
        }
        // A switch flipped by hand has lost its binding; changed() puts
        // every row back to what the store says.
        Q_EMIT changed();
    });
    Q_EMIT busyChanged();
    m_process->start(QStringLiteral("pkexec"), QStringList{helper} + args);
}

void Advanced::set(const QString &key, const QVariant &value)
{
    QString text = value.typeId() == QMetaType::Bool ? (value.toBool() ? QStringLiteral("on") : QStringLiteral("off")) : value.toString();
    run({QStringLiteral("set"), key, text}, key);
}

void Advanced::reset()
{
    run({QStringLiteral("reset")}, QStringLiteral("reset"));
}
