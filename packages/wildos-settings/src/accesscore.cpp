#include "accesscore.h"

#include <KConfig>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KSharedConfig>

#include <QColor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
const QString kwinService = QStringLiteral("org.kde.KWin");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString contrastScheme = QStringLiteral("WildOSHighContrast");
const QString monoUnit = QStringLiteral("wildos-mono-audio.service");

KSharedConfigPtr fresh(const QString &file)
{
    // Settings and the shortcut service both live for a whole session and
    // either can change a value the other has cached.
    KSharedConfigPtr config = KSharedConfig::openConfig(file, KConfig::NoGlobals);
    config->reparseConfiguration();
    return config;
}

template<typename T>
T readAccess(const char *group, const char *key, const T &fallback)
{
    return fresh(QStringLiteral("kaccessrc"))->group(QString::fromLatin1(group)).readEntry(key, fallback);
}

template<typename T>
void writeAccess(const char *group, const char *key, const T &value)
{
    KSharedConfigPtr config = fresh(QStringLiteral("kaccessrc"));
    config->group(QString::fromLatin1(group)).writeEntry(key, value, KConfig::Notify);
    config->sync();
}

// What kcm_access does after saving: kaccess applies the same choices to
// X11 apps under Xwayland, and rereads the file when started again.
void pokeKaccess()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("kaccess")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("kaccess"), {});
    }
}

bool kaccessRunning()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.interface() && bus.interface()->isServiceRegistered(QStringLiteral("org.kde.kaccess"));
}

QVariant getProperty(const QString &service, const QString &path, const QString &interface, const QString &name)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, properties, QStringLiteral("Get"));
    message << interface << name;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

void setProperty(const QString &service, const QString &path, const QString &interface, const QString &name, const QVariant &value)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, properties, QStringLiteral("Set"));
    message << interface << name << QVariant::fromValue(QDBusVariant(value));
    QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
}

void invokeKWinShortcut(const QString &name)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"),
                                                          QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("invokeShortcut"));
    message << name;
    QDBusConnection::sessionBus().asyncCall(message);
}

void runAfter(QProcess *process, QObject *context, const std::function<void()> &next)
{
    QObject::connect(process, &QProcess::finished, context, [process, next] {
        process->deleteLater();
        if (next) {
            next();
        }
    });
    QObject::connect(process, &QProcess::errorOccurred, context, [process, next](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            if (next) {
                next();
            }
        }
    });
}

// Orca has no command that stops it, and kaccess only clears the GNOME key
// it would be started by, which leaves a running Orca talking. Orca shuts
// down cleanly on SIGTERM.
void stopOrca()
{
    const uid_t me = ::getuid();
    const QStringList pids = QDir(QStringLiteral("/proc")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : pids) {
        bool ok = false;
        const int pid = entry.toInt(&ok);
        if (!ok) {
            continue;
        }
        struct stat info;
        if (::stat(QFile::encodeName(QStringLiteral("/proc/%1").arg(pid)).constData(), &info) != 0 || info.st_uid != me) {
            continue;
        }
        QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
        bool orca = comm.open(QIODevice::ReadOnly) && comm.readAll().trimmed() == "orca";
        if (!orca) {
            // Started as "python3 /usr/bin/orca" where the title could not
            // be set.
            QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
            if (cmdline.open(QIODevice::ReadOnly)) {
                const QList<QByteArray> args = cmdline.readAll().split('\0');
                orca = args.size() > 1 && args.at(0).contains("python") && args.at(1).endsWith("/orca");
            }
        }
        if (orca) {
            ::kill(pid, SIGTERM);
        }
    }
}

void startOrca()
{
    // The GNOME key is what Orca and toolkits check to know a screen reader
    // is wanted; kaccess sets it the same way before starting Orca.
    QProcess::execute(QStringLiteral("gsettings"),
                      {QStringLiteral("set"), QStringLiteral("org.gnome.desktop.a11y.applications"), QStringLiteral("screen-reader-enabled"),
                       QStringLiteral("true")});
    QProcess::startDetached(QStringLiteral("orca"), {QStringLiteral("--replace")});
}

