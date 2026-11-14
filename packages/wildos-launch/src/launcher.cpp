#include "launcher.h"
#include "feedback.h"
#include "preload.h"

#include <KConfigGroup>
#include <KDesktopFile>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QThreadPool>

namespace
{
// How long to wait for a window before deciding the app is not going to
// open one. Windows mode can take a while when Windows has to start first.
constexpr int giveUpMs = 20000;
constexpr int giveUpWindowsModeMs = 60000;
// When the launched process ends without a window, wait this long for a
// window from an instance it handed over to.
constexpr int handOverGraceMs = 1500;
// KWin finishes its feedback once a window of the app is activated; the
// window report may still be on its way, so look again after this long.
constexpr int feedbackSettleMs = 300;
// After a start, wait for the app to settle before noting which files it
// read, so the list covers what it needs to show its window.
constexpr int sampleAfterMs = 20000;

// Programs that only start the real one, so their names say nothing about
// the window that will appear.
const QSet<QString> wrappers = {
    QStringLiteral("env"),        QStringLiteral("sh"),        QStringLiteral("bash"),       QStringLiteral("flatpak"),
    QStringLiteral("python3"),    QStringLiteral("python"),    QStringLiteral("kioclient"),  QStringLiteral("kstart"),
    QStringLiteral("gtk-launch"), QStringLiteral("snap"),      QStringLiteral("java"),       QStringLiteral("wildos-winmode"),
};

// systemd unit names as KIO writes them for each app it starts.
QString escapeUnitName(const QString &input)
{
    QString res;
    for (const unsigned char c : input.toUtf8()) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ':' || c == '_' || c == '.') {
            res += QLatin1Char(c);
        } else {
            res += QStringLiteral("\\x%1").arg(c, 2, 16, QLatin1Char('0'));
        }
    }
    return res;
}

QStringList keysFrom(const QJsonValue &value)
{
    QStringList keys;
    for (const QJsonValue &key : value.toArray()) {
        if (const QString k = key.toString().toLower(); !k.isEmpty()) {
            keys << k;
        }
    }
    return keys;
}

// The systemd unit a process runs in, from the last part of its cgroup:
// app-org.wildos.files@1a2b.service, as KIO starts it.
QString unitOf(int pid)
{
    if (pid <= 0) {
        return {};
    }
    QFile file(QStringLiteral("/proc/%1/cgroup").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString line = QString::fromUtf8(file.readAll()).trimmed().section(QLatin1Char('\n'), -1);
    return line.section(QLatin1Char('/'), -1);
}

bool intersects(const QStringList &a, const QStringList &b)
{
    for (const QString &key : a) {
        if (b.contains(key)) {
            return true;
        }
    }
    return false;
}
}

Launcher::Launcher(QObject *parent)
    : QObject(parent)
{
    // Left by the placeholder windows this daemon used to open.
    QFile::remove(JsonFile::directory() + QStringLiteral("/geometry.json"));

    m_feedback = new ActivationFeedback();
    m_feedback->setParent(this);
    connect(m_feedback, &ActivationFeedback::activation, this, [this](Activation *activation) {
        connect(activation, &Activation::appId, this, [this, activation](const QString &appId) {
            activationStarted(activation, appId);
        });
        connect(activation, &Activation::finished, activation, &QObject::deleteLater);
    });
    connect(m_feedback, &QWaylandClientExtension::activeChanged, this, [this] {
        qInfo("wildos-launchd: launch feedback %s", m_feedback->isActive() ? "connected" : "not available");
    });

    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString systemd = QStringLiteral("org.freedesktop.systemd1");
    const QString manager = QStringLiteral("org.freedesktop.systemd1.Manager");
    bus.connect(systemd, QStringLiteral("/org/freedesktop/systemd1"), manager, QStringLiteral("UnitNew"), this, SLOT(unitNew(QString, QDBusObjectPath)));
    bus.connect(systemd, QStringLiteral("/org/freedesktop/systemd1"), manager, QStringLiteral("UnitRemoved"), this,
                SLOT(unitRemoved(QString, QDBusObjectPath)));
    bus.asyncCall(QDBusMessage::createMethodCall(systemd, QStringLiteral("/org/freedesktop/systemd1"), manager, QStringLiteral("Subscribe")));
}

