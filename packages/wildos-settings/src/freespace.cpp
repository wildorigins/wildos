#include "freespace.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>

namespace
{
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-free-space");
const QString timer = QStringLiteral("wildos-free-space.timer");
// The items settings-free-space clears as root; the rest are the person's.
const QStringList systemItems = {QStringLiteral("restorePoints"), QStringLiteral("updates"), QStringLiteral("leftovers"),
                                 QStringLiteral("winmode")};

QString prefsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/wildos/free-space.json");
}
}

FreeSpace::FreeSpace(QObject *parent)
    : QObject(parent)
{
    readDisk();
    readPrefs();
}

void FreeSpace::readDisk()
{
    // root() hands back a copy made once; without refresh() the free space
    // would not change after clearing.
    QStorageInfo root = QStorageInfo::root();
    root.refresh();
    m_diskTotal = double(root.bytesTotal());
    m_diskFree = double(root.bytesAvailable());
}

void FreeSpace::readPrefs()
{
    QFile file(prefsPath());
    const QJsonObject prefs = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    m_hasSavedItems = prefs.contains(QStringLiteral("items"));
    m_savedItems.clear();
    for (const QJsonValue &v : prefs.value(QStringLiteral("items")).toArray()) {
        m_savedItems << v.toString();
    }
    m_autoOn = prefs.value(QStringLiteral("auto")).toBool();
    Q_EMIT prefsChanged();
}

void FreeSpace::writePrefs()
{
    QDir().mkpath(QFileInfo(prefsPath()).absolutePath());
    QSaveFile file(prefsPath());
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    QJsonObject prefs;
    prefs[QStringLiteral("auto")] = m_autoOn;
    prefs[QStringLiteral("items")] = QJsonArray::fromStringList(m_savedItems);
    file.write(QJsonDocument(prefs).toJson());
    file.commit();
}

QStringList FreeSpace::systemPart(const QStringList &items)
{
    QStringList out;
    for (const QString &i : systemItems) {
        if (items.contains(i)) {
            out << i;
        }
    }
    return out;
}

void FreeSpace::measure()
{
    if (m_pending > 0 || m_cleaning) {
        return;
    }
    readDisk();
    m_pending = 2;
    Q_EMIT changed();
    start(QStringLiteral("pkexec"), {helper, QStringLiteral("measure")}, true);
    start(helper, {QStringLiteral("measure-home")}, false);
}

void FreeSpace::start(const QString &program, const QStringList &args, bool system)
{
    auto process = new QProcess(this);
    // stderr is only for people running it by hand; the JSON is on stdout.
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::finished, this, [this, process, system](int code, QProcess::ExitStatus status) {
        const QJsonDocument doc = QJsonDocument::fromJson(process->readAllStandardOutput());
        process->deleteLater();
        const bool ok = status == QProcess::NormalExit && code == 0 && doc.isObject();
        if (system) {
            m_system = ok ? doc.object().toVariantMap() : QVariantMap();
            m_systemFailed = !ok;
        } else {
            m_home = ok ? doc.object().toVariantMap() : QVariantMap();
        }
        --m_pending;
        Q_EMIT changed();
    });
    // A program that never starts never finishes either.
    connect(process, &QProcess::errorOccurred, this, [this, process, system](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) {
            return;
        }
        process->deleteLater();
        if (system) {
            m_system.clear();
            m_systemFailed = true;
        } else {
            m_home.clear();
        }
        --m_pending;
        Q_EMIT changed();
    });
    process->start(program, args);
}

void FreeSpace::clean(const QStringList &items, const QStringList &downloads)
{
    if (m_cleaning || m_pending > 0 || items.isEmpty()) {
        return;
    }
    QStringList args{QStringLiteral("run")};
    args << items;
    for (const QString &path : downloads) {
        args << QStringLiteral("--download") << path;
    }
    m_cleaning = true;
    Q_EMIT changed();
    auto process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::finished, this, [this, process](int, QProcess::ExitStatus) {
        const QVariantMap result = QJsonDocument::fromJson(process->readAllStandardOutput()).object().toVariantMap();
        process->deleteLater();
        m_cleaning = false;
        Q_EMIT changed();
        Q_EMIT cleaned(result);
        measure();
    });
    process->start(helper, args);
}

void FreeSpace::saveItems(const QStringList &items)
{
    const QStringList before = systemPart(m_savedItems);
    m_savedItems = items;
    m_hasSavedItems = true;
    writePrefs();
    Q_EMIT prefsChanged();
    // Tidy up automatically clears what is ticked, so its stored system
    // items follow the ticks; that takes the password again (kept for a
    // few minutes after the last time).
    if (m_autoOn && systemPart(items) != before) {
        QProcess::startDetached(QStringLiteral("pkexec"), QStringList{helper, QStringLiteral("set-auto")} + systemPart(items));
    }
}

void FreeSpace::setAuto(bool on, const QStringList &items)
{
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, on, items](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            m_autoOn = on;
            m_savedItems = items;
            m_hasSavedItems = true;
            writePrefs();
            QProcess::startDetached(QStringLiteral("systemctl"),
                                    {QStringLiteral("--user"), on ? QStringLiteral("enable") : QStringLiteral("disable"),
                                     QStringLiteral("--now"), timer});
        }
        // Also when the password was not given, so the switch goes back.
        Q_EMIT prefsChanged();
    });
    process->start(QStringLiteral("pkexec"), QStringList{helper, QStringLiteral("set-auto")} + (on ? systemPart(items) : QStringList()));
}
