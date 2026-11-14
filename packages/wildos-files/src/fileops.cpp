#include "fileops.h"

#include "drives.h"
#include "format.h"
#include "programs.h"
#include "settings.h"
#include "sharemodel.h"

#include <KApplicationTrader>
#include <KConfigGroup>
#include <KCoreDirLister>
#include <KFileUtils>
#include <KIO/ApplicationLauncherJob>
#include <KIO/CommandLauncherJob>
#include <KIO/CopyJob>
#include <KIO/DeleteJob>
#include <KIO/EmptyTrashJob>
#include <KIO/JobTracker>
#include <KJobTrackerInterface>
#include <KJobUiDelegate>
#include <KIO/MkdirJob>
#include <KIO/OpenUrlJob>
#include <KIO/Paste>
#include <KIO/RestoreJob>
#include <KIO/StoredTransferJob>
#include <KService>
#include <KSharedConfig>
#include <KTerminalLauncherJob>
#include <KUrlMimeData>
#include <KWindowSystem>
#include <QClipboard>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QWindow>

#include <limits>

namespace
{
QString folderName(const QUrl &url)
{
    if (url.isLocalFile() && QDir::cleanPath(url.toLocalFile()) == QDir::homePath()) {
        return QStringLiteral("Home");
    }
    const QString name = url.adjusted(QUrl::StripTrailingSlash).fileName();
    return name.isEmpty() ? QStringLiteral("this folder") : name;
}

QVariantMap appEntry(const KService::Ptr &service)
{
    return {
        {QStringLiteral("id"), service->storageId()},
        {QStringLiteral("name"), service->name()},
        {QStringLiteral("icon"), service->icon()},
        // App Bridge writes a launcher like this for each Windows program
        // it installs; Open with tags them so people know which is which.
        {QStringLiteral("windows"), service->storageId().startsWith(QLatin1String("wildos-winapp-"))},
    };
}
}

Asker::Asker(QObject *parent)
    : KIO::AskUserActionInterface(parent)
{
}

void Asker::askUserRename(KJob *job, const QString &title, const QUrl &src, const QUrl &dest, KIO::RenameDialog_Options options,
                          KIO::filesize_t sizeSrc, KIO::filesize_t sizeDest, const QDateTime &ctimeSrc, const QDateTime &ctimeDest,
                          const QDateTime &mtimeSrc, const QDateTime &mtimeDest)
{
    // The clash dialog of DialogCopy.dc is the system one (wildos-portal),
    // which every other app's copies reach through Wild OS's KIO patch, so
    // a clash looks the same whichever app copied. The same rules as that
    // patch: "Keep both" names the copy "Name (2)", for every later clash
    // too when chosen for all.
    QPointer<KJob> guard(job);
    const bool canSkip = options & (KIO::RenameDialog_Skip | KIO::RenameDialog_MultipleItems);
    if (job->property("wildosEveryClashKeepsBoth").toBool() && !(options & KIO::RenameDialog_NoRename)) {
        Q_EMIT askUserRenameResult(KIO::Result_Rename, keepBothUrl(dest), job);
        return;
    }
    static int counter = 0;
    const QString token = QStringLiteral("files-%1-%2").arg(QCoreApplication::applicationPid()).arg(++counter);
    QVariantMap info;
    info[QStringLiteral("token")] = token;
    info[QStringLiteral("source")] = src.toString();
    info[QStringLiteral("dest")] = dest.toString();
    info[QStringLiteral("sourceSize")] = sizeSrc == KIO::filesize_t(-1) ? qlonglong(-1) : qlonglong(sizeSrc);
    info[QStringLiteral("destSize")] = sizeDest == KIO::filesize_t(-1) ? qlonglong(-1) : qlonglong(sizeDest);
    info[QStringLiteral("sourceModified")] = mtimeSrc.isValid() ? mtimeSrc.toMSecsSinceEpoch() : qlonglong(0);
    info[QStringLiteral("destModified")] = mtimeDest.isValid() ? mtimeDest.toMSecsSinceEpoch() : qlonglong(0);
    info[QStringLiteral("isDir")] = bool((options & KIO::RenameDialog_SourceIsDirectory) && (options & KIO::RenameDialog_DestIsDirectory));
    info[QStringLiteral("sameFile")] = bool(options & KIO::RenameDialog_OverwriteItself);
    info[QStringLiteral("canReplace")] = bool(options & KIO::RenameDialog_Overwrite);
    info[QStringLiteral("canKeepBoth")] = !(options & KIO::RenameDialog_NoRename);
    info[QStringLiteral("multiple")] = bool(options & KIO::RenameDialog_MultipleItems);
    qlonglong items = qlonglong(job->totalAmount(KJob::Files) + job->totalAmount(KJob::Directories));
    if (auto copy = qobject_cast<KIO::CopyJob *>(job)) {
        info[QStringLiteral("move")] = copy->operationMode() == KIO::CopyJob::Move;
        const QList<QUrl> sources = copy->srcUrls();
        items = std::max<qlonglong>(items, sources.size());
        const QUrl target = copy->destUrl();
        if (target.isLocalFile() && QFileInfo(target.toLocalFile()).isDir()) {
            const QDir dir(target.toLocalFile());
            info[QStringLiteral("clashes")] = qlonglong(std::count_if(sources.cbegin(), sources.cend(), [&dir](const QUrl &url) {
                return dir.exists(url.fileName());
            }));
        }
    }
    info[QStringLiteral("items")] = items;
    auto window = qobject_cast<QWindow *>(FileOps::instance()->activeWindow());
    info[QStringLiteral("parentWindow")] = window && KWindowSystem::isPlatformX11() ? QStringLiteral("x11:") + QString::number(window->winId(), 16) : QString();

    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.Dialogs"), QStringLiteral("/org/wildos/Dialogs"),
                                                          QStringLiteral("org.wildos.Dialogs"), QStringLiteral("AskClash"));
    message << info;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, std::numeric_limits<int>::max()), this);
    const auto ended = connect(job, &KJob::finished, watcher, [token] {
        QDBusMessage cancel = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.Dialogs"), QStringLiteral("/org/wildos/Dialogs"),
                                                             QStringLiteral("org.wildos.Dialogs"), QStringLiteral("CancelClash"));
        cancel << token;
        QDBusConnection::sessionBus().call(cancel, QDBus::NoBlock);
    });
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [=, this](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                disconnect(ended);
                if (!guard) {
                    return;
                }
                const QDBusPendingReply<QString, bool, QString> reply = *w;
                if (reply.isError()) {
                    // Without the system dialog, Files asks in its own window.
                    askInWindow(guard, title, src, dest, options, sizeSrc, sizeDest, ctimeSrc, ctimeDest, mtimeSrc, mtimeDest);
                    return;
                }
                const QString choice = reply.argumentAt<0>();
                const bool all = reply.argumentAt<1>();
                if (choice == QLatin1String("replace")) {
                    Q_EMIT askUserRenameResult(all ? KIO::Result_OverwriteAll : KIO::Result_Overwrite, QUrl(), guard);
                } else if (choice == QLatin1String("skip")) {
                    Q_EMIT askUserRenameResult(!canSkip ? KIO::Result_Cancel : all ? KIO::Result_AutoSkip : KIO::Result_Skip, QUrl(), guard);
                } else if (choice == QLatin1String("both")) {
                    if (all) {
                        guard->setProperty("wildosEveryClashKeepsBoth", true);
                    }
                    const QString name = reply.argumentAt<2>();
                    QUrl renamed = dest.adjusted(QUrl::RemoveFilename);
                    renamed.setPath(renamed.path() + name);
                    Q_EMIT askUserRenameResult(KIO::Result_Rename, name.isEmpty() ? keepBothUrl(dest) : renamed, guard);
                } else {
                    Q_EMIT askUserRenameResult(KIO::Result_Cancel, QUrl(), guard);
                }
            });
}