void clearOrcaKey()
{
    QProcess::startDetached(QStringLiteral("gsettings"),
                            {QStringLiteral("set"), QStringLiteral("org.gnome.desktop.a11y.applications"), QStringLiteral("screen-reader-enabled"),
                             QStringLiteral("false")});
}

bool hasScheme(const QString &name)
{
    return !QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("color-schemes/%1.colors").arg(name)).isEmpty();
}

void writeWildos(const char *group, const char *key, const QVariant &value)
{
    KSharedConfigPtr wildos = fresh(QStringLiteral("wildosrc"));
    wildos->group(QString::fromLatin1(group)).writeEntry(key, value, KConfig::Notify);
    wildos->sync();
}

struct PointerColour {
    const char *id;
    const char *name;
    const char *colour;
    const char *theme;
};

// White and black are Breeze's own cursors; yellow and sea glass are
// recoloured from Breeze Light when wildos-branding is built.
const PointerColour pointerList[] = {
    {"white", "White", "#ffffff", "Breeze_Light"},
    {"black", "Black", "#111111", "breeze_cursors"},
    {"yellow", "Yellow", "#f2d34a", "WildOS-Yellow"},
    {"seaglass", "Sea glass", "#7fd6c8", "WildOS-SeaGlass"},
};

const QString customId = QStringLiteral("custom");
const QString customTheme = QStringLiteral("WildOS-Custom");

// wildos-shell's effects, by their KWin plugin ids.
const QString outlineEffect = QStringLiteral("wildos_outline");
const QString blurEffect = QStringLiteral("wildos_blurinactive");
const QString blurGroup = QStringLiteral("Effect-wildos_blurinactive");
const QString outlineGroup = QStringLiteral("Effect-wildos_outline");

void callEffects(const QString &method, const QString &effect)
{
    QDBusMessage message = QDBusMessage::createMethodCall(kwinService, QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"), method);
    message << effect;
    QDBusConnection::sessionBus().asyncCall(message);
}

bool effectEnabled(const QString &effect)
{
    return fresh(QStringLiteral("kwinrc"))->group(QStringLiteral("Plugins")).readEntry(effect + QStringLiteral("Enabled"), false);
}

// What System Settings' effects page does: the Plugins key is what KWin
// reads at the next sign-in, the D-Bus call changes the running session.
void setEffectEnabled(const QString &effect, bool on)
{
    KSharedConfigPtr kwin = fresh(QStringLiteral("kwinrc"));
    kwin->group(QStringLiteral("Plugins")).writeEntry(effect + QStringLiteral("Enabled"), on, KConfig::Notify);
    kwin->sync();
    callEffects(on ? QStringLiteral("loadEffect") : QStringLiteral("unloadEffect"), effect);
}

template<typename T>
void writeEffectSetting(const QString &effect, const QString &group, const char *key, const T &value)
{
    KSharedConfigPtr kwin = fresh(QStringLiteral("kwinrc"));
    kwin->group(group).writeEntry(key, value, KConfig::Notify);
    kwin->sync();
    callEffects(QStringLiteral("reconfigureEffect"), effect);
}

template<typename T>
void writeBlurSetting(const char *key, const T &value)
{
    writeEffectSetting(blurEffect, blurGroup, key, value);
}

QString cursorTheme()
{
    QString theme = fresh(QStringLiteral("kcminputrc"))->group(QStringLiteral("Mouse")).readEntry("cursorTheme", QString());
    if (theme.isEmpty() || theme == QLatin1String("default")) {
        // Nothing chosen yet: Debian's x-cursor-theme alternative decides.
        const KConfig fallback(QStringLiteral("/usr/share/icons/default/index.theme"), KConfig::SimpleConfig);
        const QStringList inherits = fallback.group(QStringLiteral("Icon Theme")).readEntry("Inherits", QStringList());
        theme = inherits.isEmpty() ? QStringLiteral("breeze_cursors") : inherits.constFirst();
    }
    return theme;
}
}

