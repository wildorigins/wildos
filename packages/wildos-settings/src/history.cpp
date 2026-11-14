#include "history.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>

#include <unistd.h>

namespace
{
const QString dir = QStringLiteral("/var/lib/wildos/history");
const QString socketPath = QStringLiteral("/run/wildos-history.sock");
constexpr qint64 keepSeconds = 30 * 24 * 3600;

QString backupHistory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/wildos-backup/history.json");
}

QVariantMap techLine(const QDateTime &when, const QString &source, const QString &text)
{
    return {{QStringLiteral("when"), when}, {QStringLiteral("source"), source}, {QStringLiteral("text"), text}};
}
}

History::History(QObject *parent)
    : QObject(parent)
{
    // The collector writes several files in one go; one read after it.
    m_settle.setSingleShot(true);
    m_settle.setInterval(300);
    connect(&m_settle, &QTimer::timeout, this, &History::load);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_settle, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_settle, qOverload<>(&QTimer::start));
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(20000);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_socket) {
            m_reply = QStringLiteral("error It took too long.");
            m_socket->abort();
        }
    });
    load();
}

void History::refresh()
{
    if (m_socket) {
        return;
    }
    m_loading = true;
    Q_EMIT loadingChanged();
    m_reply.clear();
    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::readyRead, this, [this]() {
        m_reply += QString::fromUtf8(m_socket->readAll());
    });
    const auto finish = [this]() {
        if (!m_socket) {
            return;
        }
        m_socket->deleteLater();
        m_socket = nullptr;
        m_timeout.stop();
        m_loading = false;
        Q_EMIT loadingChanged();
        load();
    };
    connect(m_socket, &QLocalSocket::disconnected, this, finish);
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this, finish](QLocalSocket::LocalSocketError error) {
        if (error != QLocalSocket::PeerClosedError && m_reply.isEmpty()) {
            m_reply = QStringLiteral("error ") + m_socket->errorString();
        }
        finish();
    });
    m_timeout.start();
    m_socket->connectToServer(socketPath);
}

void History::readFile(const QString &path, QVariantList &into, bool *readable)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *readable = false;
        return;
    }
    const qint64 oldest = QDateTime::currentSecsSinceEpoch() - keepSeconds;
    while (!file.atEnd()) {
        const QJsonObject o = QJsonDocument::fromJson(file.readLine()).object();
        const double t = o.value(QStringLiteral("t")).toDouble();
        if (o.isEmpty() || t < oldest) {
            continue;
        }
        QVariantList tech;
        for (const QJsonValue &v : o.value(QStringLiteral("tech")).toArray()) {
            const QJsonArray line = v.toArray();
            tech.append(techLine(QDateTime::fromMSecsSinceEpoch(qint64(line.at(0).toDouble() * 1000)), line.at(1).toString(), line.at(2).toString()));
        }
        into.append(QVariantMap{{QStringLiteral("id"), o.value(QStringLiteral("id")).toString()},
                                {QStringLiteral("when"), QDateTime::fromMSecsSinceEpoch(qint64(t * 1000))},
                                {QStringLiteral("kind"), o.value(QStringLiteral("kind")).toString()},
                                {QStringLiteral("data"), o.value(QStringLiteral("data")).toObject().toVariantMap()},
                                {QStringLiteral("tech"), tech}});
    }
}

// Backup keeps its own runs in the home folder, where the root collector
// has no business reading.
void History::readBackups(QVariantList &into)
{
    QFile file(backupHistory());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QDateTime oldest = QDateTime::currentDateTime().addSecs(-keepSeconds);
    for (const QJsonValue &v : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject o = v.toObject();
        const QDateTime at = QDateTime::fromString(o.value(QStringLiteral("at")).toString(), Qt::ISODate);
        QDateTime finished = QDateTime::fromString(o.value(QStringLiteral("finished")).toString(), Qt::ISODate);
        if (!at.isValid() || at < oldest) {
            continue;
        }
        if (!finished.isValid()) {
            finished = at;
        }
        const QString result = o.value(QStringLiteral("result")).toString();
        const QString drive = o.value(QStringLiteral("driveName")).toString();
        const int files = o.value(QStringLiteral("filesNew")).toInt() + o.value(QStringLiteral("filesChanged")).toInt();
        QVariantList tech;
        tech.append(techLine(at, QStringLiteral("wildos-backup"),
                             QStringLiteral("%1 backup started, to %2").arg(o.value(QStringLiteral("kind")).toString(QStringLiteral("scheduled")),
                                                                            drive.isEmpty() ? QStringLiteral("the backup drive") : drive)));
        if (result == QLatin1String("done")) {
            tech.append(techLine(finished, QStringLiteral("wildos-backup"),
                                 QStringLiteral("%1 new, %2 changed, %3 bytes added, snapshot %4")
                                     .arg(o.value(QStringLiteral("filesNew")).toInt())
                                     .arg(o.value(QStringLiteral("filesChanged")).toInt())
                                     .arg(qint64(o.value(QStringLiteral("dataAdded")).toDouble()))
                                     .arg(o.value(QStringLiteral("snapshot")).toString().left(8))));
        }
        if (!o.value(QStringLiteral("reason")).toString().isEmpty()) {
            tech.append(techLine(finished, QStringLiteral("wildos-backup"), o.value(QStringLiteral("reason")).toString()));
        }
        if (!o.value(QStringLiteral("warning")).toString().isEmpty()) {
            tech.append(techLine(finished, QStringLiteral("wildos-backup"), o.value(QStringLiteral("warning")).toString()));
        }
        tech.append(techLine(finished, QStringLiteral("wildos-backup"), QStringLiteral("finished: ") + result));
        QVariantMap data{{QStringLiteral("result"), result},
                         {QStringLiteral("drive"), drive},
                         {QStringLiteral("files"), files},
                         {QStringLiteral("reason"), o.value(QStringLiteral("reason")).toString()},
                         {QStringLiteral("warning"), o.value(QStringLiteral("warning")).toString()},
                         {QStringLiteral("how"), o.value(QStringLiteral("kind")).toString()}};
        into.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("backup-") + o.value(QStringLiteral("at")).toString()},
                                {QStringLiteral("when"), finished},
                                {QStringLiteral("kind"), QStringLiteral("backup")},
                                {QStringLiteral("data"), data},
                                {QStringLiteral("tech"), tech}});
    }
}

