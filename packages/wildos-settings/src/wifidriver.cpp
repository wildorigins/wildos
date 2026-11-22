#include "wifidriver.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace
{
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-wifi-driver");

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}

bool anyWifi()
{
    return !QDir(QStringLiteral("/sys/class/ieee80211")).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
}
}

WifiDriver::WifiDriver(QObject *parent)
    : QObject(parent)
{
    // The first Broadcom network controller (vendor 0x14e4, class 0x0280).
    const QDir pci(QStringLiteral("/sys/bus/pci/devices"));
    for (const QString &address : pci.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = pci.filePath(address);
        if (readFile(path + QStringLiteral("/vendor")) != "0x14e4" || !readFile(path + QStringLiteral("/class")).startsWith("0x0280")) {
            continue;
        }
        m_device = path;
        QProcess lspci;
        lspci.start(QStringLiteral("lspci"), {QStringLiteral("-s"), address, QStringLiteral("-mm")});
        if (lspci.waitForFinished(3000)) {
            // 03:00.0 "Network controller" "Broadcom Inc. and subsidiaries" "BCM4360 802.11ac Dual Band Wireless Network Adapter" ...
            QStringList fields;
            QRegularExpressionMatchIterator it = QRegularExpression(QStringLiteral("\"([^\"]*)\"")).globalMatch(QString::fromUtf8(lspci.readAllStandardOutput()));
            while (it.hasNext()) {
                fields << it.next().captured(1);
            }
            if (fields.size() >= 3) {
                m_name = fields.at(2);
            }
        }
        if (m_name.isEmpty()) {
            m_name = QStringLiteral("Broadcom Wi-Fi");
        }
        break;
    }
}

QString WifiDriver::driverInUse() const
{
    if (m_device.isEmpty()) {
        return {};
    }
    const QFileInfo link(m_device + QStringLiteral("/driver"));
    return link.exists() ? QFileInfo(link.symLinkTarget()).fileName() : QString();
}

// The same rule as settings-wifi-driver's check.
bool WifiDriver::needed() const
{
    if (m_device.isEmpty() || installed() || anyWifi()) {
        return false;
    }
    const QString driver = driverInUse();
    return driver.isEmpty() || driver == QLatin1String("bcma-pci-bridge") || driver == QLatin1String("ssb");
}

bool WifiDriver::installed() const
{
    return QFile::exists(QStringLiteral("/var/lib/dpkg/info/broadcom-sta-dkms.list"));
}

bool WifiDriver::restartNeeded() const
{
    return installed() && driverInUse() != QLatin1String("wl");
}

void WifiDriver::install()
{
    if (m_process) {
        return;
    }
    m_error.clear();
    m_step = tr("Waiting for your password");
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        while (m_process->canReadLine()) {
            const QString line = QString::fromUtf8(m_process->readLine()).trimmed();
            if (line.startsWith(QLatin1String("step "))) {
                m_step = line.mid(5);
            } else if (line.startsWith(QLatin1String("code "))) {
                m_code = line.mid(5);
            }
            Q_EMIT changed();
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code) {
        if (code == 126 || code == 127) {
            m_error = tr("Nothing was changed because the password was not given.");
        } else if (code == 4) {
            m_error = tr("Broadcom's driver doesn't support this Wi-Fi chip, so nothing was changed.");
        } else if (code == 5) {
            m_error = tr("Broadcom's driver could not be set up on this PC, so nothing was changed.");
        } else if (code != 0) {
            m_error = tr("The driver could not be downloaded. Check the internet connection and try again.");
        }
        m_process->deleteLater();
        m_process = nullptr;
        m_step.clear();
        Q_EMIT changed();
    });
    m_process->start(QStringLiteral("pkexec"), {helper, QStringLiteral("install")});
    Q_EMIT changed();
}

void WifiDriver::restart()
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown"),
                                                  QStringLiteral("logoutAndReboot"));
    QDBusConnection::sessionBus().asyncCall(message);
}