QUrl Asker::keepBothUrl(const QUrl &dest)
{
    const QUrl folder = KIO::upUrl(dest);
    const QString fileName = dest.fileName();
    QString name;
    if (folder.isLocalFile()) {
        const bool isDir = QFileInfo(dest.toLocalFile()).isDir();
        const QString suffix = isDir ? QString() : QMimeDatabase().suffixForFileName(fileName);
        const QString ext = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
        const QString base = fileName.left(fileName.size() - ext.size());
        const QDir dir(folder.toLocalFile());
        for (int n = 2; name.isEmpty() || dir.exists(name); ++n) {
            name = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(ext);
        }
    } else {
        name = KFileUtils::suggestName(folder, fileName);
    }
    QUrl url = folder;
    url.setPath(folder.path() + (folder.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + name);
    return url;
}

void Asker::askInWindow(KJob *job, const QString &, const QUrl &src, const QUrl &dest, KIO::RenameDialog_Options options,
                        KIO::filesize_t sizeSrc, KIO::filesize_t sizeDest, const QDateTime &, const QDateTime &,
                        const QDateTime &mtimeSrc, const QDateTime &mtimeDest)
{
    const bool multiple = options & KIO::RenameDialog_MultipleItems;
    const bool folders = (options & KIO::RenameDialog_SourceIsDirectory) && (options & KIO::RenameDialog_DestIsDirectory);
    const bool itself = options & KIO::RenameDialog_OverwriteItself;
    const QString name = Format::visibleName(dest.fileName(), folders, Settings::instance()->showExtensions());
    const QString where = folderName(KIO::upUrl(dest));

    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("conflict");
    prompt[QStringLiteral("multiple")] = multiple;
    if (folders) {
        prompt[QStringLiteral("title")] = QStringLiteral("Merge folders?");
        prompt[QStringLiteral("text")] = QStringLiteral("%1 already has a folder called \"%2\". Merging puts what you are copying inside it; files with the same name will be asked about one by one.").arg(where, name);
        prompt[QStringLiteral("replace")] = QStringLiteral("Merge folders");
    } else if (itself) {
        prompt[QStringLiteral("title")] = QStringLiteral("This is the same file");
        prompt[QStringLiteral("text")] = QStringLiteral("\"%1\" is already here. Keep both to make a copy next to it.").arg(name);
    } else {
        prompt[QStringLiteral("title")] = QStringLiteral("Replace or skip files");
        prompt[QStringLiteral("text")] = QStringLiteral("%1 already has a file called \"%2\".").arg(where, name);
        prompt[QStringLiteral("replace")] = QStringLiteral("Replace the file");
        prompt[QStringLiteral("incoming")] = Format::size(qint64(sizeSrc)) + QStringLiteral(", ") + Format::date(mtimeSrc);
        prompt[QStringLiteral("existing")] = Format::size(qint64(sizeDest)) + QStringLiteral(", ") + Format::date(mtimeDest);
    }
    prompt[QStringLiteral("canReplace")] = !itself && (options & KIO::RenameDialog_Overwrite);
    prompt[QStringLiteral("canSkip")] = bool(options & KIO::RenameDialog_Skip) || multiple;
    prompt[QStringLiteral("canKeepBoth")] = !(options & KIO::RenameDialog_NoRename);

    QPointer<KJob> guard(job);
    FileOps::instance()->ask(prompt, [this, guard, dest, src](const QString &choice, bool all) {
        if (!guard) {
            return;
        }
        if (choice == QStringLiteral("replace")) {
            Q_EMIT askUserRenameResult(all ? KIO::Result_OverwriteAll : KIO::Result_Overwrite, QUrl(), guard);
        } else if (choice == QStringLiteral("skip")) {
            Q_EMIT askUserRenameResult(all ? KIO::Result_AutoSkip : KIO::Result_Skip, QUrl(), guard);
        } else if (choice == QStringLiteral("both")) {
            if (all) {
                Q_EMIT askUserRenameResult(KIO::Result_AutoRename, QUrl(), guard);
            } else {
                Q_EMIT askUserRenameResult(KIO::Result_Rename, keepBothUrl(dest), guard);
            }
        } else {
            Q_EMIT askUserRenameResult(KIO::Result_Cancel, QUrl(), guard);
        }
        Q_UNUSED(src);
    });
}

void Asker::askUserSkip(KJob *job, KIO::SkipDialog_Options options, const QString &errorText)
{
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("skip");
    prompt[QStringLiteral("title")] = QStringLiteral("A file could not be copied");
    prompt[QStringLiteral("text")] = errorText;
    prompt[QStringLiteral("multiple")] = bool(options & KIO::SkipDialog_MultipleItems);
    QPointer<KJob> guard(job);
    FileOps::instance()->ask(prompt, [this, guard](const QString &choice, bool all) {
        if (!guard) {
            return;
        }
        if (choice == QStringLiteral("skip")) {
            Q_EMIT askUserSkipResult(all ? KIO::Result_AutoSkip : KIO::Result_Skip, guard);
        } else if (choice == QStringLiteral("retry")) {
            Q_EMIT askUserSkipResult(KIO::Result_Retry, guard);
        } else {
            Q_EMIT askUserSkipResult(KIO::Result_Cancel, guard);
        }
    });
}

void Asker::askUserDelete(const QList<QUrl> &urls, DeletionType deletionType, ConfirmationType, QWidget *parent)
{
    if (deletionType == Trash) {
        Q_EMIT askUserDeleteResult(true, urls, deletionType, parent);
        return;
    }
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = deletionType == EmptyTrash ? QStringLiteral("Empty the Recycle Bin?") : QStringLiteral("Delete for good?");
    prompt[QStringLiteral("text")] = QStringLiteral("You cannot undo this.");
    prompt[QStringLiteral("primary")] = deletionType == EmptyTrash ? QStringLiteral("Empty Recycle Bin") : QStringLiteral("Delete for good");
    FileOps::instance()->ask(prompt, [this, urls, deletionType, parent](const QString &choice, bool) {
        Q_EMIT askUserDeleteResult(choice == QStringLiteral("primary"), urls, deletionType, parent);
    });
}

void Asker::requestUserMessageBox(MessageDialogType type, const QString &text, const QString &title, const QString &primaryActionText,
                                  const QString &secondaryActionText, const QString &, const QString &, const QString &,
                                  const QString &, QWidget *)
{
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = title;
    prompt[QStringLiteral("text")] = text;
    prompt[QStringLiteral("primary")] = primaryActionText.isEmpty() ? QStringLiteral("Continue") : primaryActionText;
    if (type == QuestionTwoActions || type == WarningTwoActions || !secondaryActionText.isEmpty()) {
        prompt[QStringLiteral("secondary")] = secondaryActionText;
    }
    prompt[QStringLiteral("informational")] = type == Information || type == Error;
    FileOps::instance()->ask(prompt, [this](const QString &choice, bool) {
        // KMessageDialog: PrimaryAction 3, SecondaryAction 4, Cancel 2.
        Q_EMIT messageBoxResult(choice == QStringLiteral("primary") ? 3 : choice == QStringLiteral("secondary") ? 4 : 2);
    });
}

void Asker::askIgnoreSslErrors(const QVariantMap &sslErrorData, QWidget *)
{
    // Windows asks before going on to a server whose certificate does not
    // check out, and so does Files, instead of refusing without a word.
    const QString host = sslErrorData.value(QStringLiteral("hostname")).toString();
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = QStringLiteral("This connection may not be safe");
    prompt[QStringLiteral("text")] = (host.isEmpty() ? QStringLiteral("The server's") : host + QStringLiteral("'s"))
        + QStringLiteral(" security certificate cannot be checked, so someone could be listening in. Only carry on if you trust this place.");
    prompt[QStringLiteral("primary")] = QStringLiteral("Connect anyway");
    FileOps::instance()->ask(prompt, [this](const QString &choice, bool) {
        Q_EMIT askIgnoreSslErrorsResult(choice == QStringLiteral("primary") ? 1 : 0);
    });
}

OpenWithAsker::OpenWithAsker(QObject *parent)
    : KIO::OpenWithHandlerInterface(parent)
{
}

void OpenWithAsker::promptUserForApplication(KJob *, const QList<QUrl> &urls, const QString &)
{
    Q_EMIT canceled();
    Q_EMIT FileOps::instance()->chooseAppRequested(urls);
}

UntrustedAsker::UntrustedAsker(QObject *parent)
    : KIO::UntrustedProgramHandlerInterface(parent)
{
}

void UntrustedAsker::showUntrustedProgramWarning(KJob *, const QString &programName)
{
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = QStringLiteral("Run %1?").arg(programName);
    prompt[QStringLiteral("text")] = QStringLiteral("This shortcut starts a program and did not come with Wild OS or the Store. Only run it if you know where it came from.");
    prompt[QStringLiteral("primary")] = QStringLiteral("Run");
    FileOps::instance()->ask(prompt, [this](const QString &choice, bool) {
        Q_EMIT result(choice == QStringLiteral("primary"));
    });
}

FileOps *FileOps::instance()
{
    static FileOps *ops = new FileOps;
    return ops;
}

FileOps *FileOps::create(QQmlEngine *, QJSEngine *)
{
    auto ops = instance();
    QJSEngine::setObjectOwnership(ops, QJSEngine::CppOwnership);
    return ops;
}

FileOps::FileOps(QObject *parent)
    : QObject(parent)

    , m_sharer(new Sharer(this))
    , m_bin(new KCoreDirLister(this))
{
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &FileOps::updateCut);
    updateCut();

    connect(m_sharer, &Sharer::finished, this, &FileOps::notice);
    connect(m_sharer, &Sharer::failed, this, &FileOps::failed);
    connect(Drives::instance(), &Drives::failed, this, &FileOps::failed);
    connect(Drives::instance(), &Drives::needsFormat, this, &FileOps::needsFormat);

    // A quiet listing of the Recycle Bin keeps its icon and Empty Bin current.
    m_bin->setAutoErrorHandlingEnabled(false);
    m_bin->setShowHiddenFiles(true);
    auto recount = [this] {
        const int n = int(m_bin->items().size());
        if (n != m_binCount) {
            m_binCount = n;
            Q_EMIT binCountChanged();
        }
    };
    connect(m_bin, &KCoreDirLister::newItems, this, recount);
    connect(m_bin, &KCoreDirLister::itemsDeleted, this, recount);
    connect(m_bin, &KCoreDirLister::clear, this, recount);
    connect(m_bin, &KCoreDirLister::completed, this, recount);
    m_bin->openUrl(QUrl(QStringLiteral("trash:/")));
}