void History::load()
{
    QVariantList list;
    bool systemReadable = true;
    bool ownReadable = true;
    readFile(dir + QStringLiteral("/system.jsonl"), list, &systemReadable);
    const QString own = dir + QStringLiteral("/user-%1.jsonl").arg(getuid());
    // An account with nothing of its own yet has no file.
    if (QFileInfo::exists(own)) {
        readFile(own, list, &ownReadable);
    }
    readBackups(list);
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("when")).toDateTime() > b.toMap().value(QStringLiteral("when")).toDateTime();
    });

    QFile statusFile(dir + QStringLiteral("/status.json"));
    QJsonObject status;
    if (statusFile.open(QIODevice::ReadOnly)) {
        status = QJsonDocument::fromJson(statusFile.readAll()).object();
    }
    QString problem;
    QString detail;
    const bool collectorMissing = !QFileInfo::exists(dir) && !QFileInfo::exists(socketPath);
    if (collectorMissing || (!systemReadable && QFileInfo::exists(dir + QStringLiteral("/system.jsonl")))) {
        problem = QStringLiteral("unreadable");
        detail = collectorMissing ? QStringLiteral("The part of Wild OS that keeps the history isn't installed.") : QStringLiteral("The history files can't be opened.");
    } else if (!ownReadable) {
        // What everyone on this PC sees is still there; this account's own
        // sign-ins and app crashes are not.
        problem = QStringLiteral("stale");
        detail = QStringLiteral("Your own sign-ins and apps couldn't be read just now.");
    } else if (!status.isEmpty() && !status.value(QStringLiteral("ok")).toBool(true)) {
        // settings-history names what it could not read, in words.
        problem = QStringLiteral("stale");
        QStringList why;
        for (const QJsonValue &v : status.value(QStringLiteral("problems")).toArray()) {
            why << v.toString();
        }
        QString list = why.size() > 1 ? why.mid(0, why.size() - 1).join(QStringLiteral(", ")) + QStringLiteral(" and ") + why.last() : why.value(0);
        if (!list.isEmpty()) {
            list[0] = list.at(0).toUpper();
        }
        detail = list.isEmpty() ? QString() : list + QStringLiteral(" couldn't be read just now.");
    } else if (m_reply.startsWith(QLatin1String("error"))) {
        problem = QStringLiteral("stale");
        detail = QStringLiteral("The part of Wild OS that keeps this list didn't answer.");
    }

    m_events = list;
    m_problem = problem;
    m_problemDetail = detail;
    Q_EMIT eventsChanged();
    watch();
}

void History::watch()
{
    if (QFileInfo::exists(dir) && !m_watcher.directories().contains(dir)) {
        m_watcher.addPath(dir);
    }
    const QString backups = backupHistory();
    if (QFileInfo::exists(backups) && !m_watcher.files().contains(backups)) {
        m_watcher.addPath(backups);
    }
}

bool History::saveTo(const QUrl &file, const QString &text)
{
    QSaveFile out(file.toLocalFile());
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    out.write(text.toUtf8());
    return out.commit();
}

QUrl History::defaultSaveFile() const
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(folder + QStringLiteral("/History %1.txt").arg(QDate::currentDate().toString(QStringLiteral("d MMMM yyyy"))));
}

bool History::openReport() const
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("wildos-report"));
    return !program.isEmpty() && QProcess::startDetached(program, {});
}

bool History::report(const QVariantMap &event, const QString &text)
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("wildos-report"));
    if (program.isEmpty()) {
        return false;
    }
    // Handed over in this account's own runtime folder, which nobody else
    // can open; Report a problem deletes it once read.
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/wildos-history");
    QDir().mkpath(folder);
    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QVariantMap data = event.value(QStringLiteral("data")).toMap();
    const QString kind = event.value(QStringLiteral("kind")).toString();
    QJsonObject note{{QStringLiteral("title"), event.value(QStringLiteral("title")).toString()},
                     {QStringLiteral("when"), event.value(QStringLiteral("when")).toDateTime().toSecsSinceEpoch()},
                     {QStringLiteral("text"), text},
                     {QStringLiteral("kind"), kind},
                     {QStringLiteral("program"), data.value(QStringLiteral("program")).toString()}};
    if (kind == QLatin1String("crash")) {
        note.insert(QStringLiteral("crash"), true);
        note.insert(QStringLiteral("app"), data.value(QStringLiteral("app")).toString());
    }
    const QString path = folder + QStringLiteral("/report-%1.json").arg(event.value(QStringLiteral("id")).toString().left(32).remove(QLatin1Char('/')));
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    out.write(QJsonDocument(note).toJson(QJsonDocument::Compact));
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!out.commit()) {
        return false;
    }
    return QProcess::startDetached(program, {QStringLiteral("--event"), path});
}
