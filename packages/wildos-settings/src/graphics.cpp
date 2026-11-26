#include "graphics.h"

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
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-graphics-driver");

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().trimmed() : QByteArray();
}
}

Graphics::Graphics(QObject *parent)
    : QObject(parent)
{
    // The first NVIDIA display controller (PCI class 0x03xxxx, vendor 0x10de).
    const QDir pci(QStringLiteral("/sys/bus/pci/devices"));
    for (const QString &address : pci.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString path = pci.filePath(address);
        if (readFile(path + QStringLiteral("/vendor")) == "0x10de" && readFile(path + QStringLiteral("/class")).startsWith("0x03")) {
            m_device = path;
            QProcess lspci;
            lspci.start(QStringLiteral("lspci"), {QStringLiteral("-s"), address, QStringLiteral("-mm")});
            if (lspci.waitForFinished(3000)) {
                // 01:00.0 "VGA compatible controller" "NVIDIA Corporation" "GA104M [GeForce RTX 3070 Mobile]" ...
                QStringList fields;
                QRegularExpressionMatchIterator it = QRegularExpression(QStringLiteral("\"([^\"]*)\"")).globalMatch(QString::fromUtf8(lspci.readAllStandardOutput()));
                while (it.hasNext()) {
                    fields << it.next().captured(1);
                }
                // The marketing name is the part in brackets, when lspci knows it.
                if (fields.size() >= 3) {
                    const QRegularExpressionMatch bracket = QRegularExpression(QStringLiteral("\\[([^\\]]+)\\]")).match(fields.at(2));
                    m_name = bracket.hasMatch() ? bracket.captured(1) : fields.at(2);
                }
            }
            if (m_name.isEmpty()) {
                m_name = tr("NVIDIA graphics");
            }
            break;
        }
    }
}

QString Graphics::driverInUse() const
{
    if (m_device.isEmpty()) {
        return {};
    }
    const QFileInfo link(m_device + QStringLiteral("/driver"));
    return link.exists() ? QFileInfo(link.symLinkTarget()).fileName() : QString();
}

bool Graphics::nvidiaInstalled() const
{
    return QFile::exists(QStringLiteral("/var/lib/dpkg/info/nvidia-driver.list"));
}

// The firmware's SecureBoot variable: four bytes of attributes, then 1 for on.
bool Graphics::secureBoot() const
{
    const QByteArray value = readFile(QStringLiteral("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    return value.size() >= 5 && value.at(4) == 1;
}

bool Graphics::restartNeeded() const
{
    const QString running = driverInUse();
    return nvidiaInstalled() ? running != QLatin1String("nvidia") : running == QLatin1String("nvidia");
}

void Graphics::useDriver(const QString &driver)
{
    if (m_process || (driver != QLatin1String("nvidia") && driver != QLatin1String("open"))) {
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
            m_error = tr("Debian's NVIDIA driver doesn't support this graphics card, so it keeps the open driver.");
        } else if (code == 5) {
            m_error = tr("NVIDIA's driver could not be installed whole on this PC, so the open driver was kept.");
        } else if (code != 0) {
            m_error = tr("The driver could not be changed. Check the internet connection and try again.");
        }
        m_process->deleteLater();
        m_process = nullptr;
        m_step.clear();
        Q_EMIT changed();
    });
    m_process->start(QStringLiteral("pkexec"), {helper, driver});
    Q_EMIT changed();
}

bool Graphics::trialFailed() const
{
    return QFile::exists(QStringLiteral("/var/lib/wildos/graphics-driver/failed"));
}

void Graphics::restart()
{
    // Plasma's own restart path first, so open apps get the chance to save.
    // Between a driver switch and the restart, newly started programs cannot
    // draw, and that includes Plasma's sign-out screen: its restart can wait
    // for ever. So the system's own restart follows 20 seconds later
    // whatever happens to the first.
    QProcess::startDetached(QStringLiteral("systemd-run"),
                            {QStringLiteral("--user"), QStringLiteral("--collect"), QStringLiteral("--on-active=20"), QStringLiteral("systemctl"),
                             QStringLiteral("reboot")});
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown"),
                                                  QStringLiteral("logoutAndReboot"));
    QDBusConnection::sessionBus().asyncCall(message);
}

// An install still running when Settings closes carries on by itself: the
// process is let go rather than killed with this object (the helper ignores
// the closed pipe), so dpkg is never stopped halfway.
Graphics::~Graphics()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->setParent(nullptr);
        m_process = nullptr;
    }
}