void FileOps::setActiveWindow(QObject *window)
{
    if (window != m_activeWindow) {
        m_activeWindow = window;
        Q_EMIT activeWindowChanged();
    }
}

void FileOps::ask(const QVariantMap &prompt, std::function<void(const QString &, bool)> reply)
{
    m_prompts.append(prompt);
    m_replies.append(std::move(reply));
    if (m_prompts.size() == 1) {
        Q_EMIT promptChanged();
    }
}

void FileOps::answer(const QString &choice, bool applyToAll)
{
    if (m_prompts.isEmpty()) {
        return;
    }
    m_prompts.removeFirst();
    auto reply = m_replies.takeFirst();
    Q_EMIT promptChanged();
    reply(choice, applyToAll);
}

void FileOps::watchJob(KJob *job, const QString &verb)
{
    // Long copies show their progress in the notification area.
    if (auto tracker = KIO::getJobTracker()) {
        tracker->registerJob(job);
    }
    connect(job, &KJob::result, this, [this, job, verb] {
        if (!job->error() || job->error() == KIO::ERR_USER_CANCELED || job->error() == KJob::KilledJobError) {
            return;
        }
        QString message = job->errorString();
        switch (job->error()) {
        case KIO::ERR_ACCESS_DENIED:
        case KIO::ERR_WRITE_ACCESS_DENIED:
        case KIO::ERR_CANNOT_WRITE:
            message = QStringLiteral("You do not have permission to %1 here.").arg(verb);
            break;
        case KIO::ERR_DISK_FULL:
            message = QStringLiteral("There is not enough free space to %1 this.").arg(verb);
            break;
        case KIO::ERR_DOES_NOT_EXIST:
            message = QStringLiteral("The item is no longer there. It may have been moved or deleted.");
            break;
        default:
            break;
        }
        Q_EMIT failed(message);
    });
}