namespace AccessCore
{
QString highContrastScheme()
{
    return contrastScheme;
}

bool stickyKeys()
{
    return readAccess("Keyboard", "StickyKeys", false);
}

void setStickyKeys(bool on)
{
    writeAccess("Keyboard", "StickyKeys", on);
    pokeKaccess();
}

bool slowKeys()
{
    return readAccess("Keyboard", "SlowKeys", false);
}

int slowKeysDelay()
{
    return readAccess("Keyboard", "SlowKeysDelay", 500);
}

void setSlowKeys(bool on)
{
    writeAccess("Keyboard", "SlowKeys", on);
    pokeKaccess();
}

void setSlowKeysDelay(int ms)
{
    writeAccess("Keyboard", "SlowKeysDelay", ms);
    pokeKaccess();
}

bool bounceKeys()
{
    return readAccess("Keyboard", "BounceKeys", false);
}

int bounceKeysDelay()
{
    return readAccess("Keyboard", "BounceKeysDelay", 500);
}

void setBounceKeys(bool on)
{
    writeAccess("Keyboard", "BounceKeys", on);
    pokeKaccess();
}

void setBounceKeysDelay(int ms)
{
    writeAccess("Keyboard", "BounceKeysDelay", ms);
    pokeKaccess();
}

bool visibleBell()
{
    return readAccess("Bell", "VisibleBell", false);
}

void setVisibleBell(bool on)
{
    writeAccess("Bell", "VisibleBell", on);
    if (on) {
        // kcm_access turns the system bell on whenever any bell is wanted;
        // without it no bell event reaches KWin's flash.
        KSharedConfigPtr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        globals->group(QStringLiteral("General")).writeEntry("UseSystemBell", true, KConfig::Notify);
        globals->sync();
    }
    pokeKaccess();
}

bool screenReaderInstalled()
{
    return !QStandardPaths::findExecutable(QStringLiteral("orca")).isEmpty();
}

bool screenReader()
{
    return readAccess("ScreenReader", "Enabled", false);
}

void setScreenReader(bool on)
{
    writeAccess("ScreenReader", "Enabled", on);
    if (kaccessRunning()) {
        // kaccess owns Super Alt S in the session and keeps the value in
        // memory; starting it again makes it reread the file and start Orca
        // itself, so its shortcut stays in step with this switch.
        pokeKaccess();
    } else if (on) {
        startOrca();
    }
    if (!on) {
        clearOrcaKey();
        stopOrca();
    }
}

void restoreScreenReader()
{
    if (screenReader() && screenReaderInstalled()) {
        startOrca();
    }
}

bool highContrast()
{
    return fresh(QStringLiteral("kdeglobals"))->group(QStringLiteral("General")).readEntry("ColorScheme", QString()) == contrastScheme;
}

void setHighContrast(bool on, QObject *context, const std::function<void()> &done)
{
    KSharedConfigPtr globals = fresh(QStringLiteral("kdeglobals"));
    const KConfigGroup general = globals->group(QStringLiteral("General"));
    const QString current = general.readEntry("ColorScheme", QStringLiteral("WildOS"));
    const bool tool = !QStandardPaths::findExecutable(QStringLiteral("plasma-apply-colorscheme")).isEmpty();

    QString scheme;
    QString accent;
    if (on) {
        if (current == contrastScheme || !hasScheme(contrastScheme)) {
            if (done) {
                done();
            }
            return;
        }
        // Remembered so turning high contrast off puts back exactly the
        // theme and accent that were there.
        KSharedConfigPtr wildos = fresh(QStringLiteral("wildosrc"));
        KConfigGroup access = wildos->group(QStringLiteral("Accessibility"));
        access.writeEntry("SchemeBeforeContrast", current);
        access.writeEntry("AccentBeforeContrast", general.readEntry("AccentColor", QString()));
        wildos->sync();
        scheme = contrastScheme;
    } else {
        if (current != contrastScheme) {
            if (done) {
                done();
            }
            return;
        }
        const KConfigGroup access = fresh(QStringLiteral("wildosrc"))->group(QStringLiteral("Accessibility"));
        scheme = access.readEntry("SchemeBeforeContrast", QStringLiteral("WildOS"));
        if (scheme == contrastScheme || !hasScheme(scheme)) {
            scheme = QStringLiteral("WildOS");
        }
        accent = access.readEntry("AccentBeforeContrast", QString());
    }

    const auto writeAccent = [accent] {
        if (accent.isEmpty()) {
            return;
        }
        KSharedConfigPtr config = fresh(QStringLiteral("kdeglobals"));
        KConfigGroup group = config->group(QStringLiteral("General"));
        group.writeEntry("AccentColor", accent, KConfig::Notify);
        group.writeEntry("LastUsedCustomAccentColor", accent, KConfig::Notify);
        config->sync();
    };

    if (!tool) {
        KConfigGroup group = globals->group(QStringLiteral("General"));
        group.writeEntry("ColorScheme", scheme, KConfig::Notify);
        globals->sync();
        writeAccent();
        if (done) {
            done();
        }
        return;
    }

    // As Settings and wildos-auto-theme apply a scheme: the tool resets the
    // accent key, so the accent goes back on afterwards through the tool's
    // own option and then the key Wild OS widgets read.
    auto apply = new QProcess(context);
    QPointer<QObject> guard(context);
    runAfter(apply, context, [accent, writeAccent, context, guard, done] {
        if (accent.isEmpty() || !guard) {
            if (done) {
                done();
            }
            return;
        }
        const QStringList rgb = accent.split(QLatin1Char(','));
        const QColor colour = rgb.size() >= 3 ? QColor(rgb[0].trimmed().toInt(), rgb[1].trimmed().toInt(), rgb[2].trimmed().toInt()) : QColor(accent);
        auto restore = new QProcess(context);
        runAfter(restore, context, [writeAccent, done] {
            writeAccent();
            if (done) {
                done();
            }
        });
        restore->start(QStringLiteral("plasma-apply-colorscheme"), {QStringLiteral("--accent-color"), colour.name(QColor::HexRgb)});
    });
    apply->start(QStringLiteral("plasma-apply-colorscheme"), {scheme});
}

bool magnifier()
{
    return getProperty(kwinService, QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"), QStringLiteral("activeEffects"))
        .toStringList()
        .contains(QLatin1String("zoom"));
}

void zoom(const QString &action)
{
    // The effect stays loaded whether or not the screen is magnified, so
    // Super and plus works at any time, as it does in Plasma by default.
    KSharedConfigPtr kwin = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup plugins = kwin->group(QStringLiteral("Plugins"));
    if (!plugins.readEntry("zoomEnabled", true)) {
        plugins.writeEntry("zoomEnabled", true);
        kwin->sync();
    }
    QDBusMessage load = QDBusMessage::createMethodCall(kwinService, QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"),
                                                       QStringLiteral("loadEffect"));
    load << QStringLiteral("zoom");
    // Waited for, so the zoom actions exist before they are invoked.
    QDBusConnection::sessionBus().call(load, QDBus::Block, 2000);
    if (action == QLatin1String("in")) {
        invokeKWinShortcut(QStringLiteral("view_zoom_in"));
    } else if (action == QLatin1String("out")) {
        invokeKWinShortcut(QStringLiteral("view_zoom_out"));
    } else {
        invokeKWinShortcut(QStringLiteral("view_actual_size"));
    }
}

void setMagnifier(bool on)
{
    // As Windows' Magnifier switch: on starts magnifying, off goes back to
    // the normal size.
    zoom(on ? QStringLiteral("in") : QStringLiteral("reset"));
}

QString keyboardDesktopFile()
{
    const QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("applications"), QStandardPaths::LocateDirectory);
    QString found;
    for (const QString &dir : dirs) {
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QString &name : files) {
            const QString path = dir + QLatin1Char('/') + name;
            const KDesktopFile file(path);
            if (!file.desktopGroup().readEntry("X-KDE-Wayland-VirtualKeyboard", false)) {
                continue;
            }
            if (name == QLatin1String("com.github.maliit.keyboard.desktop")) {
                return path;
            }
            if (found.isEmpty()) {
                found = path;
            }
        }
    }
    return found;
}

