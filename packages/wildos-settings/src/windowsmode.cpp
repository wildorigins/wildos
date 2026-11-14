#include "windowsmode.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMimeData>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace
{
const QString service = QStringLiteral("org.wildos.WindowsMode1");
const QString path = QStringLiteral("/org/wildos/WindowsMode1");
}

WindowsMode::WindowsMode(QObject *parent)
    : QObject(parent)
{
    if (!installed()) {
        return;
    }
    m_service = new QDBusInterface(service, path, service, QDBusConnection::sessionBus(), this);
    QDBusConnection::sessionBus().connect(service, path, service, QStringLiteral("Changed"), this, SLOT(refresh()));
    refresh();
}

bool WindowsMode::installed() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("wildos-winmode")).isEmpty();
}

void WindowsMode::refresh()
{
    if (!m_service) {
        return;
    }
    auto *watcher = new QDBusPendingCallWatcher(m_service->asyncCall(QStringLiteral("Status")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        m_status = reply.isError() ? QVariantMap() : QJsonDocument::fromJson(reply.value().toUtf8()).object().toVariantMap();
        Q_EMIT statusChanged();
    });
}

QString WindowsMode::call(const QString &method, const QVariantList &args, int timeoutMs)
{
    if (!m_service) {
        return QStringLiteral("Windows mode isn't installed on this PC.");
    }
    m_service->setTimeout(timeoutMs);
    const QDBusMessage reply = m_service->callWithArgumentList(QDBus::Block, method, args);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        return QStringLiteral("Windows mode didn't answer. Restart the PC, then try again.");
    }
    return reply.arguments().value(0).toString();
}

QVariantList WindowsMode::memoryChoices() const
{
    qint64 total = 0;
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (meminfo.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : meminfo.readAll().split('\n')) {
            if (line.startsWith("MemTotal:")) {
                total = line.mid(9).trimmed().split(' ').value(0).toLongLong() * 1024;
            }
        }
    }
    // As the service allows: at least Windows 11's 4 GB, at most three
    // quarters of the PC's memory.
    QVariantList choices;
    for (int gb : {4, 6, 8, 12, 16, 24, 32}) {
        if (gb == 4 || gb * 1024.0 * 1024 * 1024 <= total * 0.75) {
            choices << QVariantMap{{QStringLiteral("value"), gb}, {QStringLiteral("text"), QStringLiteral("Up to %1 GB").arg(gb)}};
        }
    }
    return choices;
}

QString WindowsMode::setOption(const QString &key, const QString &value)
{
    return call(QStringLiteral("SetOption"), {key, value});
}

void WindowsMode::setProductKey(const QString &key)
{
    if (!m_service) {
        Q_EMIT productKeyDone(QStringLiteral("Windows mode isn't installed on this PC."));
        return;
    }
    if (m_activating) {
        return;
    }
    m_activating = true;
    Q_EMIT activatingChanged();
    // Windows may have to start first, and activation then waits on
    // Microsoft's servers, so Settings carries on meanwhile.
    m_service->setTimeout(900000);
    auto *watcher = new QDBusPendingCallWatcher(m_service->asyncCall(QStringLiteral("SetProductKey"), key), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        m_activating = false;
        Q_EMIT activatingChanged();
        Q_EMIT productKeyDone(reply.isError() ? QStringLiteral("Windows mode didn't answer. Restart the PC, then try again.") : reply.value());
    });
}

void WindowsMode::power(const QString &action)
{
    if (!m_service || !m_powerAction.isEmpty()) {
        return;
    }
    m_powerAction = action;
    Q_EMIT powerActionChanged();
    // Shutting down waits for Windows, and for the person's answer when an
    // app has unsaved work, so the call may run for a long time.
    m_service->setTimeout(1800000);
    auto *watcher = new QDBusPendingCallWatcher(m_service->asyncCall(QStringLiteral("Power"), action), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        m_powerAction.clear();
        Q_EMIT powerActionChanged();
        Q_EMIT powerDone(reply.isError() ? QStringLiteral("Windows mode didn't answer. Restart the PC, then try again.") : reply.value());
        refresh();
    });
}