Launcher::~Launcher()
{
    m_usage.flush();
}

bool Launcher::anyOpen(const QStringList &keys) const
{
    for (const QString &key : keys) {
        if (m_open.value(key) > 0) {
            return true;
        }
    }
    return false;
}

void Launcher::activationStarted(Activation *activation, const QString &appId)
{
    QString desktopId = appId;
    if (desktopId.endsWith(QLatin1String(".desktop"))) {
        desktopId.chop(8);
    }
    if (desktopId.isEmpty()) {
        return;
    }
    const QString path = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId + QStringLiteral(".desktop"));
    if (path.isEmpty()) {
        return;
    }
    const KDesktopFile desktop(path);
    const KConfigGroup group = desktop.desktopGroup();

    Launch launch;
    launch.desktopId = desktopId;
    launch.windowsMode = group.readEntry("X-WildOS-Engine") == QLatin1String("windows-mode");
    launch.keys << desktopId.toLower();
    if (const QString wmClass = group.readEntry("StartupWMClass").toLower(); !wmClass.isEmpty()) {
        launch.keys << wmClass;
    }
    const QStringList exec = QProcess::splitCommand(group.readEntry("Exec"));
    if (!exec.isEmpty()) {
        const QString program = QFileInfo(exec.first()).fileName().toLower();
        if (!program.isEmpty() && !wrappers.contains(program) && !launch.keys.contains(program)) {
            launch.keys << program;
        }
    }

    m_usage.countLaunch(desktopId);
    Q_EMIT LaunchStarted(desktopId);

    // A window of it is open already: a single-instance app raising its
    // window, or opening another from a running process, which is quick.
    if (anyOpen(launch.keys)) {
        return;
    }
    for (const Launch &other : std::as_const(m_launches)) {
        if (other.desktopId == desktopId) {
            return;
        }
    }

    launch.id = QStringLiteral("l%1").arg(++m_next);
    launch.clock.start();
    const QString id = launch.id;
    launch.giveUpTimer = new QTimer(this);
    launch.giveUpTimer->setSingleShot(true);
    launch.giveUpTimer->setInterval(launch.windowsMode ? giveUpWindowsModeMs : giveUpMs);
    connect(launch.giveUpTimer, &QTimer::timeout, this, [this, id] {
        qInfo("wildos-launchd: %s opened no window", qPrintable(m_launches.value(id).desktopId));
        finish(id);
    });
    launch.giveUpTimer->start();
    m_launches.insert(id, launch);
    Q_EMIT LaunchWaiting(desktopId);

    connect(activation, &Activation::finished, this, [this, id] {
        QTimer::singleShot(feedbackSettleMs, this, [this, id] {
            const auto it = m_launches.constFind(id);
            if (it != m_launches.constEnd() && anyOpen(it->keys)) {
                finish(id);
            }
        });
    });
}

void Launcher::finish(const QString &id)
{
    auto it = m_launches.find(id);
    if (it == m_launches.end()) {
        return;
    }
    const QString desktopId = it->desktopId;
    delete it->giveUpTimer;
    m_launches.erase(it);
    Q_EMIT LaunchEnded(desktopId);
}

void Launcher::sampleLater(const QString &desktopId, int pid)
{
    if (pid <= 0 || m_sampled.contains(desktopId)) {
        return;
    }
    m_sampled.insert(desktopId);
    QTimer::singleShot(sampleAfterMs, this, [this, desktopId, pid] {
        QPointer<Launcher> self(this);
        QThreadPool::globalInstance()->start([self, desktopId, pid] {
            const QJsonArray files = Preload::sample({pid});
            QMetaObject::invokeMethod(qApp, [self, desktopId, files] {
                if (self && !files.isEmpty()) {
                    self->m_usage.setFiles(desktopId, files);
                }
            });
        });
    });
}