bool onScreenKeyboard()
{
    const KConfigGroup wayland = fresh(QStringLiteral("kwinrc"))->group(QStringLiteral("Wayland"));
    if (wayland.readPathEntry("InputMethod", QString()).isEmpty()) {
        return false;
    }
    const QVariant live = getProperty(kwinService, QStringLiteral("/VirtualKeyboard"), QStringLiteral("org.kde.kwin.VirtualKeyboard"), QStringLiteral("enabled"));
    return live.isValid() ? live.toBool() : wayland.readEntry("VirtualKeyboardEnabled", true);
}

void setOnScreenKeyboard(bool on, QObject *context, const std::function<void()> &done)
{
    const QString desktopFile = keyboardDesktopFile();
    if (on && desktopFile.isEmpty()) {
        if (done) {
            done();
        }
        return;
    }
    KSharedConfigPtr kwin = fresh(QStringLiteral("kwinrc"));
    KConfigGroup wayland = kwin->group(QStringLiteral("Wayland"));
    if (on) {
        // What kcm_virtualkeyboard writes; KWin watches the key and starts
        // the keyboard at once.
        wayland.writePathEntry("InputMethod", desktopFile, KConfig::Notify);
    }
    wayland.writeEntry("VirtualKeyboardEnabled", on);
    kwin->sync();
    writeWildos("Accessibility", "OnScreenKeyboard", on);

    setProperty(kwinService, QStringLiteral("/VirtualKeyboard"), QStringLiteral("org.kde.kwin.VirtualKeyboard"), QStringLiteral("enabled"), on);
    if (!on) {
        if (done) {
            done();
        }
        return;
    }
    // KWin only shows its keyboard by itself after a touch; turned on here
    // it should appear now, for someone using a mouse or a switch. The
    // keyboard process needs a moment to connect before it can be shown.
    QTimer::singleShot(1200, context, [done] {
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(kwinService, QStringLiteral("/VirtualKeyboard"),
                                                                               QStringLiteral("org.kde.kwin.VirtualKeyboard"), QStringLiteral("forceActivate")));
        if (done) {
            done();
        }
    });
}

