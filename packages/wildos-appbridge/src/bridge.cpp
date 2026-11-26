#include "bridge.h"

#include <KWindowEffects>
#include <KWindowSystem>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPainterPath>
#include <QQuickWindow>
#include <QRegion>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <algorithm>
#include <limits>
#include <memory>

namespace
{
const QString catalogueFile = QStringLiteral("/usr/share/wildos-appbridge/alternatives.json");

// "InvoiceMaker-Setup-2.4.1-x64.exe" to "Invoice Maker".
QString nameFromFile(const QString &fileName)
{
    QString name = QFileInfo(fileName).completeBaseName();
    name.replace(QRegularExpression(QStringLiteral("[_\\-.]+")), QStringLiteral(" "));
    name.remove(QRegularExpression(QStringLiteral("\\b(v?\\d+(\\s\\d+)*|setup|installer|install|x64|x86|win64|win32|amd64|arm64|64bit|32bit|"
                                                  "full|portable|release|final|web|offline|online)\\b"),
                                   QRegularExpression::CaseInsensitiveOption));
    name = name.simplified();
    if (!name.contains(QLatin1Char(' '))) {
        name.replace(QRegularExpression(QStringLiteral("([a-z])([A-Z])")), QStringLiteral("\\1 \\2"));
    }
    return name.isEmpty() ? QFileInfo(fileName).completeBaseName() : name;
}

QString cleanProductName(QString name)
{
    name.remove(QRegularExpression(QStringLiteral("\\b(setup|installer|install|web installer)\\b"), QRegularExpression::CaseInsensitiveOption));
    return name.simplified();
}

QRegion roundedRegion(const QRect &rect, int radius)
{
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    return QRegion(path.toFillPolygon().toPolygon());
}
}

QString Bridge::s_path;

void Bridge::setFile(const QString &path)
{
    s_path = path;
}

Bridge::Bridge(QObject *parent)
    : QObject(parent)
    , m_path(QFileInfo(s_path).absoluteFilePath())
{
    m_info = PeInfo::read(m_path);
    readCatalogue();
    readDecision();

    if (!m_catalogueName.isEmpty()) {
        m_appName = m_catalogueName;
    } else if (!cleanProductName(m_info.productName).isEmpty()) {
        m_appName = cleanProductName(m_info.productName);
    } else if (!cleanProductName(m_info.fileDescription).isEmpty() && m_info.fileDescription.size() < 40) {
        m_appName = cleanProductName(m_info.fileDescription);
    } else {
        m_appName = nameFromFile(fileName());
    }
    m_id = WinApps::makeId(m_appName);

    if (!m_info.icon.isNull()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        const QString icon = dir + QStringLiteral("/wildos-appbridge-%1.png").arg(QCoreApplication::applicationPid());
        if (m_info.icon.save(icon, "PNG")) {
            m_iconUrl = QUrl::fromLocalFile(icon).toString();
            connect(qApp, &QCoreApplication::aboutToQuit, this, [icon] {
                QFile::remove(icon);
            });
        }
    }

    m_permissions = WinApps::startingPermissions();
    if (!WinApps::supportEnabled()) {
        m_state = QStringLiteral("disabled");
    } else if (m_info.kind == PeInfo::NotWindows) {
        m_state = QStringLiteral("failed");
        m_message = tr("This file isn't a Windows program, or it's damaged. Check where it came from and download it again.");
    }
    checkSignature();
}

QString Bridge::windowsModeReason() const
{
    if (m_windowsAction == QLatin1String("windows-mode")) {
        return {};
    }
    if (m_windowsAction == QLatin1String("home")) {
        return tr("This PC has Windows 11 Home, which can't run Windows apps on Wild OS yet. "
                  "Microsoft's upgrade to Windows 11 Pro uses this PC's licence, or use the Wild OS version or Office on the web.");
    }
    if (m_windowsAction == QLatin1String("cannot") && !m_windowsReason.isEmpty()) {
        return m_windowsReason;
    }
    if (QStandardPaths::findExecutable(QStringLiteral("wildos-winmode")).isEmpty()) {
        return tr("Windows mode isn't installed on this PC.");
    }
    return tr("Set up Windows mode first, in Settings under Windows apps.");
}

