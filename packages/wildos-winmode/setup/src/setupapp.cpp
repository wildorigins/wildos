#include "setupapp.h"
#include "licence.h"
#include "options.h"
#include "pccheck.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>

namespace
{
// The per-user agent of Windows mode (wildos-winmode's agent.py), which
// passes setup on to the system service.
const QString agentName = QStringLiteral("org.wildos.WindowsMode1");
const QString agentPath = QStringLiteral("/org/wildos/WindowsMode1");
}

SetupApp::SetupApp(QObject *parent)
    : QObject(parent)
{
    const Options saved = Options::load();
    m_office = saved.office;
    m_officePlan = saved.officePlan;
    m_openFiles = saved.openFiles;
    m_whenIdle = saved.whenIdle;
    m_memoryGb = saved.memoryGb;
    m_folders = saved.folders;
}

void SetupApp::begin(const QString &use, const QString &board)
{
    if (use == QLatin1String("preview")) {
        m_preview = board.isEmpty() ? QStringLiteral("check") : board;
        loadPreview();
        chooseDefaults();
        Q_EMIT checksChanged();
        Q_EMIT statusChanged();
        return;
    }
    // For a test PC that isn't the boards' PC: their made-up checks, with
    // setup itself as real as ever.
    const QString canned = qEnvironmentVariable("WILDOS_WINMODE_PREVIEW");
    if (!canned.isEmpty()) {
        m_preview = canned;
        loadPreview();
        m_preview.clear();
        m_status.clear();
        m_canned = true;
        chooseDefaults();
        Q_EMIT checksChanged();
    }
    if (use == QLatin1String("setup")) {
        m_daemon = new QDBusInterface(agentName, agentPath, agentName, QDBusConnection::sessionBus(), this);
        QDBusConnection::sessionBus().connect(agentName, agentPath,
                                              QStringLiteral("org.wildos.WindowsMode1"), QStringLiteral("Changed"), this, SLOT(refreshStatus()));
        refreshStatus();
    }
    if (!m_canned) {
        recheck();
    }
}