QVariantList pointerColours()
{
    QVariantList list;
    for (const PointerColour &p : pointerList) {
        // A colour whose theme is missing is not offered.
        bool installed = false;
        for (const QString &dir : QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons"), QStandardPaths::LocateDirectory)) {
            if (QFileInfo::exists(dir + QLatin1Char('/') + QString::fromLatin1(p.theme) + QStringLiteral("/index.theme"))) {
                installed = true;
                break;
            }
        }
        if (installed) {
            list.append(QVariantMap{{QStringLiteral("id"), QString::fromLatin1(p.id)},
                                    {QStringLiteral("name"), QString::fromLatin1(p.name)},
                                    {QStringLiteral("colour"), QColor(QString::fromLatin1(p.colour))},
                                    {QStringLiteral("theme"), QString::fromLatin1(p.theme)}});
        }
    }
    return list;
}

QString customPointerTheme()
{
    return customTheme;
}

QString customPointerDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/icons/") + customTheme;
}

QString pointerColour()
{
    const QString theme = cursorTheme();
    if (theme == customTheme) {
        return customId;
    }
    for (const PointerColour &p : pointerList) {
        if (theme == QLatin1String(p.theme)) {
            return QString::fromLatin1(p.id);
        }
    }
    return {};
}

QList<int> pointerSizes()
{
    // Breeze's own sizes, so X11 apps draw the same size KWin does, and 96
    // for the largest Windows offers.
    return {24, 30, 36, 48, 60, 72, 96};
}

int pointerSize()
{
    return fresh(QStringLiteral("kcminputrc"))->group(QStringLiteral("Mouse")).readEntry("cursorSize", 24);
}

void setPointer(const QString &colourId, int size)
{
    QString theme;
    for (const PointerColour &p : pointerList) {
        if (colourId == QLatin1String(p.id)) {
            theme = QString::fromLatin1(p.theme);
        }
    }
    if (colourId == customId && QFileInfo::exists(customPointerDir() + QStringLiteral("/index.theme"))) {
        theme = customTheme;
    }
    KSharedConfigPtr input = fresh(QStringLiteral("kcminputrc"));
    KConfigGroup mouse = input->group(QStringLiteral("Mouse"));
    if (!theme.isEmpty()) {
        mouse.writeEntry("cursorTheme", theme, KConfig::Notify);
    }
    mouse.writeEntry("cursorSize", size, KConfig::Notify);
    input->sync();
    // What kcm_cursortheme sends (CursorChanged): KWin reloads its cursor
    // and the GTK settings bridge follows.
    auto message = QDBusMessage::createSignal(QStringLiteral("/KGlobalSettings"), QStringLiteral("org.kde.KGlobalSettings"), QStringLiteral("notifyChange"));
    message << 5 << 0;
    QDBusConnection::sessionBus().send(message);
}

