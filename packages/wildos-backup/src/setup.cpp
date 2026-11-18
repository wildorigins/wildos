#include "setup.h"
#include "backupstatus.h"
#include "drives.h"
#include "keyring.h"
#include "paths.h"
#include "restic.h"
#include "settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <cerrno>
#include <cstring>

#include <functional>

namespace
{
Setup *s_instance = nullptr;

// One restic command, its result handed to `done`, then it goes.
void runRestic(QObject *owner, const QString &repo, const QByteArray &password, const QStringList &args,
               std::function<void(int code, Restic *restic)> done)
{
    auto *restic = new Restic(repo, password, owner);
    QObject::connect(restic, &Restic::finished, owner, [restic, done](int code) {
        done(code, restic);
        restic->deleteLater();
    });
    restic->start(args);
}

}

Setup::Setup(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
}

Setup::~Setup()
{
    m_password.fill('\0');
    m_shown.fill('\0');
}

Setup *Setup::instance()
{
    if (!s_instance) {
        new Setup(QCoreApplication::instance());
    }
    return s_instance;
}

Setup *Setup::create(QQmlEngine *, QJSEngine *)
{
    Setup *setup = instance();
    QQmlEngine::setObjectOwnership(setup, QQmlEngine::CppOwnership);
    return setup;
}

void Setup::setState(const QString &state, const QString &step)
{
    m_state = state;
    m_step = step;
    if (state != QLatin1String("failed") && state != QLatin1String("needPassword")) {
        m_error.clear();
        m_errorDetail.clear();
    }
    Q_EMIT changed();
}

void Setup::failed(const QString &error, const QString &detail)
{
    m_error = error;
    m_errorDetail = detail;
    m_password.fill('\0');
    m_password.clear();
    m_shown.fill('\0');
    m_shown.clear();
    Q_EMIT passwordChanged();
    setState(QStringLiteral("failed"));
}

void Setup::useDrive(const QString &uuid)
{
    if (m_state == QLatin1String("working")) {
        return;
    }
    m_uuid = uuid;
    m_error.clear();
    setState(QStringLiteral("working"), tr("Opening the drive"));
    Drives::mountLater(this, uuid, [this](const QString &mountPoint, const QString &error, const DriveInfo &info) {
        m_driveName = info.name;
        m_kind = info.kind;
        opened(mountPoint, error);
    });
}

void Setup::opened(const QString &mountPoint, const QString &error)
{
    if (mountPoint.isEmpty()) {
        failed(error.isEmpty() ? tr("The drive couldn't be opened.") : tr("The drive couldn't be opened: %1").arg(error));
        return;
    }
    m_repo = mountPoint + QLatin1Char('/') + Paths::repoFolder();
    const bool existing = QFileInfo::exists(m_repo + QStringLiteral("/config"));
    setState(QStringLiteral("working"), existing ? tr("Opening the backups on it") : tr("Making the backup folder"));
    Keyring::read(this, [this, existing](bool ok, const QByteArray &password, bool missing, const QString &why) {
        if (!ok && !missing) {
            failed(tr("Your keyring couldn't be opened: %1").arg(why));
            return;
        }
        if (existing) {
            if (ok) {
                verify(password, true);
            } else {
                setState(QStringLiteral("needPassword"));
            }
            return;
        }
        if (ok) {
            // One password for all of their drives: one thing to write down.
            m_password = password;
            if (!Settings::instance()->passwordShown()) {
                m_shown = password;
                Q_EMIT passwordChanged();
            }
            createRepository();
            return;
        }
        m_password = Keyring::generate();
        // Kept here, not read back for the dialog, from now until the
        // dialog closes: a locked wallet must not leave it empty.
        m_shown = m_password;
        Q_EMIT passwordChanged();
        Keyring::write(this, m_password, [this](bool written, const QString &why) {
            if (!written) {
                // Never make backups whose only key is nowhere.
                failed(tr("Your keyring couldn't save the backup password, so no backups were set up: %1").arg(why));
                return;
            }
            // Read back once: a backend that says yes but kept nothing would
            // otherwise leave backups that nothing on this PC can open.
            Keyring::read(this, [this](bool ok, const QByteArray &stored, bool, const QString &readWhy) {
                if (!ok || stored != m_password) {
                    failed(ok ? tr("Your keyring didn't keep the backup password, so no backups were set up.")
                              : tr("Your keyring couldn't be read back after saving the backup password, so no backups were set up: %1").arg(readWhy));
                    return;
                }
                createRepository();
            });
        });
    });
}

void Setup::createRepository()
{
    const QFileInfo folder(m_repo);
    if (!folder.isDir() || !folder.isWritable()) {
        const QString mountPoint = folder.absolutePath();
        if (!folder.exists() && QFileInfo(mountPoint).isWritable()) {
            if (!QDir().mkdir(m_repo)) {
                const int error = errno;
                m_shown.clear();
                failed(tr("The folder for backups couldn't be made on the drive: %1.").arg(QString::fromLocal8Bit(std::strerror(error))));
                return;
            }
        } else {
            // The drive's top folder is root's, as on an ext4 drive formatted
            // on another Linux PC: an administrator lets Backup make its own
            // folder there, and nothing else changes on the drive.
            makeFolderAsAdmin(mountPoint);
            return;
        }
    }
    initRepository();
}