void SetupApp::loadPreview()
{
    // Made-up values for design reviews (--preview), as on the boards.
    m_virt = QStringLiteral("ok");
    m_technology = QStringLiteral("Intel VT-x");
    m_cores = 8;
    m_windowsFound = true;
    m_memory = qint64(16) * 1024 * 1024 * 1024;
    m_memoryLabel = QStringLiteral("16 GB");
    m_free = qint64(412) * 1000 * 1000 * 1000;
    m_freeLabel = QStringLiteral("412 GB");
    m_internet = QStringLiteral("connected");
    m_licence = m_preview == QLatin1String("nolicence") ? QStringLiteral("none") : QStringLiteral("firmware");
    m_edition = m_preview == QLatin1String("home") ? QStringLiteral("home") : QStringLiteral("pro");
    m_downloadsLabel = QStringLiteral("9.4 GB");
    m_binLabel = QStringLiteral("3.1 GB");
    m_canFirmware = true;
    if (m_preview == QLatin1String("problems")) {
        m_virt = QStringLiteral("off");
        m_free = qint64(18) * 1000 * 1000 * 1000;
        m_freeLabel = QStringLiteral("18 GB");
    }
    const auto stage = [](const QString &id, const QString &title, const QString &detail, const QString &state, int percent) {
        return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("title"), title}, {QStringLiteral("detail"), detail},
                           {QStringLiteral("state"), state}, {QStringLiteral("percent"), percent}};
    };
    if (m_preview == QLatin1String("setup") || m_preview == QLatin1String("download")) {
        const bool paused = m_preview == QLatin1String("download");
        m_status = QVariantMap{
            {QStringLiteral("state"), QStringLiteral("setup")},
            {QStringLiteral("activeStage"), paused ? QStringLiteral("media") : QStringLiteral("install")},
            {QStringLiteral("percent"), paused ? 15 : 46},
            {QStringLiteral("left"), QStringLiteral("About 14 minutes left")},
            {QStringLiteral("paused"), paused},
            {QStringLiteral("problem"), paused ? QVariantMap{{QStringLiteral("kind"), QStringLiteral("download")}, {QStringLiteral("offline"), true}, {QStringLiteral("received"), 3.8e9}} : QVariantMap()},
            {QStringLiteral("stages"), QVariantList{
                 stage(QStringLiteral("media"), QStringLiteral("Download Windows 11 from Microsoft"), QStringLiteral("6.1 GB, checked against Microsoft's signature"),
                       paused ? QStringLiteral("active") : QStringLiteral("done"), paused ? 62 : 100),
                 stage(QStringLiteral("install"), QStringLiteral("Install Windows"), QStringLiteral("Automatic, no questions to answer"),
                       paused ? QStringLiteral("pending") : QStringLiteral("active"), 61),
                 stage(QStringLiteral("activate"), QStringLiteral("Activate with this PC's licence"), QStringLiteral("Windows 11 Pro"), QStringLiteral("pending"), 0),
                 stage(QStringLiteral("tune"), QStringLiteral("Tune for speed and privacy"), QStringLiteral("Removes adverts, tracking and apps you won't use"),
                       QStringLiteral("pending"), 0),
                 stage(QStringLiteral("office"), QStringLiteral("Install Microsoft 365"), QStringLiteral("Word, Excel, PowerPoint, Outlook, OneNote"),
                       QStringLiteral("pending"), 0),
             }},
        };
    } else if (m_preview == QLatin1String("ready")) {
        const auto app = [](const QString &id, const QString &name) {
            return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("icon"), QStringLiteral("wildos-winmode-") + id}};
        };
        m_status = QVariantMap{
            {QStringLiteral("state"), QStringLiteral("ready")},
            {QStringLiteral("vm"), QStringLiteral("paused")},
            {QStringLiteral("licence"), QVariantMap{{QStringLiteral("source"), QStringLiteral("firmware")}, {QStringLiteral("edition"), QStringLiteral("pro")},
                                                    {QStringLiteral("activated"), true}}},
            {QStringLiteral("options"), QVariantMap{{QStringLiteral("office"), true}, {QStringLiteral("whenIdle"), QStringLiteral("automatic")}}},
            {QStringLiteral("apps"), QVariantList{app(QStringLiteral("word"), QStringLiteral("Word")), app(QStringLiteral("excel"), QStringLiteral("Excel")),
                                                  app(QStringLiteral("powerpoint"), QStringLiteral("PowerPoint")), app(QStringLiteral("outlook"), QStringLiteral("Outlook")),
                                                  app(QStringLiteral("onenote"), QStringLiteral("OneNote"))}},
        };
    }
}

void SetupApp::refreshStatus()
{
    if (!m_daemon) {
        return;
    }
    auto *watcher = new QDBusPendingCallWatcher(m_daemon->asyncCall(QStringLiteral("Status")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        m_status = reply.isError() ? QVariantMap() : QJsonDocument::fromJson(reply.value().toUtf8()).object().toVariantMap();
        Q_EMIT statusChanged();
    });
}

QString SetupApp::daemonState() const
{
    return m_status.value(QStringLiteral("state")).toString();
}

void SetupApp::finishOne()
{
    if (m_pending > 0) {
        --m_pending;
    }
    if (m_pending == 0) {
        chooseDefaults();
    }
    Q_EMIT checksChanged();
}

// What 9.4 starts on once the PC is known: the built-in licence when it can
// activate Windows mode, else activate later rather than a key field that
// holds everything up; and the most memory this PC can spare, unless a size
// chosen before still fits.
void SetupApp::chooseDefaults()
{
    if (m_licenceChoice != QLatin1String("key")) {
        m_licenceChoice = firmwareUsable() ? QStringLiteral("firmware") : QStringLiteral("none");
    }
    const QVariantList choices = memoryChoices();
    bool fits = false;
    for (const QVariant &c : choices) {
        fits = fits || c.toMap().value(QStringLiteral("value")).toInt() == m_memoryGb;
    }
    if (!fits || !QFile::exists(Paths::configFile())) {
        m_memoryGb = choices.isEmpty() ? 4 : choices.last().toMap().value(QStringLiteral("value")).toInt();
    }
    Q_EMIT optionsChanged();
}