bool monoAudio()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                                                          QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("GetUnitFileState"));
    message << monoUnit;
    const QDBusReply<QString> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
    return reply.isValid() && reply.value() == QLatin1String("enabled");
}

void setMonoAudio(bool on)
{
    // The mono mix is a PipeWire filter in a process of its own (the unit
    // runs "pipewire -c wildos-mono-audio.conf"), so turning it on or off
    // never restarts PipeWire or interrupts what is playing. Enabling the
    // unit keeps the choice for the next sign-in.
    const QString service = QStringLiteral("org.freedesktop.systemd1");
    const QString path = QStringLiteral("/org/freedesktop/systemd1");
    const QString manager = QStringLiteral("org.freedesktop.systemd1.Manager");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (on) {
        QDBusMessage enable = QDBusMessage::createMethodCall(service, path, manager, QStringLiteral("EnableUnitFiles"));
        enable << QStringList{monoUnit} << false << true;
        bus.call(enable, QDBus::Block, 5000);
        QDBusMessage start = QDBusMessage::createMethodCall(service, path, manager, QStringLiteral("StartUnit"));
        start << monoUnit << QStringLiteral("replace");
        bus.asyncCall(start);
    } else {
        QDBusMessage stop = QDBusMessage::createMethodCall(service, path, manager, QStringLiteral("StopUnit"));
        stop << monoUnit << QStringLiteral("replace");
        bus.asyncCall(stop);
        QDBusMessage disable = QDBusMessage::createMethodCall(service, path, manager, QStringLiteral("DisableUnitFiles"));
        disable << QStringList{monoUnit} << false;
        bus.call(disable, QDBus::Block, 5000);
    }
}

bool windowEffectsInstalled()
{
    const QString dir = QLibraryInfo::path(QLibraryInfo::PluginsPath) + QStringLiteral("/kwin/effects/plugins/");
    return QFile::exists(dir + outlineEffect + QStringLiteral(".so")) && QFile::exists(dir + blurEffect + QStringLiteral(".so"));
}

bool windowEffectsSupported()
{
    QDBusMessage message = QDBusMessage::createMethodCall(kwinService, QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"),
                                                          QStringLiteral("isEffectSupported"));
    message << outlineEffect;
    const QDBusReply<bool> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
    // No answer (KWin busy or not running) is not a reason to lock the
    // switches; KWin refuses to load an effect it cannot run anyway.
    return !reply.isValid() || reply.value();
}

bool outlineWindows()
{
    return effectEnabled(outlineEffect);
}

void setOutlineWindows(bool on)
{
    setEffectEnabled(outlineEffect, on);
}

bool outlineFillingScreen()
{
    return fresh(QStringLiteral("kwinrc"))->group(outlineGroup).readEntry("Maximised", true);
}

void setOutlineFillingScreen(bool on)
{
    writeEffectSetting(outlineEffect, outlineGroup, "Maximised", on);
}

bool blurBackground()
{
    return effectEnabled(blurEffect);
}

void setBlurBackground(bool on)
{
    setEffectEnabled(blurEffect, on);
}

int blurStrength()
{
    return std::clamp(fresh(QStringLiteral("kwinrc"))->group(blurGroup).readEntry("Strength", 3), 1, 5);
}

void setBlurStrength(int step)
{
    writeBlurSetting("Strength", std::clamp(step, 1, 5));
}

bool blurDesktop()
{
    return fresh(QStringLiteral("kwinrc"))->group(blurGroup).readEntry("Desktop", true);
}

void setBlurDesktop(bool on)
{
    writeBlurSetting("Desktop", on);
}

bool strongerText()
{
    return fresh(QStringLiteral("wildosrc"))->group(QStringLiteral("Accessibility")).readEntry("StrongerText", false);
}

void setStrongerText(bool on)
{
    writeWildos("Accessibility", "StrongerText", on);
}
}