void FileOps::open(const QList<QUrl> &urls)
{
    for (const QUrl &url : urls) {
        // A Windows program goes to the app that runs Windows programs (App
        // Bridge). KIO counts an .exe as something that would run itself and
        // refuses it outright when executables may not run, rather than
        // passing it to the app registered for it.
        const QMimeType type = QMimeDatabase().mimeTypeForUrl(url);
        if (type.inherits(QStringLiteral("application/x-ms-dos-executable")) || type.inherits(QStringLiteral("application/x-msi"))
            || type.inherits(QStringLiteral("application/x-msdownload")) || type.inherits(QStringLiteral("application/vnd.microsoft.portable-executable"))) {
            if (const KService::Ptr runner = KApplicationTrader::preferredService(type.name())) {
                auto job = new KIO::ApplicationLauncherJob(runner);
                job->setUrls({url});
                job->setUiDelegate(delegate());
                job->start();
                continue;
            }
        }
        // A downloaded program asks first, as Explorer does, instead of
        // KIO refusing it outright.
        if (isLinuxProgram(url, type)) {
            askToRun(url);
            continue;
        }
        // A script opens in its app rather than running; KIO's own question
        // is a stock widget dialog.
        auto job = new KIO::OpenUrlJob(url);
        job->setUiDelegate(delegate());
        // App shortcuts (.desktop files) are the exception: KIO only starts
        // them when executables may run, and still checks they are trusted
        // (UntrustedAsker) before it does.
        const bool shortcut = url.isLocalFile() && QMimeDatabase().mimeTypeForUrl(url).name() == QLatin1String("application/x-desktop");
        job->setRunExecutables(shortcut);
        reportLaunch(job);
        job->start();
    }
}

