#include "installer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QWindow>

#include <pwd.h>
#include <unistd.h>

static const QString checker = QStringLiteral("/usr/libexec/wildos-installer/check-computer");
// From base-files, so it is on every Debian system; the image's slimming
// only drops /usr/share/doc.
static const QString licenceFile = QStringLiteral("/usr/share/common-licenses/GPL-3");

Installer::Installer()
{
    // Calamares 3.3.14 crashes while tearing its pages down after the event
    // loop ends, so closing it (the sidebar's Close the installer, Alt+F4)
    // ended on a crash report. Before the install step nothing is mounted
    // or written, so it leaves here instead; after it, Calamares' own
    // shutdown runs as before, so a finished install still restarts.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        if (m_exitQuickly)
            ::_exit(0);
    });

    // Letters and digits that cannot be mistaken for one another when
    // written down or read back: 32 of them, so 30 give 150 random bits.
    static const QString alphabet = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
    QRandomGenerator *random = QRandomGenerator::system();
    for (int i = 0; i < 30; ++i) {
        if (i > 0 && i % 5 == 0)
            m_recoveryKey += u'-';
        m_recoveryKey += alphabet.at(int(random->bounded(quint32(alphabet.size()))));
    }
}

Installer *Installer::instance()
{
    static Installer *s = new Installer;
    return s;
}

Installer *Installer::create(QQmlEngine *, QJSEngine *)
{
    Installer *s = instance();
    // Every page's engine gets the same object and none of them may delete it.
    QJSEngine::setObjectOwnership(s, QJSEngine::CppOwnership);
    return s;
}

void Installer::setWelcome(QObject *o)
{
    if (m_welcome != o) {
        m_welcome = o;
        Q_EMIT welcomeChanged();
    }
}

void Installer::setLocale(QObject *o)
{
    if (m_locale != o) {
        m_locale = o;
        Q_EMIT localeChanged();
    }
}

void Installer::setKeyboard(QObject *o)
{
    if (m_keyboard != o) {
        m_keyboard = o;
        Q_EMIT keyboardChanged();
    }
}

void Installer::setFinished(QObject *o)
{
    if (m_finished != o) {
        m_finished = o;
        Q_EMIT finishedChanged();
    }
}

void Installer::runChecks()
{
    if (m_checking)
        return;
    m_checking = true;
    Q_EMIT checksChanged();
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        const QJsonObject result = QJsonDocument::fromJson(p->readAllStandardOutput()).object();
        p->deleteLater();
        m_checks = result.value(QStringLiteral("checks")).toArray().toVariantList();
        m_alongsideBlocked = result.value(QStringLiteral("alongsideBlocked")).toString();
        m_checking = false;
        Q_EMIT checksChanged();
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            m_checking = false;
            Q_EMIT checksChanged();
        }
    });
    p->start(qEnvironmentVariable("WILDOS_CHECK_COMPUTER", checker), {});
}

QString Installer::obscure(const QString &text) const
{
    QString out;
    for (const QChar c : text) {
        const ushort u = c.unicode();
        out += u <= 0x21 ? c : QChar(ushort(0x1001F - u));
    }
    return out;
}

static QString run(const QString &program, const QStringList &args, int *code = nullptr)
{
    QProcess p;
    p.start(program, args);
    p.waitForFinished(20000);
    if (code)
        *code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return QString::fromUtf8(p.readAllStandardOutput());
}

void Installer::refreshDrives()
{
    const QString liveSource = run(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"),
                                                               QStringLiteral("SOURCE"),
                                                               QStringLiteral("/run/initramfs/live")})
                                   .trimmed();
    const QJsonArray disks = QJsonDocument::fromJson(
                                 run(QStringLiteral("lsblk"),
                                     {QStringLiteral("-J"), QStringLiteral("-b"), QStringLiteral("-p"),
                                      QStringLiteral("-o"),
                                      QStringLiteral("NAME,TYPE,RM,RO,HOTPLUG,TRAN,FSTYPE,LABEL,SIZE,MODEL")})
                                     .toUtf8())
                                 .object()
                                 .value(QStringLiteral("blockdevices"))
                                 .toArray();
    static const QStringList readable{QStringLiteral("vfat"), QStringLiteral("exfat"), QStringLiteral("ntfs")};
    m_drives.clear();
    for (const QJsonValue &dv : disks) {
        const QJsonObject disk = dv.toObject();
        const bool removable = disk.value(QStringLiteral("rm")).toBool() || disk.value(QStringLiteral("hotplug")).toBool()
            || disk.value(QStringLiteral("tran")).toString() == QLatin1String("usb");
        if (!removable)
            continue;
        QJsonArray parts = disk.value(QStringLiteral("children")).toArray();
        if (parts.isEmpty())
            parts.append(disk);
        bool isLive = false;
        for (const QJsonValue &pv : parts)
            isLive = isLive || pv.toObject().value(QStringLiteral("name")).toString() == liveSource;
        if (isLive)
            continue;
        for (const QJsonValue &pv : parts) {
            const QJsonObject part = pv.toObject();
            if (part.value(QStringLiteral("ro")).toBool()
                || !readable.contains(part.value(QStringLiteral("fstype")).toString()))
                continue;
            QString name = part.value(QStringLiteral("label")).toString();
            if (name.isEmpty())
                name = disk.value(QStringLiteral("model")).toString().trimmed();
            const qint64 size = part.value(QStringLiteral("size")).toVariant().toLongLong();
            m_drives.append(QVariantMap{{QStringLiteral("device"), part.value(QStringLiteral("name")).toString()},
                                        {QStringLiteral("name"), name.isEmpty() ? tr("USB drive") : name},
                                        {QStringLiteral("size"), QStringLiteral("%1 GB").arg(qMax<qint64>(1, qRound64(size / 1e9)))}});
        }
    }
    Q_EMIT drivesChanged();
}