void Bridge::readDecision()
{
    // wild-run is the one place that decides where a Windows program runs;
    // the catalogue read above only fills in when it is not there.
    const QString router = QStandardPaths::findExecutable(QStringLiteral("wild-run"));
    if (router.isEmpty()) {
        m_action = m_windowsAction = QStringLiteral("setup");
        return;
    }
    QProcess run;
    run.start(router, {QStringLiteral("--json"), m_path});
    if (!run.waitForFinished(30000) || run.exitCode() != 0) {
        run.kill();
        // A slow PC can keep wild-run past the limit. With Windows mode
        // installed that says nothing about whether it is set up, and
        // Windows mode answers for itself when the program is handed over.
        const bool installed = !QStandardPaths::findExecutable(QStringLiteral("wildos-winmode")).isEmpty();
        m_action = m_windowsAction = installed ? QStringLiteral("windows-mode") : QStringLiteral("setup");
        return;
    }
    const QJsonObject d = QJsonDocument::fromJson(run.readAllStandardOutput()).object();
    m_action = d.value(QStringLiteral("action")).toString();
    m_windowsAction = d.value(QStringLiteral("windows")).toString();
    if (m_windowsAction.isEmpty()) {
        m_windowsAction = m_action;
    }
    if (m_windowsAction == QLatin1String("cannot")) {
        m_windowsReason = d.value(QStringLiteral("reason")).toString();
    }
    const QString name = d.value(QStringLiteral("name")).toString();
    if (!d.value(QStringLiteral("entry")).toString().isEmpty() && !name.isEmpty()) {
        m_catalogueName = name;
    }
    const QJsonObject alternative = d.value(QStringLiteral("alternative")).toObject();
    if (!alternative.isEmpty()) {
        m_storeId = alternative.value(QStringLiteral("storeId")).toString();
        m_webApp = alternative.value(QStringLiteral("webApp")).toString();
        m_nativeText = alternative.value(QStringLiteral("text")).toString();
    }
    m_callApp = d.value(QStringLiteral("calls")).toBool();
    if (m_action == QLatin1String("unsupported") || m_action == QLatin1String("home") || m_action == QLatin1String("cannot")) {
        m_compat = QStringLiteral("no");
        m_compatReason = m_action == QLatin1String("unsupported") ? d.value(QStringLiteral("reason")).toString() : windowsModeReason();
    } else if (m_action == QLatin1String("windows-mode") || m_action == QLatin1String("setup")) {
        m_compat = d.value(QStringLiteral("entry")).toString().isEmpty() ? QStringLiteral("unknown") : QStringLiteral("well");
        m_compatReason.clear();
    } else {
        m_compat = QStringLiteral("unknown");
        m_compatReason.clear();
    }
}

void Bridge::installNative()
{
    if (m_webApp.isEmpty()) {
        openStore();
        return;
    }
    // A window of its own in the browser Wild OS ships, with a Start entry
    // that groups on the taskbar like any app.
    const QString id = WinApps::makeId(m_appName);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
    QDir().mkpath(dir);
    QFile entry(dir + QStringLiteral("/wildos-webapp-%1.desktop").arg(id));
    if (entry.open(QIODevice::WriteOnly)) {
        const QStringList lines{
            QStringLiteral("[Desktop Entry]"),
            QStringLiteral("Type=Application"),
            QStringLiteral("Name=") + m_appName,
            QStringLiteral("Exec=brave-browser --app=%1 --class=wildos-webapp-%2").arg(m_webApp, id),
            QStringLiteral("Icon=web-browser"),
            QStringLiteral("StartupWMClass=wildos-webapp-") + id,
            QStringLiteral("Categories=Network;VideoConference;"),
        };
        entry.write(lines.join(QLatin1Char('\n')).toUtf8() + '\n');
        entry.close();
    }
    QProcess::startDetached(QStringLiteral("brave-browser"), {QStringLiteral("--app=") + m_webApp, QStringLiteral("--class=wildos-webapp-") + id});
    close();
}