void FileOps::askToRun(const QUrl &url)
{
    const QString path = url.toLocalFile();
    const QString name = url.fileName();
    const quint16 machine = elfMachine(path);
    if (machine != 0 && hostMachine != 0 && machine != hostMachine) {
        Q_EMIT failed(QStringLiteral("%1 is made for a different type of processor, so it cannot run on this PC. "
                                     "Download the version marked x86_64 or amd64 instead.").arg(name));
        return;
    }
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = QStringLiteral("Run %1?").arg(name);
    prompt[QStringLiteral("text")] = QStringLiteral("This is a program that did not come with Wild OS or the Store, and nothing has checked it. "
                                                    "It can open and change any of your files. Only run it if you trust where it came from.");
    prompt[QStringLiteral("primary")] = QStringLiteral("Run");
    // With the safe box on, it can be tried sealed off first.
    const bool box = safeBoxPlan(url).value(QStringLiteral("ok")).toBool();
    if (box) {
        prompt[QStringLiteral("secondary")] = QStringLiteral("Run in safe box");
    }
    prompt[QStringLiteral("cancelDefault")] = true;
    ask(prompt, [this, url, path, name](const QString &choice, bool) {
        if (choice == QLatin1String("secondary")) {
            openInSafeBox(url);
            return;
        }
        if (choice != QLatin1String("primary")) {
            return;
        }
        // Downloads arrive without the execute bit. Drives formatted for
        // Windows cannot keep it, and setPermissions can report success there.
        if (!QFileInfo(path).isExecutable()) {
            QFile file(path);
            file.setPermissions(file.permissions() | QFileDevice::ExeOwner);
        }
        if (!QFileInfo(path).isExecutable()) {
            Q_EMIT failed(QStringLiteral("%1 cannot run from here. Copy it to a folder in Home, such as Downloads, and try again.").arg(name));
            return;
        }
        // The executable and arguments overload quotes the path. A bare {}
        // picked the shell command one, which splits names with spaces or
        // brackets, such as a second download saved as "app (1).AppImage".
        auto job = new KIO::CommandLauncherJob(path, QStringList());
        job->setWorkingDirectory(QFileInfo(path).absolutePath());
        reportLaunch(job);
        job->start();
    });
}

QVariantList FileOps::appsFor(const QUrl &url) const
{
    QVariantList list;
    const QString mime = QMimeDatabase().mimeTypeForUrl(url).name();
    // The app that opens it on a double click comes first, marked Default.
    const KService::Ptr preferred = KApplicationTrader::preferredService(mime);
    const auto services = KApplicationTrader::queryByMimeType(mime);
    for (const KService::Ptr &service : services) {
        if (!service->noDisplay() && list.size() < 8) {
            QVariantMap entry = appEntry(service);
            const bool isDefault = preferred && service->storageId() == preferred->storageId();
            entry[QStringLiteral("isDefault")] = isDefault;
            if (isDefault) {
                list.prepend(entry);
            } else {
                list.append(entry);
            }
        }
    }
    return list;
}