QString Installer::saveKey(const QString &device)
{
    const QString mountPoint = QStringLiteral("/run/wildos-installer/key-drive");
    QDir().mkpath(mountPoint);
    QString target = run(QStringLiteral("findmnt"), {QStringLiteral("-n"), QStringLiteral("-o"), QStringLiteral("TARGET"),
                                                     QStringLiteral("--source"), device})
                         .split(u'\n')
                         .first()
                         .trimmed();
    const bool mountedHere = target.isEmpty();
    if (mountedHere) {
        int code = 0;
        run(QStringLiteral("mount"), {device, mountPoint}, &code);
        if (code != 0)
            return tr("That drive could not be opened. Try another one.");
        target = mountPoint;
    }
    QFile file(target + QStringLiteral("/Wild OS recovery key.txt"));
    QString problem;
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString text = tr("Wild OS recovery key\r\n\r\n%1\r\n\r\n"
                                "This unlocks the disk of the computer you installed Wild OS on if its disk "
                                "password is ever forgotten. Keep it somewhere other than that computer.\r\n")
                                 .arg(m_recoveryKey);
        if (file.write(text.toUtf8()) < 0)
            problem = tr("The key could not be written to that drive. Try another one.");
        file.close();
    } else {
        problem = tr("The key could not be written to that drive. Try another one.");
    }
    run(QStringLiteral("sync"), {});
    if (mountedHere)
        run(QStringLiteral("umount"), {mountPoint});
    if (problem.isEmpty() && !m_keySaved) {
        m_keySaved = true;
        Q_EMIT keySavedChanged();
    }
    return problem;
}

QString Installer::startMode() const
{
    QFile cmdline(QStringLiteral("/proc/cmdline"));
    if (!cmdline.open(QIODevice::ReadOnly))
        return QString();
    QString mode;
    for (const QByteArray &word : cmdline.readAll().simplified().split(' ')) {
        if (word.startsWith("wildos.mode="))
            mode = QString::fromUtf8(word.mid(12));
    }
    return mode;
}

QVariantList Installer::licenceSections() const
{
    QFile file(licenceFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString text = QString::fromUtf8(file.readAll());

    // A heading is a paragraph of one line, "  15. Disclaimer of Warranty.",
    // numbered one more than the last, so a number inside the terms can
    // never start a section by mistake.
    static const QRegularExpression heading(QStringLiteral(R"(^\s*(\d{1,2})\.\s+(.+)\.$)"));
    QVariantList sections;
    QVariantMap section{{QStringLiteral("number"), -1}, {QStringLiteral("title"), QString()}};
    QStringList paragraphs;
    int next = 0;
    const auto flush = [&] {
        section.insert(QStringLiteral("paragraphs"), paragraphs);
        sections.append(section);
        paragraphs.clear();
    };

    const QStringList blocks = text.split(QRegularExpression(QStringLiteral(R"(\n[ \t]*\n)")), Qt::SkipEmptyParts);
    for (const QString &block : blocks) {
        QStringList lines = block.split(u'\n');
        for (QString &line : lines)
            line = line.trimmed();
        lines.removeAll(QString());
        if (lines.isEmpty())
            continue;
        if (lines.size() == 1) {
            const auto m = heading.match(lines.first());
            if (m.hasMatch() && m.captured(1).toInt() == next) {
                flush();
                section = {{QStringLiteral("number"), next}, {QStringLiteral("title"), m.captured(2)}};
                paragraphs.append(lines.first());
                ++next;
                continue;
            }
        }
        paragraphs.append(lines.join(u' '));
    }
    flush();
    return sections;
}

QString Installer::saveLicence(QObject *window)
{
    // Calamares runs as root through sudo; the copy belongs to the person
    // in the live session, so it starts in their home folder and is theirs.
    QString home = QDir::homePath();
    uid_t uid = 0;
    gid_t gid = 0;
    if (const QByteArray user = qgetenv("SUDO_USER"); !user.isEmpty()) {
        if (const passwd *pw = getpwnam(user.constData())) {
            home = QString::fromLocal8Bit(pw->pw_dir);
            uid = pw->pw_uid;
            gid = pw->pw_gid;
        }
    }

    QFileDialog dialog(nullptr, tr("Save a copy of the licence"), home + QStringLiteral("/GNU General Public License v3.txt"),
                       tr("Text files (*.txt)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("txt"));
    dialog.winId();
    if (auto *parent = qobject_cast<QWindow *>(window); parent && dialog.windowHandle())
        dialog.windowHandle()->setTransientParent(parent);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return QString();
    const QString target = dialog.selectedFiles().first();

    QFile source(licenceFile);
    if (!source.open(QIODevice::ReadOnly))
        return tr("The licence file could not be read.");
    const QByteArray bytes = source.readAll();
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size()) {
        return tr("The licence could not be saved there. Try another folder or drive.");
    }
    out.close();
    // FAT and NTFS drives have no owners and refuse; the copy is saved anyway.
    if (uid != 0)
        (void)::chown(QFile::encodeName(target).constData(), uid, gid);
    run(QStringLiteral("sync"), {});
    return QString();
}

void Installer::quitNow()
{
    // Straight out, without Calamares' "Close the installer?" question:
    // on the first page nothing has been chosen.
    ::_exit(0);
}