void SetupApp::recheck()
{
    if (preview()) {
        return;
    }
    m_pending = 3;
    const PcCheck::Virtualisation v = PcCheck::virtualisation();
    m_virt = v.state;
    m_technology = v.technology;
    m_cores = PcCheck::processorCores();
    m_memory = PcCheck::memoryBytes();
    m_memoryLabel = PcCheck::memoryLabel(m_memory);
    const double gb = double(m_memory) / (1024.0 * 1024 * 1024);
    m_memoryState = gb >= 7.2 ? QStringLiteral("ok") : gb >= 5.4 ? QStringLiteral("low") : QStringLiteral("short");
    QDBusInterface login(QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
                         QDBusConnection::systemBus());
    const QDBusReply<QString> can = login.call(QStringLiteral("CanRebootToFirmwareSetup"));
    m_canFirmware = can.isValid() && (can.value() == QLatin1String("yes") || can.value() == QLatin1String("challenge"));
    Q_EMIT checksChanged();
    checkSpace();
    checkInternet();
    checkLicence();
}

void SetupApp::checkSpace()
{
    // Windows mode keeps Windows in /var/lib/wildos/winmode, which may not
    // exist yet.
    m_free = PcCheck::freeBytes(QStringLiteral("/var/lib"));
    m_freeLabel = m_free >= 0 ? PcCheck::sizeLabel(m_free) : QString();
    // Sizing two folders can take a moment on a full disk; off the main
    // thread so the window stays responsive.
    auto *thread = QThread::create([this] {
        const qint64 downloads = PcCheck::folderBytes(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        const qint64 bin = PcCheck::folderBytes(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files"));
        QMetaObject::invokeMethod(this, [this, downloads, bin] {
            m_downloadsLabel = PcCheck::sizeLabel(downloads);
            m_binLabel = PcCheck::sizeLabel(bin);
            finishOne();
        });
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

void SetupApp::checkInternet()
{
    m_internet = QStringLiteral("checking");
    QDBusInterface nm(QStringLiteral("org.freedesktop.NetworkManager"), QStringLiteral("/org/freedesktop/NetworkManager"),
                      QStringLiteral("org.freedesktop.NetworkManager"), QDBusConnection::systemBus());
    if (nm.isValid()) {
        const uint connectivity = nm.property("Connectivity").toUInt();
        const uint metered = nm.property("Metered").toUInt();
        // 4 is full internet; 1 and 3 are metered, yes and guessed yes.
        m_internet = connectivity == 4 ? ((metered == 1 || metered == 3) ? QStringLiteral("metered") : QStringLiteral("connected")) : QStringLiteral("offline");
        finishOne();
        return;
    }
    // No NetworkManager to ask: try Microsoft itself.
    auto *socket = new QTcpSocket(this);
    auto *timer = new QTimer(socket);
    timer->setSingleShot(true);
    connect(socket, &QTcpSocket::connected, this, [this, socket] {
        if (socket->property("done").toBool()) {
            return;
        }
        socket->setProperty("done", true);
        m_internet = QStringLiteral("connected");
        socket->deleteLater();
        finishOne();
    });
    const auto offline = [this, socket] {
        if (socket->property("done").toBool()) {
            return;
        }
        socket->setProperty("done", true);
        m_internet = QStringLiteral("offline");
        socket->deleteLater();
        finishOne();
    };
    connect(socket, &QTcpSocket::errorOccurred, this, offline);
    connect(timer, &QTimer::timeout, this, offline);
    timer->start(4000);
    socket->connectToHost(QStringLiteral("www.microsoft.com"), 443);
}

void SetupApp::checkLicence()
{
    m_licence = QStringLiteral("checking");
    // For the rig and the tests: the answer from a file.
    const QString canned = qEnvironmentVariable("WILDOS_WINMODE_FIRMWARE");
    const auto apply = [this](const QByteArray &json, bool ok) {
        const QJsonObject fw = QJsonDocument::fromJson(json).object();
        if (!ok || fw.isEmpty() || fw.contains(QStringLiteral("error"))) {
            m_licence = QStringLiteral("error");
        } else {
            m_edition = Licence::editionFrom(fw.value(QStringLiteral("edition")).toString(), fw.value(QStringLiteral("description")).toString());
            m_windowsFound = fw.value(QStringLiteral("windows")).toBool();
            // One licence covers one copy of Windows; with Windows still on
            // this PC the built-in one is taken.
            m_licence = !fw.value(QStringLiteral("msdm")).toBool() ? QStringLiteral("none")
                : fw.value(QStringLiteral("windows")).toBool()     ? QStringLiteral("inuse")
                                                                   : QStringLiteral("firmware");
        }
        finishOne();
    };
    if (!canned.isEmpty()) {
        QFile file(canned);
        apply(file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(), file.isOpen());
        return;
    }
    // The system service reads the firmware's licence table and looks for
    // Windows on the disks, which can take a little while. First run asks
    // it directly (anyone may ask this one thing); the setup window through
    // the agent, like everything else it asks.
    if (m_daemon) {
        m_daemon->setTimeout(120000);
    }
    const QDBusPendingCall call = m_daemon ? m_daemon->asyncCall(QStringLiteral("Firmware"))
                                           : QDBusConnection::systemBus().asyncCall(
                                                 QDBusMessage::createMethodCall(agentName, agentPath, QStringLiteral("org.wildos.WindowsMode1.Host"),
                                                                                QStringLiteral("Firmware")),
                                                 120000);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, apply] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        apply(reply.isError() ? QByteArray() : reply.value().toUtf8(), !reply.isError());
    });
}

QString SetupApp::needLabel() const
{
    return PcCheck::sizeLabel(PcCheck::neededBytes(m_office));
}

bool SetupApp::spaceOk() const
{
    return m_free < 0 || m_free >= PcCheck::neededBytes(m_office);
}

int SetupApp::setupMinutes() const
{
    // media 15, install 25, activate 1, tune 10, office 15
    return 15 + 25 + 1 + 10 + (m_office ? 15 : 0);
}

bool SetupApp::coresOk() const
{
    return m_cores >= PcCheck::neededCores;
}

bool SetupApp::requirementsMet() const
{
    return !checking() && m_virt == QLatin1String("ok") && coresOk() && m_memoryState == QLatin1String("ok") && spaceOk()
        && m_internet != QLatin1String("offline");
}

bool SetupApp::firmwareUsable() const
{
    return (m_licence == QLatin1String("firmware") || m_licence == QLatin1String("inuse")) && m_edition != QLatin1String("home");
}

bool SetupApp::canStart() const
{
    return m_termsAccepted && spaceOk() && (m_licenceChoice != QLatin1String("key") || Licence::keyShapeValid(Licence::normaliseKey(m_productKey)));
}

// The sizes this PC can spare: up to two fifths of the memory fitted, so
// Wild OS and its apps keep the rest (6 GB of a 16 GB PC). 4 GB, the least
// Windows 11 runs in, is always offered.
QVariantList SetupApp::memoryChoices() const
{
    const double fitted = PcCheck::memoryLabel(m_memory).section(QLatin1Char(' '), 0, 0).toDouble();
    QVariantList choices;
    for (int gb : {4, 6, 8, 12, 16, 24, 32}) {
        if (gb == 4 || gb <= fitted * 0.4) {
            choices << QVariantMap{{QStringLiteral("value"), gb}, {QStringLiteral("text"), QStringLiteral("Up to %1 GB").arg(gb)}};
        }
    }
    return choices;
}

void SetupApp::setOffice(bool on)
{
    if (m_office != on) {
        m_office = on;
        Q_EMIT optionsChanged();
        Q_EMIT checksChanged();
    }
}

QStringList SetupApp::folders() const
{
    return m_folders;
}

void SetupApp::restartToFirmware()
{
    QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("reboot"), QStringLiteral("--firmware-setup")});
}