QVariantList FileOps::allApps() const
{
    QVariantList list;
    auto services = KApplicationTrader::query([](const KService::Ptr &service) {
        return !service->noDisplay() && !service->exec().isEmpty();
    });
    std::sort(services.begin(), services.end(), [](const KService::Ptr &a, const KService::Ptr &b) {
        return a->name().compare(b->name(), Qt::CaseInsensitive) < 0;
    });
    for (const KService::Ptr &service : std::as_const(services)) {
        list.append(appEntry(service));
    }
    return list;
}

QString FileOps::defaultAppName(const QUrl &url) const
{
    const QString mime = QMimeDatabase().mimeTypeForUrl(url).name();
    const KService::Ptr service = KApplicationTrader::preferredService(mime);
    return service ? service->name() : QString();
}

void FileOps::openWith(const QList<QUrl> &urls, const QString &appId, bool makeDefault)
{
    const KService::Ptr service = KService::serviceByStorageId(appId);
    if (!service || urls.isEmpty()) {
        return;
    }
    if (makeDefault) {
        const QString mime = QMimeDatabase().mimeTypeForUrl(urls.constFirst()).name();
        KApplicationTrader::setPreferredService(mime, service);
    }
    auto job = new KIO::ApplicationLauncherJob(service);
    job->setUrls(urls);
    job->setUiDelegate(delegate());
    reportLaunch(job);
    job->start();
}

void FileOps::openTerminal(const QUrl &folder)
{
    // The terminal chosen in kdeglobals: Wild OS sets its own, which is
    // started with --workdir. KTerminalLauncherJob only passes the folder to
    // Konsole, and falls back to Konsole whenever the TerminalService entry
    // is not in the app cache yet, so Wild OS Terminal is started directly.
    const QString path = folder.toLocalFile();
    const KConfigGroup general(KSharedConfig::openConfig(QStringLiteral("kdeglobals")), QStringLiteral("General"));
    const bool ours = general.readEntry("TerminalService") == QLatin1String("org.wildos.terminal.desktop")
        || general.readEntry("TerminalApplication").section(QLatin1Char(' '), 0, 0).endsWith(QLatin1String("wildos-terminal"));
    if (ours && !QStandardPaths::findExecutable(QStringLiteral("wildos-terminal")).isEmpty()) {
        auto job = new KIO::CommandLauncherJob(QStringLiteral("wildos-terminal"), {QStringLiteral("--workdir"), path});
        job->setDesktopName(QStringLiteral("org.wildos.terminal"));
        job->setWorkingDirectory(path);
        reportLaunch(job);
        job->start();
        return;
    }
    auto job = new KTerminalLauncherJob(QString());
    job->setWorkingDirectory(path);
    reportLaunch(job);
    job->start();
}

bool FileOps::canPaste() const
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    return data && data->hasUrls();
}

void FileOps::updateCut()
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    QSet<QUrl> cut;
    if (data && data->hasUrls() && KIO::isClipboardDataCut(data)) {
        const auto urls = KUrlMimeData::urlsFromMimeData(data);
        cut = QSet<QUrl>(urls.cbegin(), urls.cend());
    }
    m_cut = cut;
    Q_EMIT clipboardChanged();
}

void FileOps::copy(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    auto data = new QMimeData;
    KUrlMimeData::setUrls(urls, urls, data);
    KIO::setClipboardDataCut(data, false);
    QGuiApplication::clipboard()->setMimeData(data);
}

void FileOps::cut(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    auto data = new QMimeData;
    KUrlMimeData::setUrls(urls, urls, data);
    KIO::setClipboardDataCut(data, true);
    QGuiApplication::clipboard()->setMimeData(data);
}

void FileOps::copyPaths(const QList<QUrl> &urls)
{
    QStringList paths;
    for (const QUrl &url : urls) {
        paths.append(QLatin1Char('"') + (url.isLocalFile() ? url.toLocalFile() : url.toDisplayString()) + QLatin1Char('"'));
    }
    QGuiApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
}

void FileOps::paste(const QUrl &folder)
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data || !data->hasUrls()) {
        return;
    }
    const QList<QUrl> urls = KUrlMimeData::urlsFromMimeData(data);
    const bool move = KIO::isClipboardDataCut(data);
    transfer(urls, folder, move);
    if (move) {
        // A cut can only be pasted once, as on Windows.
        QGuiApplication::clipboard()->clear();
    }
}