QString Bridge::installInWindowsMode()
{
    if (!windowsModeReady()) {
        return windowsModeReason();
    }
    // A second press while Windows is still waking would start it twice.
    if (m_handingOver) {
        return {};
    }
    m_handingOver = true;
    // The agent copies the installer into the shared Downloads folder if
    // Windows can't already see it, and runs it there. It answers once the
    // installer is starting, which from a saved or stopped Windows takes a
    // while, so the call has no time limit of its own.
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.WindowsMode1"), QStringLiteral("/org/wildos/WindowsMode1"),
                                                      QStringLiteral("org.wildos.WindowsMode1"), QStringLiteral("RunInstaller"));
    call.setArguments({m_path});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, std::numeric_limits<int>::max()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        m_handingOver = false;
        const QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT windowsModeAnswered(tr("Windows mode didn't answer. Restart the PC, then try again."));
            return;
        }
        if (reply.value().isEmpty()) {
            close();
            return;
        }
        Q_EMIT windowsModeAnswered(reply.value());
    });
    return {};
}

QString Bridge::fileName() const
{
    return QFileInfo(m_path).fileName();
}

QString Bridge::fromText() const
{
    const QString dir = QFileInfo(m_path).absolutePath();
    const QList<std::pair<QStandardPaths::StandardLocation, QString>> places = {
        {QStandardPaths::DownloadLocation, tr("Downloads")}, {QStandardPaths::DesktopLocation, tr("Desktop")},
        {QStandardPaths::DocumentsLocation, tr("Documents")}, {QStandardPaths::HomeLocation, tr("Home")},
    };
    for (const auto &[location, name] : places) {
        if (dir == QStandardPaths::writableLocation(location)) {
            return tr("%1, from your %2 folder").arg(fileName(), name);
        }
    }
    return tr("%1, from %2").arg(fileName(), QFileInfo(dir).fileName().isEmpty() ? dir : QFileInfo(dir).fileName());
}

void Bridge::readCatalogue()
{
    QFile file(catalogueFile);
    if (!file.open(QIODevice::ReadOnly)) {
        file.setFileName(QStringLiteral(":/qt/qml/WildOS/AppBridge/data/alternatives.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }
    }
    const QString haystack = (fileName() + QLatin1Char('\n') + m_info.productName + QLatin1Char('\n') + m_info.companyName + QLatin1Char('\n')
                              + m_info.fileDescription)
                                 .toLower();
    const QJsonArray apps = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("apps")).toArray();
    for (const QJsonValue &value : apps) {
        const QJsonObject app = value.toObject();
        const QJsonArray words = app.value(QStringLiteral("match")).toArray();
        const bool matched = std::any_of(words.begin(), words.end(), [&haystack](const QJsonValue &word) {
            return haystack.contains(word.toString());
        });
        if (!matched) {
            continue;
        }
        m_catalogueName = app.value(QStringLiteral("name")).toString();
        m_storeId = app.value(QStringLiteral("storeId")).toString();
        m_nativeText = app.value(QStringLiteral("native")).toString();
        m_callApp = app.value(QStringLiteral("calls")).toBool();
        m_webApp = app.value(QStringLiteral("webApp")).toString();
        return;
    }
}

void Bridge::checkSignature()
{
    if (!m_info.signedFile || QStandardPaths::findExecutable(QStringLiteral("osslsigncode")).isEmpty()) {
        m_publisherState = QStringLiteral("unknown");
        return;
    }
    // Only a signature that checks out against the trusted certificate
    // authorities counts; a signature alone says nothing about who made it.
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int exitCode) {
        process->deleteLater();
        const QString output = QString::fromUtf8(process->readAllStandardOutput());
        m_publisherState = QStringLiteral("unknown");
        if (exitCode == 0 && output.contains(QLatin1String("Signature verification: ok"))) {
            const int signer = output.indexOf(QLatin1String("Signer's certificate:"));
            const auto match = QRegularExpression(QStringLiteral("Subject:.*?/CN=([^/\\n]+)")).match(output, std::max(signer, 0));
            if (match.hasMatch()) {
                m_signer = match.captured(1).trimmed();
                // osslsigncode escapes punctuation in names, as in "Notepad\+\+".
                m_signer.remove(QRegularExpression(QStringLiteral("\\\\(?=.)")));
                m_publisherState = QStringLiteral("verified");
            }
        }
        Q_EMIT publisherChanged();
    });
    QStringList args = {QStringLiteral("verify"), QStringLiteral("-in"), m_path};
    if (QFileInfo::exists(QStringLiteral("/etc/ssl/certs/ca-certificates.crt"))) {
        args << QStringLiteral("-CAfile") << QStringLiteral("/etc/ssl/certs/ca-certificates.crt");
    }
    process->start(QStringLiteral("osslsigncode"), args);
    QTimer::singleShot(15000, process, [process] {
        process->kill();
    });
}

