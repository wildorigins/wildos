#include "crashnotify.h"
#include "parts.h"

#include <KNotification>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace
{
QHash<QString, QString> readNote(const QString &path)
{
    QHash<QString, QString> note;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) {
                note.insert(line.left(eq), line.mid(eq + 1));
            }
        }
    }
    return note;
}

// The name people know a program by, from its launcher. Programs without
// one (helpers, services) get no notice: there would be nothing to say
// they recognise.
QString appName(const QString &exe, const QString &comm)
{
    const QString name = QFileInfo(exe).fileName();
    if (name == QLatin1String("plasmashell") || comm == QLatin1String("plasmashell")) {
        return QCoreApplication::translate("CrashNotify", "The desktop");
    }
    if (name == QLatin1String("kwin_wayland")) {
        return QCoreApplication::translate("CrashNotify", "The window manager");
    }
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("applications"), QStandardPaths::LocateDirectory);
    for (const QString &dir : dirs) {
        for (const QString &file : QDir(dir).entryList({QStringLiteral("*.desktop")}, QDir::Files)) {
            QFile desktop(QDir(dir).filePath(file));
            if (!desktop.open(QIODevice::ReadOnly)) {
                continue;
            }
            QString title;
            QString program;
            bool hidden = false;
            bool entry = false;
            for (const QString &line : QString::fromUtf8(desktop.readAll()).split(QLatin1Char('\n'))) {
                if (line.startsWith(QLatin1Char('['))) {
                    entry = line.trimmed() == QLatin1String("[Desktop Entry]");
                } else if (entry && line.startsWith(QLatin1String("Name="))) {
                    title = line.mid(5).trimmed();
                } else if (entry && line.startsWith(QLatin1String("Exec="))) {
                    program = QFileInfo(line.mid(5).trimmed().section(QLatin1Char(' '), 0, 0)).fileName();
                } else if (entry && line.trimmed() == QLatin1String("NoDisplay=true")) {
                    hidden = true;
                }
            }
            if (!hidden && !title.isEmpty() && !program.isEmpty() && (program == name || program == comm)) {
                return title;
            }
        }
    }
    return {};
}
}

CrashNotify::CrashNotify(QObject *parent)
    : QObject(parent)
{
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    m_dir = runtime + QStringLiteral("/wildos-report/crashes");
    m_seen = runtime + QStringLiteral("/wildos-report/seen");
    m_quit.setSingleShot(true);
    m_quit.setInterval(3000);
    connect(&m_quit, &QTimer::timeout, this, &CrashNotify::idle);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &CrashNotify::read);
}

void CrashNotify::start()
{
    QDir().mkpath(m_dir);
    QDir().mkpath(m_seen);
    m_watcher.addPath(m_dir);
    read();
    // An unanswered notice stays in the notification list, but this
    // doesn't wait for ever behind it.
    QTimer::singleShot(30 * 60 * 1000, qApp, &QCoreApplication::quit);
}

void CrashNotify::read()
{
    const QDir dir(m_dir);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos-report");
    QDir().mkpath(cache);
    QSettings offered(cache + QStringLiteral("/offered"), QSettings::IniFormat);
    const QString today = QDate::currentDate().toString(Qt::ISODate);

    for (const QString &name : dir.entryList({QStringLiteral("*.crash")}, QDir::Files, QDir::Name)) {
        const QString path = dir.filePath(name);
        const auto note = readNote(path);
        // Kept until sign-out, so Report a problem can still say what
        // crashed when the system log is out of reach.
        QFile::remove(QDir(m_seen).filePath(name));
        QFile::rename(path, QDir(m_seen).filePath(name));

        const QString exe = note.value(QStringLiteral("exe"));
        const QString comm = note.value(QStringLiteral("comm"));
        const QString app = appName(exe, comm);
        if (app.isEmpty()) {
            continue;
        }
        const QString key = QFileInfo(exe).fileName().isEmpty() ? comm : QFileInfo(exe).fileName();
        if (offered.value(key).toString() == today) {
            continue;
        }
        offered.setValue(key, today);
        offered.sync();
        notify(app, Parts::byProgram(key).id, note.value(QStringLiteral("time")));
    }
    // Anything else that landed here (not a note) would keep the path unit
    // firing.
    for (const QString &name : dir.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot)) {
        if (!name.endsWith(QLatin1String(".crash"))) {
            QFile::remove(dir.filePath(name));
        }
    }
    if (m_open == 0) {
        m_quit.start();
    }
}

void CrashNotify::notify(const QString &app, const QString &part, const QString &time)
{
    auto *notification = new KNotification(QStringLiteral("crashed"));
    notification->setComponentName(QStringLiteral("wildos-report"));
    notification->setTitle(tr("%1 closed unexpectedly").arg(app));
    notification->setText(tr("Want to tell us what happened? You'll see everything before it's shared."));
    notification->setIconName(QStringLiteral("wildos-report"));
    notification->setHint(QStringLiteral("desktop-entry"), QStringLiteral("org.wildos.report"));
    // Kept in the notification list after the popup goes, with its buttons
    // still working while this waits for them.
    notification->setHint(QStringLiteral("resident"), true);
    auto *later = notification->addAction(tr("Not now"));
    connect(later, &KNotificationAction::activated, notification, &KNotification::close);
    auto *report = notification->addAction(tr("Report a problem"));
    connect(report, &KNotificationAction::activated, this, [notification, app, part, time]() {
        // In a unit of its own: this service's processes all end when it
        // does, and the window must outlive the notice. It also gets the
        // session's environment rather than this one's offscreen platform.
        QProcess::startDetached(QStringLiteral("systemd-run"),
                                {QStringLiteral("--user"), QStringLiteral("--quiet"), QStringLiteral("--collect"), QStringLiteral("--"),
                                 QStringLiteral("wildos-report"), QStringLiteral("--part"), part, QStringLiteral("--crash"), time,
                                 QStringLiteral("--app"), app});
        notification->close();
    });
    ++m_open;
    m_quit.stop();
    connect(notification, &KNotification::closed, this, [this]() {
        if (--m_open <= 0) {
            m_quit.start();
        }
    });
    notification->sendEvent();
}

void CrashNotify::idle()
{
    if (m_open <= 0 && QDir(m_dir).entryList({QStringLiteral("*.crash")}, QDir::Files).isEmpty()) {
        QCoreApplication::quit();
    }
}
