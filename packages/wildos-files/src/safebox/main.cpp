// wildos-safebox FILE: opens a file, or runs a program, in a bubblewrap
// sandbox that is thrown away when it closes (Settings > Privacy and
// security > Advanced protection > Safe box).
//
// Inside the box: the system read-only, an empty Home in memory holding only
// the file, no network (its own empty network namespace), no other
// processes, a /dev with no hardware in it, and of the person's session only
// the Wayland socket, so it can show a window. No D-Bus, no X11, no sound,
// no camera. A seccomp filter turns down the system calls that only serve to
// reach the kernel's less exposed corners, and new user namespaces are
// refused, so nothing inside can build a sandbox of its own to escape from.
//
// What it can't stop: a flaw in the Linux kernel or in KWin could still be
// used to get out, and a window in front can read what was copied.

#include "safeboxplan.h"

#include <KIO/DesktopExecParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include <cerrno>
#include <sched.h>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <seccomp.h>
#include <sys/ioctl.h>
#include <cstring>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace
{
void notify(const QString &title, const QString &body)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                                          QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    message << QStringLiteral("Safe box") << uint(0) << QStringLiteral("security-high") << title << body << QStringList() << QVariantMap() << int(-1);
    QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
}

int fail(const QString &message)
{
    std::fprintf(stderr, "%s\n", qPrintable(message));
    notify(QStringLiteral("The safe box couldn't open"), message);
    return 3;
}

// Built in memory and handed to bwrap, which loads it just before the
// program starts. Calls that are turned down fail with EPERM, as they would
// for an unprivileged program, rather than killing it.
int seccompFilter()
{
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ALLOW);
    if (!ctx) {
        return -1;
    }
    // 32-bit programs reach the same kernel through other numbers.
    seccomp_arch_add(ctx, SCMP_ARCH_X86);
    seccomp_arch_add(ctx, SCMP_ARCH_X32);
    const int denied[] = {
        // The kernel keyring is the person's, not the box's.
        SCMP_SYS(add_key), SCMP_SYS(keyctl), SCMP_SYS(request_key),
        // Large, often exploited interfaces no ordinary app needs.
        SCMP_SYS(bpf), SCMP_SYS(perf_event_open), SCMP_SYS(userfaultfd),
        SCMP_SYS(io_uring_setup), SCMP_SYS(io_uring_enter), SCMP_SYS(io_uring_register),
        SCMP_SYS(kexec_load), SCMP_SYS(kexec_file_load), SCMP_SYS(init_module), SCMP_SYS(finit_module), SCMP_SYS(delete_module),
        SCMP_SYS(mount), SCMP_SYS(umount2), SCMP_SYS(pivot_root), SCMP_SYS(move_mount), SCMP_SYS(open_tree), SCMP_SYS(fsopen),
        SCMP_SYS(fsmount), SCMP_SYS(fsconfig), SCMP_SYS(fspick), SCMP_SYS(mount_setattr),
        SCMP_SYS(unshare), SCMP_SYS(setns),
        SCMP_SYS(acct), SCMP_SYS(swapon), SCMP_SYS(swapoff), SCMP_SYS(reboot), SCMP_SYS(syslog), SCMP_SYS(quotactl),
        SCMP_SYS(uselib), SCMP_SYS(lookup_dcookie), SCMP_SYS(name_to_handle_at), SCMP_SYS(open_by_handle_at),
        SCMP_SYS(vhangup), SCMP_SYS(ptrace), SCMP_SYS(process_vm_readv), SCMP_SYS(process_vm_writev),
    };
    for (int call : denied) {
        // A call this processor does not have is skipped by libseccomp.
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), call, 0);
    }
    // Typing into the terminal the box was started from.
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(ioctl), 1, SCMP_A1(SCMP_CMP_MASKED_EQ, 0xFFFFFFFFu, TIOCSTI));
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(ioctl), 1, SCMP_A1(SCMP_CMP_MASKED_EQ, 0xFFFFFFFFu, TIOCLINUX));
    // clone with a new namespace flag; bwrap's --disable-userns covers
    // user namespaces, this covers the rest. clone3 keeps its flags in
    // memory, where a filter can't look, so it gets ENOSYS and libc falls
    // back to clone.
    for (unsigned long flag : {CLONE_NEWNS, CLONE_NEWCGROUP, CLONE_NEWUTS, CLONE_NEWIPC, CLONE_NEWUSER, CLONE_NEWPID, CLONE_NEWNET}) {
        seccomp_rule_add(ctx, SCMP_ACT_ERRNO(EPERM), SCMP_SYS(clone), 1, SCMP_A0(SCMP_CMP_MASKED_EQ, flag, flag));
    }
    seccomp_rule_add(ctx, SCMP_ACT_ERRNO(ENOSYS), SCMP_SYS(clone3), 0);

    const int fd = memfd_create("wildos-safebox-seccomp", 0);
    if (fd < 0 || seccomp_export_bpf(ctx, fd) != 0 || lseek(fd, 0, SEEK_SET) != 0) {
        seccomp_release(ctx);
        return -1;
    }
    seccomp_release(ctx);
    return fd;
}