int FileOps::dropAction(const QList<QUrl> &urls, const QUrl &folder, int modifiers) const
{
    if (modifiers & Qt::ControlModifier) {
        return Qt::CopyAction;
    }
    if (modifiers & Qt::ShiftModifier) {
        return Qt::MoveAction;
    }
    if (urls.isEmpty() || !folder.isLocalFile() || folder.scheme() == QStringLiteral("trash")) {
        return folder.scheme() == QStringLiteral("trash") ? Qt::MoveAction : Qt::CopyAction;
    }
    // Windows moves within a drive and copies between drives.
    const QStorageInfo target(folder.toLocalFile());
    for (const QUrl &url : urls) {
        if (!url.isLocalFile() || QStorageInfo(url.toLocalFile()).rootPath() != target.rootPath()) {
            return Qt::CopyAction;
        }
    }
    return Qt::MoveAction;
}

void FileOps::drop(const QList<QUrl> &urls, const QUrl &folder, int action)
{
    if (urls.isEmpty()) {
        return;
    }
    if (folder.scheme() == QStringLiteral("trash")) {
        trash(urls);
        return;
    }
    QList<QUrl> moving;
    for (const QUrl &url : urls) {
        // Dropping a folder onto itself, or an item where it already is,
        // does nothing.
        if (url == folder || KIO::upUrl(url).adjusted(QUrl::StripTrailingSlash) == folder.adjusted(QUrl::StripTrailingSlash)) {
            if (action == Qt::MoveAction) {
                continue;
            }
        }
        if (url.isParentOf(folder)) {
            continue;
        }
        moving.append(url);
    }
    if (!moving.isEmpty()) {
        transfer(moving, folder, action == Qt::MoveAction);
    }
}

void FileOps::transfer(const QList<QUrl> &urls, const QUrl &folder, bool move)
{
    if (urls.isEmpty()) {
        return;
    }
    KIO::CopyJob *job = move ? KIO::move(urls, folder) : KIO::copy(urls, folder);
    job->setUiDelegate(delegate());
    // Pasting a copy into the folder it came from makes "Name (1)", the
    // nearest thing to Windows' "Name - Copy" without asking.
    bool sameFolder = !move;
    for (const QUrl &url : urls) {
        if (KIO::upUrl(url).adjusted(QUrl::StripTrailingSlash) != folder.adjusted(QUrl::StripTrailingSlash)) {
            sameFolder = false;
        }
    }
    job->setAutoRename(sameFolder);
    auto made = std::make_shared<QList<QUrl>>();
    connect(job, &KIO::CopyJob::copyingDone, this, [made](KIO::Job *, const QUrl &, const QUrl &to, const QDateTime &, bool, bool) {
        made->append(to);
    });
    connect(job, &KIO::CopyJob::copyingLinkDone, this, [made](KIO::Job *, const QUrl &, const QString &, const QUrl &to) {
        made->append(to);
    });
    connect(job, &KJob::result, this, [this, made, folder] {
        QList<QUrl> top;
        for (const QUrl &url : std::as_const(*made)) {
            if (KIO::upUrl(url).adjusted(QUrl::StripTrailingSlash) == folder.adjusted(QUrl::StripTrailingSlash)) {
                top.append(url);
            }
        }
        if (!top.isEmpty()) {
            Q_EMIT created(folder, top, false);
        }
    });
    watchJob(job, move ? QStringLiteral("move files") : QStringLiteral("copy files"));
}

void FileOps::trash(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    QList<QUrl> local;
    QList<QUrl> other;
    for (const QUrl &url : urls) {
        (url.isLocalFile() ? local : other).append(url);
    }
    if (!local.isEmpty()) {
        auto job = KIO::trash(local);
        job->setUiDelegate(delegate());
        if (auto tracker = KIO::getJobTracker()) {
            tracker->registerJob(job);
        }
        connect(job, &KJob::result, this, [this, job, local] {
            if (!job->error() || job->error() == KIO::ERR_USER_CANCELED) {
                return;
            }
            // Some drives have no Recycle Bin. Windows asks before deleting for
            // good instead, and so do we.
            QVariantMap prompt;
            prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
            prompt[QStringLiteral("title")] = QStringLiteral("Delete for good?");
            prompt[QStringLiteral("text")] = local.size() == 1
                ? QStringLiteral("This item cannot go to the Recycle Bin, so it would be deleted for good. You cannot undo this.")
                : QStringLiteral("These items cannot go to the Recycle Bin, so they would be deleted for good. You cannot undo this.");
            prompt[QStringLiteral("primary")] = QStringLiteral("Delete for good");
            ask(prompt, [this, local](const QString &choice, bool) {
                if (choice == QStringLiteral("primary")) {
                    auto del = KIO::del(local);
                    watchJob(del, QStringLiteral("delete files"));
                }
            });
        });
    }
    if (!other.isEmpty()) {
        deleteForGood(other);
    }
}

void FileOps::deleteForGood(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = urls.size() == 1 ? QStringLiteral("Delete this item for good?") : QStringLiteral("Delete %1 items for good?").arg(urls.size());
    prompt[QStringLiteral("text")] = QStringLiteral("It will not go to the Recycle Bin, and you cannot undo this.");
    prompt[QStringLiteral("primary")] = QStringLiteral("Delete for good");
    ask(prompt, [this, urls](const QString &choice, bool) {
        if (choice == QStringLiteral("primary")) {
            auto job = KIO::del(urls);
            watchJob(job, QStringLiteral("delete files"));
        }
    });
}