void Setup::makeFolderAsAdmin(const QString &mountPoint)
{
    setState(QStringLiteral("working"), tr("Asking to make Backup's folder on the drive"));
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        const QString why = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        if (status == QProcess::NormalExit && code == 0 && QFileInfo(m_repo).isWritable()) {
            initRepository();
            return;
        }
        m_shown.clear();
        // pkexec's own codes: 126 when the person said no, 127 when polkit
        // wouldn't ask.
        if (code == 126 || code == 127) {
            failed(tr("Wild OS can't write to this drive. Use a drive formatted for Windows or Mac (exFAT), or allow Backup to make its folder."));
            return;
        }
        QString reason = why.section(QLatin1Char('\n'), -1).remove(QStringLiteral("make-folder: "));
        failed(reason.isEmpty() ? tr("Backup's folder couldn't be made on the drive.")
                                : tr("Backup's folder couldn't be made on the drive: %1.").arg(reason));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_shown.clear();
            failed(tr("Wild OS can't write to this drive. Use a drive formatted for Windows or Mac (exFAT), or allow Backup to make its folder."));
        }
    });
    process->start(QStringLiteral("pkexec"), {Paths::folderHelper(), mountPoint});
}

void Setup::initRepository()
{
    setState(QStringLiteral("working"), tr("Encrypting the drive's backup folder"));
    runRestic(this, m_repo, m_password, {QStringLiteral("init"), QStringLiteral("--json")}, [this](int code, Restic *restic) {
        if (code != Restic::Ok) {
            m_shown.clear();
            failed(Restic::explain(code, restic->errorText()), Restic::detail(code, restic->errorText()));
            return;
        }
        added(!m_shown.isEmpty());
    });
}

void Setup::verify(const QByteArray &password, bool fromKeyring)
{
    m_password = password;
    setState(QStringLiteral("working"), tr("Checking the password"));
    runRestic(this, m_repo, m_password, {QStringLiteral("cat"), QStringLiteral("config")}, [this, fromKeyring](int code, Restic *restic) {
        if (code == Restic::Ok) {
            if (fromKeyring) {
                added(false);
                return;
            }
            Keyring::write(this, m_password, [this](bool written, const QString &why) {
                if (!written) {
                    failed(tr("Your keyring couldn't save the backup password: %1").arg(why));
                    return;
                }
                added(false);
            });
            return;
        }
        m_password.fill('\0');
        m_password.clear();
        if (code == Restic::WrongPassword) {
            m_error = fromKeyring ? QString() : tr("That password doesn't open the backups on this drive.");
            setState(QStringLiteral("needPassword"));
            return;
        }
        failed(Restic::explain(code, restic->errorText()), Restic::detail(code, restic->errorText()));
    });
}

void Setup::givePassword(const QString &password)
{
    const QByteArray typed = password.trimmed().toUtf8();
    if (typed.isEmpty() || m_repo.isEmpty()) {
        return;
    }
    verify(typed, false);
}

void Setup::added(bool created)
{
    Settings *settings = Settings::instance();
    settings->addDrive(m_uuid, m_driveName, m_kind);
    m_password.fill('\0');
    m_password.clear();
    Drives::instance()->refresh();
    setState(created ? QStringLiteral("created") : QStringLiteral("added"));
}

void Setup::dismiss()
{
    const bool first = m_state == QLatin1String("created");
    if (first) {
        Settings::instance()->setPasswordShown(true);
    }
    m_shown.fill('\0');
    m_shown.clear();
    Q_EMIT passwordChanged();
    setState(QStringLiteral("idle"));
    if (first) {
        BackupStatus::instance()->backUpNow();
    }
}

void Setup::reveal()
{
    m_revealing = true;
    m_revealError.clear();
    Q_EMIT passwordChanged();
    Keyring::read(this, [this](bool ok, const QByteArray &password, bool missing, const QString &why) {
        m_revealing = false;
        if (ok && !password.isEmpty()) {
            m_shown = password;
        } else if (ok || missing) {
            m_revealError = tr("There's no backup password in your keyring.");
        } else {
            m_revealError = tr("Your keyring is locked or couldn't be opened, so the password can't be shown. Unlock it and try again. (%1)").arg(why);
        }
        Q_EMIT passwordChanged();
    });
}

void Setup::hidePassword()
{
    m_shown.fill('\0');
    m_shown.clear();
    m_revealError.clear();
    Q_EMIT passwordChanged();
}

void Setup::check()
{
    if (m_checking) {
        return;
    }
    const QString uuid = Drives::instance()->pluggedIn();
    m_checking = true;
    m_checkResult.clear();
    m_checkDetail.clear();
    Q_EMIT checkChanged();
    const auto finish = [this](bool ok, const QString &text, const QString &detail = QString()) {
        m_checking = false;
        m_checkOk = ok;
        m_checkResult = text;
        m_checkDetail = detail;
        Q_EMIT checkChanged();
    };
    if (uuid.isEmpty()) {
        finish(false, tr("Plug in your backup drive to check it."));
        return;
    }
    Drives::mountLater(this, uuid, [this, finish](const QString &mountPoint, const QString &error, const DriveInfo &) {
        if (mountPoint.isEmpty()) {
            finish(false, tr("The drive couldn't be opened: %1").arg(error));
            return;
        }
        const QString repo = mountPoint + QLatin1Char('/') + Paths::repoFolder();
        Keyring::read(this, [this, repo, finish](bool ok, const QByteArray &password, bool, const QString &) {
            if (!ok) {
                finish(false, tr("The backup password couldn't be read from your keyring."));
                return;
            }
            // A twentieth of the data each time: every pack gets read over
            // a few checks without one taking an hour.
            runRestic(this, repo, password, {QStringLiteral("check"), QStringLiteral("--read-data-subset"), QStringLiteral("5%")},
                      [finish](int code, Restic *restic) {
                          if (code == Restic::Ok) {
                              finish(true, tr("Your backups can be read and nothing is damaged."));
                          } else {
                              finish(false, Restic::explain(code, restic->errorText()), Restic::detail(code, restic->errorText()));
                          }
                      });
        });
    });
}