// /bin, /lib and the rest as the system has them: links into /usr on a
// merged system, read-only folders otherwise.
void hostLinkOrDir(QStringList &args, const QString &top)
{
    const QFileInfo info(top);
    if (info.isSymLink()) {
        args << QStringLiteral("--symlink") << info.symLinkTarget().mid(1) << top;
    } else if (info.isDir()) {
        args << QStringLiteral("--ro-bind") << top << top;
    }
}
}

// Inside the box, first in line: runs the app and stays until every process
// it started has ended. Apps that hand over to a process of their own and
// quit, as Write and Tables do with LibreOffice, would otherwise take the
// whole box down with them, since the box ends when its first process does.
// Returns the app's own exit code.
int reap(char *argv[])
{
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    const pid_t app = fork();
    if (app < 0) {
        return 127;
    }
    if (app == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int code = 0;
    for (;;) {
        int wstatus = 0;
        const pid_t done = wait(&wstatus);
        if (done < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (done == app) {
            code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
        }
    }
    return code;
}

int main(int argc, char *argv[])
{
    if (argc > 3 && std::strcmp(argv[1], "--reap") == 0 && std::strcmp(argv[2], "--") == 0) {
        return reap(argv + 3);
    }
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("wildos-safebox"));
    const QStringList arguments = app.arguments();
    if (arguments.size() != 2 || arguments.at(1).startsWith(QLatin1Char('-'))) {
        std::fprintf(stderr, "usage: wildos-safebox FILE\n");
        return 2;
    }
    const QFileInfo file(arguments.at(1));
    const SafeBoxPlan plan = safeBoxPlan(file.absoluteFilePath());
    if (!plan.ok) {
        return fail(plan.message);
    }

    const QString home = QDir::homePath();
    const QString runtime = QStringLiteral("/run/user/%1").arg(getuid());
    QString wayland = qEnvironmentVariable("WAYLAND_DISPLAY", QStringLiteral("wayland-0"));
    if (!wayland.startsWith(QLatin1Char('/'))) {
        wayland = qEnvironmentVariable("XDG_RUNTIME_DIR", runtime) + QLatin1Char('/') + wayland;
    }
    if (!QFileInfo::exists(wayland)) {
        return fail(QStringLiteral("The safe box needs the desktop's Wayland display, and it isn't there."));
    }

    // The file inside: a program is copied into the box, so it can be made
    // runnable there without touching the original; a document is put there
    // read-only, so the app can read it and nothing can change it.
    const QString inside = home + QLatin1Char('/') + file.fileName();
    int programFd = -1;
    if (plan.program) {
        programFd = open(QFile::encodeName(file.absoluteFilePath()).constData(), O_RDONLY | O_CLOEXEC);
        if (programFd < 0) {
            return fail(QStringLiteral("%1 can't be read.").arg(file.fileName()));
        }
    }

    QStringList command;
    if (plan.program) {
        command << inside;
    } else {
        KIO::DesktopExecParser parser(*plan.app, {QUrl::fromLocalFile(inside)});
        command = parser.resultingArguments();
        if (command.isEmpty()) {
            return fail(QStringLiteral("%1 can't be started in the safe box.").arg(plan.appName));
        }
        // Chromium-based apps build their own sandbox from user namespaces
        // or a setuid helper, neither of which the box allows, and refuse to
        // start without it. The box is the sandbox here.
        const QString program = QFileInfo(command.first()).fileName();
        if (program.contains(QLatin1String("brave")) || program.contains(QLatin1String("chrom")) || program.contains(QLatin1String("electron"))) {
            command.insert(1, QStringLiteral("--no-sandbox"));
        }
    }

    const int seccomp = seccompFilter();
    if (seccomp < 0) {
        return fail(QStringLiteral("The safe box's system call filter couldn't be set up."));
    }
    int status[2];
    if (pipe2(status, O_CLOEXEC) != 0) {
        return fail(QStringLiteral("The safe box couldn't be set up."));
    }

    QStringList args{
        QStringLiteral("--unshare-all"),
        QStringLiteral("--unshare-user"),
        QStringLiteral("--disable-userns"),
        QStringLiteral("--die-with-parent"),
        QStringLiteral("--new-session"),
        QStringLiteral("--cap-drop"), QStringLiteral("ALL"),
        QStringLiteral("--hostname"), QStringLiteral("safe-box"),
        QStringLiteral("--ro-bind"), QStringLiteral("/usr"), QStringLiteral("/usr"),
        QStringLiteral("--ro-bind"), QStringLiteral("/etc"), QStringLiteral("/etc"),
        QStringLiteral("--ro-bind-try"), QStringLiteral("/opt"), QStringLiteral("/opt"),
        QStringLiteral("--proc"), QStringLiteral("/proc"),
        QStringLiteral("--dev"), QStringLiteral("/dev"),
        QStringLiteral("--tmpfs"), QStringLiteral("/tmp"),
        QStringLiteral("--tmpfs"), QStringLiteral("/run"),
        QStringLiteral("--dir"), QStringLiteral("/var"),
        QStringLiteral("--tmpfs"), QStringLiteral("/var/tmp"),
        QStringLiteral("--ro-bind-try"), QStringLiteral("/var/cache/fontconfig"), QStringLiteral("/var/cache/fontconfig"),
        QStringLiteral("--dir"), runtime,
        QStringLiteral("--chmod"), QStringLiteral("0700"), runtime,
        QStringLiteral("--ro-bind"), wayland, runtime + QStringLiteral("/wayland-0"),
        QStringLiteral("--tmpfs"), home,
        QStringLiteral("--chmod"), QStringLiteral("0700"), home,
    };
    for (const char *top : {"/bin", "/sbin", "/lib", "/lib64", "/lib32", "/libx32"}) {
        hostLinkOrDir(args, QString::fromLatin1(top));
    }
    if (plan.program) {
        args << QStringLiteral("--perms") << QStringLiteral("0700") << QStringLiteral("--file") << QString::number(programFd) << inside;
    } else {
        args << QStringLiteral("--ro-bind") << file.absoluteFilePath() << inside;
    }
    args << QStringLiteral("--chdir") << home;

    // Only what an app needs to draw itself in the person's language and
    // look; nothing that points at the session's services.
    args << QStringLiteral("--clearenv");
    const auto setenv = [&args](const QString &name, const QString &value) {
        args << QStringLiteral("--setenv") << name << value;
    };
    for (const char *name : {"LANG", "LANGUAGE", "LC_ALL", "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY", "LC_MESSAGES",
                             "LC_PAPER", "LC_NAME", "LC_ADDRESS", "LC_TELEPHONE", "LC_MEASUREMENT", "XDG_CURRENT_DESKTOP", "KDE_FULL_SESSION",
                             "KDE_SESSION_VERSION", "QT_QPA_PLATFORMTHEME", "QT_STYLE_OVERRIDE", "XDG_DATA_DIRS", "XDG_CONFIG_DIRS", "TZ",
                             "XCURSOR_THEME", "XCURSOR_SIZE", "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "GDK_SCALE"}) {
        if (qEnvironmentVariableIsSet(name)) {
            setenv(QString::fromLatin1(name), qEnvironmentVariable(name));
        }
    }
    setenv(QStringLiteral("HOME"), home);
    setenv(QStringLiteral("USER"), qEnvironmentVariable("USER"));
    setenv(QStringLiteral("LOGNAME"), qEnvironmentVariable("USER"));
    setenv(QStringLiteral("PATH"), QStringLiteral("/usr/local/bin:/usr/bin:/bin"));
    setenv(QStringLiteral("TMPDIR"), QStringLiteral("/tmp"));
    setenv(QStringLiteral("XDG_RUNTIME_DIR"), runtime);
    setenv(QStringLiteral("WAYLAND_DISPLAY"), QStringLiteral("wayland-0"));
    setenv(QStringLiteral("XDG_SESSION_TYPE"), QStringLiteral("wayland"));
    setenv(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));
    setenv(QStringLiteral("GDK_BACKEND"), QStringLiteral("wayland"));
    setenv(QStringLiteral("SDL_VIDEODRIVER"), QStringLiteral("wayland"));
    setenv(QStringLiteral("MOZ_ENABLE_WAYLAND"), QStringLiteral("1"));
    setenv(QStringLiteral("ELECTRON_OZONE_PLATFORM_HINT"), QStringLiteral("wayland"));
    // There are no graphics devices in the box, so drawing is done in
    // software.
    setenv(QStringLiteral("LIBGL_ALWAYS_SOFTWARE"), QStringLiteral("1"));
    // AppImages normally mount themselves with FUSE, which the box has no
    // device for; this unpacks them into the box's memory instead.
    setenv(QStringLiteral("APPIMAGE_EXTRACT_AND_RUN"), QStringLiteral("1"));
    // LibreOffice's KDE integration needs the session's D-Bus and falls
    // back to X11, which the box has neither of; its plain Qt one draws on
    // Wayland.
    setenv(QStringLiteral("SAL_USE_VCLPLUGIN"), QStringLiteral("qt6"));
    // The same for Electron apps, AppImages included.
    setenv(QStringLiteral("ELECTRON_DISABLE_SANDBOX"), QStringLiteral("1"));
    // The accessibility bus is D-Bus, which the box has none of.
    setenv(QStringLiteral("NO_AT_BRIDGE"), QStringLiteral("1"));

    args << QStringLiteral("--seccomp") << QString::number(seccomp);
    args << QStringLiteral("--json-status-fd") << QString::number(status[1]);
    args << QStringLiteral("--") << QStringLiteral("/usr/bin/wildos-safebox") << QStringLiteral("--reap") << QStringLiteral("--") << command;

    QList<QByteArray> encoded;
    encoded << QByteArrayLiteral("bwrap");
    for (const QString &arg : std::as_const(args)) {
        encoded << QFile::encodeName(arg);
    }
    std::vector<char *> argvOut;
    for (QByteArray &a : encoded) {
        argvOut.push_back(a.data());
    }
    argvOut.push_back(nullptr);

    const time_t started = time(nullptr);
    const pid_t child = fork();
    if (child < 0) {
        return fail(QStringLiteral("The safe box couldn't be set up."));
    }
    if (child == 0) {
        // bwrap reads these by number, so they must survive exec.
        fcntl(seccomp, F_SETFD, 0);
        fcntl(status[1], F_SETFD, 0);
        if (programFd >= 0) {
            fcntl(programFd, F_SETFD, 0);
        }
        execvp("/usr/bin/bwrap", argvOut.data());
        _exit(127);
    }
    close(status[1]);
    close(seccomp);
    if (programFd >= 0) {
        close(programFd);
    }

    // bwrap reports {"child-pid": N} once the app is running inside, and
    // its exit code when it ends. No child-pid means the box never opened.
    QByteArray report;
    char buffer[512];
    ssize_t got;
    while ((got = read(status[0], buffer, sizeof buffer)) > 0 || (got < 0 && errno == EINTR)) {
        if (got > 0) {
            report.append(buffer, got);
        }
    }
    close(status[0]);
    int wstatus = 0;
    while (waitpid(child, &wstatus, 0) < 0 && errno == EINTR) {
    }
    const int code = WIFEXITED(wstatus) ? WEXITSTATUS(wstatus) : 128 + WTERMSIG(wstatus);
    if (!report.contains("child-pid")) {
        return fail(QStringLiteral("Wild OS couldn't make the sealed space for it."));
    }
    // An app that closes at once with an error most likely needs something
    // the box keeps out, such as the network or its own sandbox.
    if (code != 0 && time(nullptr) - started < 4) {
        notify(QStringLiteral("%1 closed straight away").arg(plan.appName),
               QStringLiteral("It may need something the safe box keeps out, such as the internet."));
    }
    return code;
}