bool WindowsMode::keyShapeValid(const QString &key) const
{
    static const QRegularExpression shape(QStringLiteral("^([BCDFGHJKMNPQRTVWXY2346789]{5}-?){4}[BCDFGHJKMNPQRTVWXY2346789]{5}$"));
    return shape.match(key.trimmed().toUpper().remove(QLatin1Char(' '))).hasMatch();
}

QString WindowsMode::checkUpdates()
{
    return call(QStringLiteral("CheckUpdates"));
}

QString WindowsMode::openDesktop()
{
    return call(QStringLiteral("OpenDesktop"));
}

void WindowsMode::activateWithAccount()
{
    // Windows may start from off first, then the account is made.
    callAsync(QStringLiteral("ActivateWithAccount"), 900000, &WindowsMode::accountActivationDone);
}

void WindowsMode::endAccountActivation()
{
    // Signing the account out and deleting its profile takes a minute.
    callAsync(QStringLiteral("EndActivation"), 480000, &WindowsMode::accountActivationDone);
}

QString WindowsMode::activationPassword()
{
    return m_service ? call(QStringLiteral("ActivationPassword")) : QString();
}

void WindowsMode::callAsync(const QString &method, int timeoutMs, void (WindowsMode::*done)(const QString &))
{
    if (!m_service) {
        Q_EMIT(this->*done)(QStringLiteral("Windows mode isn't installed on this PC."));
        return;
    }
    m_service->setTimeout(timeoutMs);
    auto *watcher = new QDBusPendingCallWatcher(m_service->asyncCall(method), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, done] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        Q_EMIT(this->*done)(reply.isError() ? QStringLiteral("Windows mode didn't answer. Restart the PC, then try again.") : reply.value());
        refresh();
    });
}

QString WindowsMode::repair()
{
    return call(QStringLiteral("Repair"), {}, 180000);
}

// Windows mode's agent keeps the password it set in the keyring under these
// attributes (wildos-winmode, agent.py SECRET_ATTRS). Without a keyring it
// only holds it in memory, and there is nothing to show.
QString WindowsMode::windowsPassword(const QString &account) const
{
    const QString tool = QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
    if (tool.isEmpty() || account.isEmpty()) {
        return QString();
    }
    QProcess p;
    p.start(tool, {QStringLiteral("lookup"), QStringLiteral("service"), QStringLiteral("org.wildos.WindowsMode1"),
                   QStringLiteral("account"), account});
    if (!p.waitForFinished(10000) || p.exitCode() != 0) {
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

void WindowsMode::copySecret(const QString &text) const
{
    auto *mime = new QMimeData;
    mime->setText(text);
    mime->setData(QStringLiteral("x-kde-passwordManagerHint"), QByteArrayLiteral("secret"));
    QGuiApplication::clipboard()->setMimeData(mime);
}

void WindowsMode::remove()
{
    if (!m_service) {
        Q_EMIT removeDone(QStringLiteral("Windows mode isn't installed on this PC."));
        return;
    }
    if (m_removing) {
        return;
    }
    m_removing = true;
    Q_EMIT removingChanged();
    // The administrator's password is asked for first, so the call waits
    // on the person as well as on Windows being deleted.
    m_service->setTimeout(1800000);
    auto *watcher = new QDBusPendingCallWatcher(m_service->asyncCall(QStringLiteral("Remove")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        m_removing = false;
        Q_EMIT removingChanged();
        Q_EMIT removeDone(reply.isError() ? QStringLiteral("Windows mode didn't answer. Restart the PC, then try again.") : reply.value());
        refresh();
    });
}

void WindowsMode::openSetup()
{
    QProcess::startDetached(QStringLiteral("wildos-winmode"), {});
}

QString WindowsMode::size(double bytes) const
{
    // Also catches a missing figure, which QML hands over as NaN.
    if (!(bytes > 0)) {
        return {};
    }
    const QLocale locale = QLocale::system();
    const double gb = bytes / 1e9;
    return gb >= 1 ? QStringLiteral("%1 GB").arg(locale.toString(gb, 'f', 1)) : QStringLiteral("%1 MB").arg(qRound(bytes / 1e6));
}