void FileOps::restore(const QList<QUrl> &urls)
{
    if (urls.isEmpty()) {
        return;
    }
    auto job = KIO::restoreFromTrash(urls);
    job->setUiDelegate(delegate());
    watchJob(job, QStringLiteral("restore files"));
}

void FileOps::emptyBin()
{
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = QStringLiteral("Empty the Recycle Bin?");
    prompt[QStringLiteral("text")] = m_binCount == 1 ? QStringLiteral("The item in the Recycle Bin will be deleted for good. You cannot undo this.")
                                                     : QStringLiteral("All %1 items in the Recycle Bin will be deleted for good. You cannot undo this.").arg(m_binCount);
    prompt[QStringLiteral("primary")] = QStringLiteral("Empty Recycle Bin");
    ask(prompt, [this](const QString &choice, bool) {
        if (choice == QStringLiteral("primary")) {
            auto job = KIO::emptyTrash();
            watchJob(job, QStringLiteral("empty the Recycle Bin"));
        }
    });
}

QString FileOps::editableName(const QUrl &, const QString &fileName, bool isDir) const
{
    return Format::visibleName(fileName, isDir, Settings::instance()->showExtensions());
}

void FileOps::rename(const QUrl &url, const QString &fileName, bool isDir, const QString &typed)
{
    const QString visible = Format::visibleName(fileName, isDir, Settings::instance()->showExtensions());
    QString name = typed.trimmed();
    if (name.isEmpty() || name == visible) {
        return;
    }
    if (name.contains(QLatin1Char('/')) || name == QStringLiteral(".") || name == QStringLiteral("..")) {
        Q_EMIT failed(QStringLiteral("A name cannot contain / or be just dots. Try a different name."));
        return;
    }
    // The extension hidden from view stays on the file.
    name += fileName.mid(visible.size());
    const QUrl folder = KIO::upUrl(url);
    QUrl target = folder;
    target.setPath(folder.path() + (folder.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + name);
    if (target.isLocalFile() && QFileInfo::exists(target.toLocalFile()) && target.toLocalFile().compare(url.toLocalFile(), Qt::CaseSensitive) != 0) {
        Q_EMIT failed(QStringLiteral("There is already an item called \"%1\" here. Pick a different name.").arg(typed.trimmed()));
        return;
    }
    auto job = KIO::moveAs(url, target, KIO::HideProgressInfo);
    job->setUiDelegate(delegate());
    connect(job, &KJob::result, this, [this, job, folder, target] {
        if (!job->error()) {
            Q_EMIT created(folder, {target}, false);
        }
    });
    watchJob(job, QStringLiteral("rename this"));
}

QString FileOps::uniqueName(const QUrl &folder, const QString &base, const QString &suffix) const
{
    if (!folder.isLocalFile()) {
        return base + suffix;
    }
    const QDir dir(folder.toLocalFile());
    QString name = base + suffix;
    for (int n = 2; dir.exists(name); ++n) {
        name = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
    }
    return name;
}

void FileOps::newFolder(const QUrl &folder)
{
    const QString name = uniqueName(folder, QStringLiteral("New folder"), QString());
    QUrl url = folder;
    url.setPath(folder.path() + (folder.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + name);
    auto job = KIO::mkdir(url);
    connect(job, &KJob::result, this, [this, job, folder, url] {
        if (!job->error()) {
            Q_EMIT created(folder, {url}, true);
        }
    });
    watchJob(job, QStringLiteral("make a folder"));
}

QVariantList FileOps::shareTargets(const QList<QUrl> &urls)
{
    return m_sharer->targets(urls);
}

void FileOps::share(int index)
{
    m_sharer->share(index);
}

void FileOps::ejectDrive(const QString &udi)
{
    Drives::instance()->eject(udi);
}

KJobUiDelegate *FileOps::delegate()
{
    // KIO finds the question asker as a direct child of the job's delegate.
    auto ui = new KJobUiDelegate(KJobUiDelegate::Flags{});
    new Asker(ui);
    new OpenWithAsker(ui);
    new UntrustedAsker(ui);
    return ui;
}

void FileOps::reportLaunch(KJob *job)
{
    // Failures show in the window's own dialog, not a KMessageBox.
    connect(job, &KJob::result, this, [this](KJob *job) {
        if (job->error() == 0 || job->error() == KIO::ERR_USER_CANCELED) {
            return;
        }
        QString message = job->errorString();
        if (job->error() == KIO::ERR_DOES_NOT_EXIST) {
            message = QStringLiteral("The item is no longer there. It may have been moved or deleted.");
        } else if (message.isEmpty()) {
            message = QStringLiteral("Wild OS could not open this.");
        }
        Q_EMIT failed(message);
    });
}