void Bridge::setDocuments(bool on)
{
    m_permissions.documents = on;
    Q_EMIT permissionsChanged();
}

void Bridge::setPrinter(bool on)
{
    m_permissions.printer = on;
    Q_EMIT permissionsChanged();
}

void Bridge::setInternet(bool on)
{
    m_permissions.internet = on;
    Q_EMIT permissionsChanged();
}

void Bridge::setState(const QString &state, const QString &message)
{
    m_state = state;
    m_message = message;
    Q_EMIT stateChanged();
}

void Bridge::setProgress(int percent, const QString &text)
{
    m_progress = percent;
    m_progressText = text;
    Q_EMIT progressChanged();
}

void Bridge::install()
{
    if (m_state == QLatin1String("installing")) {
        return;
    }
    if (needsSetup()) {
        openSettings();
        close();
        return;
    }
    setState(QStringLiteral("installing"));
    setProgress(-1, tr("Opening %1 in Windows mode").arg(m_appName));
    const QString why = installInWindowsMode();
    if (!why.isEmpty()) {
        setState(QStringLiteral("failed"), why);
    }
}

void Bridge::stop()
{
    if (m_state == QLatin1String("installing")) {
        setState(QStringLiteral("ask"));
    }
}

bool Bridge::canOpenStore() const
{
    return !m_storeId.isEmpty() && !QStandardPaths::findExecutable(QStringLiteral("wildos-store")).isEmpty();
}

void Bridge::openStore()
{
    if (!canOpenStore()) {
        return;
    }
    QProcess::startDetached(QStringLiteral("wildos-store"), {QStringLiteral("--app"), m_storeId});
    close();
}

void Bridge::openSettings()
{
    QProcess::startDetached(QStringLiteral("wildos-settings"), {QStringLiteral("--page"), QStringLiteral("windowsapps")});
}

void Bridge::openApp(int)
{
    close();
}

void Bridge::close()
{
    QCoreApplication::quit();
}

void Bridge::setDialogGeometry(QQuickWindow *window, const QRectF &rect, qreal radius)
{
    if (!window) {
        return;
    }
    const QRect r = rect.toAlignedRect();
    const QRegion region = roundedRegion(r, int(radius));
    // Clicks outside the dialog fall through to the desktop rather than the
    // transparent margin kept for its shadow.
    window->setMask(region);
    KWindowEffects::enableBlurBehind(window, true, region);
}