void SetupApp::openStorage()
{
    QProcess::startDetached(QStringLiteral("wildos-settings"), {QStringLiteral("--page"), QStringLiteral("storage")});
}

void SetupApp::openUrl(const QString &url)
{
    QDesktopServices::openUrl(QUrl(url));
}

bool SetupApp::keyShapeValid(const QString &typed) const
{
    return Licence::keyShapeValid(Licence::normaliseKey(typed));
}

QString SetupApp::normaliseKey(const QString &typed) const
{
    return Licence::normaliseKey(typed);
}

namespace
{
const QString cpuThreadsHelper = QStringLiteral("/usr/libexec/wildos/settings-cpu-threads");

QByteArray readSmt(const char *name)
{
    QFile file(QStringLiteral("/sys/devices/system/cpu/smt/") + QLatin1String(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

QByteArray readEtc(const char *name)
{
    QFile file(QStringLiteral("/etc/wildos/") + QLatin1String(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}
}

// Offered only on a processor with two threads per core whose second threads
// are off and not already chosen on, and not while extra processor protection
// (or Strict or Maximum, which include it) keeps them off. Most processors
// run every thread by default, so this rarely shows.
bool SetupApp::hyperThreadingAvailable() const
{
    // The boards' made-up PC has the row.
    if (preview() || m_canned) {
        return true;
    }
    const QByteArray control = readSmt("control");
    const QByteArray level = readEtc("protection-level");
    return QFile::exists(cpuThreadsHelper) && !control.isEmpty() && control != "notsupported" && control != "notimplemented"
        && readSmt("active") != "1" && readEtc("cpu-threads") != "on" && readEtc("cpu-protection") != "extra"
        && level != "strict" && level != "maximum";
}

bool SetupApp::hyperThreadingNeedsRestart() const
{
    return readEtc("cpu-threads") == "on" && readSmt("active") != "1";
}

QString SetupApp::setupRequest(bool background) const
{
    Options o = Options::load();
    o.office = m_office;
    o.officePlan = m_officePlan;
    o.openFiles = m_openFiles;
    o.whenIdle = m_whenIdle;
    o.memoryGb = m_memoryGb;
    // The firmware's key stays with the system service, which reads it
    // itself; only a key typed here travels with the request.
    QJsonObject lic{{QStringLiteral("source"), m_licenceChoice}, {QStringLiteral("edition"), m_edition}};
    if (m_licenceChoice == QLatin1String("key")) {
        lic.insert(QStringLiteral("key"), Licence::normaliseKey(m_productKey));
        lic.insert(QStringLiteral("edition"), QString());
    }
    QJsonObject options = o.toJson();
    options.insert(QStringLiteral("hyperThreading"), m_hyperThreading && hyperThreadingAvailable());
    QJsonObject request{{QStringLiteral("licence"), lic},
                        {QStringLiteral("options"), options},
                        // Windows' own licence screen is skipped, so the
                        // acceptance here goes with the setup it was for.
                        {QStringLiteral("terms"), QJsonObject{{QStringLiteral("windows"), m_termsAccepted},
                                                              {QStringLiteral("office"), m_termsAccepted && m_office}}},
                        {QStringLiteral("background"), background}};
    // The service checks the file again itself, as this account, and copies it.
    if (m_isoState == QLatin1String("ok")) {
        request.insert(QStringLiteral("iso"), m_isoPath);
    }
    return QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact));
}

QString SetupApp::startSetup(bool background)
{
    if (preview()) {
        return {};
    }
    // A boot option, so it needs the administrator; setup goes ahead either way.
    if (m_hyperThreading && hyperThreadingAvailable()) {
        auto process = new QProcess(this);
        connect(process, &QProcess::finished, this, [this, process] {
            process->deleteLater();
            Q_EMIT optionsChanged();
        });
        process->start(QStringLiteral("pkexec"), {cpuThreadsHelper, QStringLiteral("on")});
    }
    if (!m_daemon) {
        m_daemon = new QDBusInterface(agentName, agentPath, agentName, QDBusConnection::sessionBus(), this);
    }
    m_daemon->setTimeout(120000);
    const QDBusReply<QString> reply = m_daemon->call(QStringLiteral("StartSetup"), setupRequest(background));
    if (!reply.isValid()) {
        return QStringLiteral("Windows mode's background service didn't start. Restart the PC, then try again.");
    }
    refreshStatus();
    return reply.value();
}

void SetupApp::pause()
{
    if (m_daemon) {
        m_daemon->asyncCall(QStringLiteral("PauseSetup"));
    }
}

void SetupApp::resume()
{
    if (m_daemon) {
        m_daemon->asyncCall(QStringLiteral("ResumeSetup"));
    }
}

void SetupApp::cancel()
{
    if (m_daemon) {
        m_daemon->asyncCall(QStringLiteral("CancelSetup"));
    }
}

void SetupApp::openDownloadPage()
{
    if (m_daemon) {
        m_daemon->asyncCall(QStringLiteral("OpenDownloadPage"));
    }
}

QString SetupApp::chooseIso(const QString &url)
{
    if (!m_daemon) {
        return {};
    }
    const QDBusReply<QString> reply = m_daemon->call(QStringLiteral("ChooseIso"), QUrl(url).toLocalFile());
    return reply.isValid() ? reply.value() : QStringLiteral("Windows mode's background service didn't answer.");
}

QString SetupApp::isoFile() const
{
    return QFileInfo(m_isoPath).fileName();
}

QUrl SetupApp::downloadsFolder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
}

void SetupApp::checkIso(const QString &url)
{
    const QString path = QUrl(url).isLocalFile() ? QUrl(url).toLocalFile() : url;
    forgetIso();
    m_isoPath = path;
    m_isoState = QStringLiteral("checking");
    Q_EMIT isoChanged();
    if (preview()) {
        m_isoState = QStringLiteral("ok");
        Q_EMIT isoChanged();
        return;
    }
    // The same check the service runs again before copying the file, so the
    // two can't disagree about it.
    auto *process = new QProcess(this);
    m_isoCheck = process;
    const auto answer = [this, process](const QJsonObject &result) {
        process->deleteLater();
        // A check that was replaced or called off says nothing.
        if (m_isoCheck != process) {
            return;
        }
        m_isoCheck = nullptr;
        if (result.value(QStringLiteral("ok")).toBool()) {
            m_isoState = QStringLiteral("ok");
            Q_EMIT isoChanged();
            return;
        }
        const QString why = result.value(QStringLiteral("text")).toString();
        m_isoPath.clear();
        m_isoState.clear();
        Q_EMIT isoChanged();
        Q_EMIT isoRefused(why.isEmpty() ? QStringLiteral("This file couldn't be checked. Try again.") : why);
    };
    connect(process, &QProcess::finished, this, [process, answer](int code, QProcess::ExitStatus status) {
        const bool clean = status == QProcess::NormalExit && code == 0;
        answer(clean ? QJsonDocument::fromJson(process->readAllStandardOutput()).object() : QJsonObject());
    });
    connect(process, &QProcess::errorOccurred, this, [answer](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            answer(QJsonObject());
        }
    });
    process->start(QStringLiteral("wildos-winmode"), {QStringLiteral("check-iso"), path});
}

void SetupApp::forgetIso()
{
    if (QProcess *running = m_isoCheck) {
        m_isoCheck = nullptr;
        running->kill();
    }
    m_isoPath.clear();
    m_isoState.clear();
    Q_EMIT isoChanged();
}

void SetupApp::openApp(const QString &id)
{
    if (preview()) {
        return;
    }
    QProcess::startDetached(QStringLiteral("wildos-winmode"), {QStringLiteral("launch"), id});
}