void Launcher::Windows(const QString &json)
{
    const QJsonObject report = QJsonDocument::fromJson(json.toUtf8()).object();
    m_open.clear();
    for (const QJsonValue &window : report.value(QStringLiteral("open")).toArray()) {
        for (const QString &key : keysFrom(window.toObject().value(QStringLiteral("k")))) {
            m_open[key] += 1;
        }
    }
    const QJsonObject added = report.value(QStringLiteral("added")).toObject();
    if (added.isEmpty()) {
        return;
    }
    const QStringList keys = keysFrom(added.value(QStringLiteral("k")));
    // An app can name its window differently from its desktop file; the
    // process it was started in still says which launch it belongs to.
    const QString unit = unitOf(added.value(QStringLiteral("pid")).toInt());
    const QStringList ids = m_launches.keys();
    for (const QString &id : ids) {
        const Launch &launch = m_launches[id];
        const QString prefix = QStringLiteral("app-") + escapeUnitName(launch.desktopId);
        const bool sameUnit = !unit.isEmpty()
            && (unit == launch.unit || unit.startsWith(prefix + QLatin1Char('@'))
                || (unit.startsWith(prefix + QLatin1Char('-')) && unit.endsWith(QLatin1String(".scope"))));
        if (!sameUnit && !intersects(keys, launch.keys)) {
            continue;
        }
        qInfo("wildos-launchd: %s opened in %lld ms", qPrintable(launch.desktopId), launch.clock.elapsed());
        // A Windows mode window belongs to the RemoteApp client, whose
        // files say nothing about the app inside Windows.
        if (!launch.windowsMode) {
            sampleLater(launch.desktopId, added.value(QStringLiteral("pid")).toInt());
        }
        finish(id);
    }
}

QString Launcher::State() const
{
    QJsonArray launches;
    for (const Launch &launch : m_launches) {
        launches.append(QJsonObject{
            {QStringLiteral("id"), launch.id},
            {QStringLiteral("desktopId"), launch.desktopId},
            {QStringLiteral("keys"), QJsonArray::fromStringList(launch.keys)},
            {QStringLiteral("windowsMode"), launch.windowsMode},
            {QStringLiteral("unit"), launch.unit},
            {QStringLiteral("ms"), launch.clock.elapsed()},
        });
    }
    QJsonObject open;
    for (auto it = m_open.constBegin(); it != m_open.constEnd(); ++it) {
        open.insert(it.key(), it.value());
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
                                               {QStringLiteral("feedback"), m_feedback->isActive()},
                                               {QStringLiteral("launches"), launches},
                                               {QStringLiteral("open"), open},
                                           })
                                 .toJson(QJsonDocument::Compact));
}

void Launcher::unitNew(const QString &name, const QDBusObjectPath &)
{
    for (Launch &launch : m_launches) {
        if (!launch.unit.isEmpty()) {
            continue;
        }
        const QString prefix = QStringLiteral("app-") + escapeUnitName(launch.desktopId);
        if (name.startsWith(prefix + QLatin1Char('@')) || (name.startsWith(prefix + QLatin1Char('-')) && name.endsWith(QLatin1String(".scope")))) {
            launch.unit = name;
        }
    }
}

void Launcher::unitRemoved(const QString &name, const QDBusObjectPath &)
{
    for (const Launch &launch : std::as_const(m_launches)) {
        // A Windows mode launcher hands the app to the agent and exits at
        // once; its window comes from the agent's RemoteApp connection.
        if (launch.unit != name || launch.windowsMode) {
            continue;
        }
        const QString id = launch.id;
        QTimer::singleShot(handOverGraceMs, this, [this, id] {
            if (m_launches.contains(id)) {
                qInfo("wildos-launchd: %s ended without a window", qPrintable(m_launches.value(id).desktopId));
                finish(id);
            }
        });
    }
}
