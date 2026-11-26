#include "typingfollower.h"

#include "typingmethods.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace
{
const QString keyboardService = QStringLiteral("org.kde.keyboard");
const QString layoutsPath = QStringLiteral("/Layouts");
const QString layoutsInterface = QStringLiteral("org.kde.KeyboardLayouts");
const QString fcitxService = QStringLiteral("org.fcitx.Fcitx5");

// One engine in a Fcitx group, as its controller sends them: the engine's
// name and the keyboard layout it types on (empty for the group's own).
struct FcitxItem {
    QString name;
    QString layout;
};
using FcitxItems = QList<FcitxItem>;
}

Q_DECLARE_METATYPE(FcitxItem)
Q_DECLARE_METATYPE(FcitxItems)

namespace
{
QDBusArgument &operator<<(QDBusArgument &argument, const FcitxItem &item)
{
    argument.beginStructure();
    argument << item.name << item.layout;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, FcitxItem &item)
{
    argument.beginStructure();
    argument >> item.name >> item.layout;
    argument.endStructure();
    return argument;
}

KSharedConfigPtr fresh(const QString &file)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(file, KConfig::NoGlobals);
    config->reparseConfiguration();
    return config;
}

QString fcitxDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/fcitx5");
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.readAll() == contents) {
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(contents);
    return file.commit();
}

QDBusMessage controller(const QString &method, const QVariantList &arguments = {})
{
    QDBusMessage message = QDBusMessage::createMethodCall(fcitxService, QStringLiteral("/controller"), QStringLiteral("org.fcitx.Fcitx.Controller1"), method);
    message.setArguments(arguments);
    // D-Bus would otherwise start Fcitx itself for the call, outside KWin,
    // where it can't type into Wayland apps and KWin's own copy can't start.
    message.setAutoStartService(false);
    return message;
}

// Not running (no typing method yet) is fine: nothing to switch.
void call(const QString &method, const QVariantList &arguments = {})
{
    QDBusConnection::sessionBus().asyncCall(controller(method, arguments));
}

// Fcitx answers at once; the timeout only matters if it hangs.
QDBusMessage ask(const QString &method, const QVariantList &arguments = {})
{
    return QDBusConnection::sessionBus().call(controller(method, arguments), QDBus::Block, 2000);
}

bool fcitxRunning()
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(fcitxService);
}

QSet<QString> availableEngines()
{
    QSet<QString> names;
    const QDBusMessage reply = ask(QStringLiteral("AvailableInputMethods"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return names;
    }
    // a(ssssssb): unique name, name, native name, icon, label, language,
    // configurable.
    const QDBusArgument list = reply.arguments().constFirst().value<QDBusArgument>();
    list.beginArray();
    while (!list.atEnd()) {
        QString unique, name, native, icon, label, language;
        bool configurable = false;
        list.beginStructure();
        list >> unique >> name >> native >> icon >> label >> language >> configurable;
        list.endStructure();
        names.insert(unique);
    }
    list.endArray();
    return names;
}

// Fcitx's one group for someone who has never used it: its plain keyboard,
// which passes keys on untouched in whatever layout KWin is using, and the
// engines. Fcitx reads the profile only when it starts, so this is written
// before KWin starts it. A profile that is there already is the person's
// (engines they added, their own order) and is never written over; the
// engines are added to it once Fcitx runs (addEngines).
void writeProfileIfNone(const QStringList &engines)
{
    const QString path = fcitxDir() + QStringLiteral("/profile");
    if (QFile::exists(path)) {
        return;
    }
    QByteArray profile =
        "[Groups/0]\n"
        "Name=Default\n"
        "Default Layout=us\n"
        "DefaultIM=" + engines.value(0).toUtf8() + "\n"
        "\n"
        "[Groups/0/Items/0]\n"
        "Name=keyboard-us\n"
        "Layout=\n";
    for (int i = 0; i < engines.size(); ++i) {
        profile += "\n[Groups/0/Items/" + QByteArray::number(i + 1) + "]\nName=" + engines.at(i).toUtf8() + "\nLayout=\n";
    }
    profile += "\n[GroupOrder]\n0=Default\n";
    writeFile(path, profile);
}

// The engine Fcitx last typed with in a group, which it keeps in its profile
// and turns back on when typing is switched on.
QString lastEngine(const QString &group)
{
    QFile file(fcitxDir() + QStringLiteral("/profile"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    bool inGroup = false;
    QString name, engine;
    const auto lines = file.readAll().split('\n');
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.startsWith('[')) {
            if (inGroup && name == group) {
                return engine;
            }
            // [Groups/0], not [Groups/0/Items/1].
            inGroup = line.startsWith("[Groups/") && line.count('/') == 1;
            name.clear();
            engine.clear();
        } else if (inGroup && line.startsWith("Name=")) {
            name = QString::fromUtf8(line.mid(5));
        } else if (inGroup && line.startsWith("DefaultIM=")) {
            engine = QString::fromUtf8(line.mid(10));
        }
    }
    return inGroup && name == group ? engine : QString();
}

// Wild OS's Fcitx settings (no switching keys of its own, no tray icon, one
// state for every window), for someone using a Wild OS typing method.
// Settings they already have are left alone.
void ensureConfig()
{
    const QString path = fcitxDir() + QStringLiteral("/config");
    if (QFile::exists(path)) {
        return;
    }
    QFile source(QStringLiteral("/usr/share/wildos/fcitx5/config"));
    if (source.open(QIODevice::ReadOnly)) {
        writeFile(path, source.readAll());
    }
}

// Lotus and Bamboo have one method at a time (InputMethod in their own
// file), so it is set as each Vietnamese entry comes into use. The rest of
// the file, which someone may have changed in Fcitx's or Lotus's own
// settings, stays as it is; the defaults below are only added when the key
// is missing.
bool setEngineMethod(const QString &engine, const QString &name)
{
    const QString path = fcitxDir() + QStringLiteral("/conf/") + engine + QStringLiteral(".conf");
    QFile file(path);
    QByteArrayList lines;
    if (file.open(QIODevice::ReadOnly)) {
        lines = file.readAll().split('\n');
        file.close();
        while (!lines.isEmpty() && lines.last().isEmpty()) {
            lines.removeLast();
        }
    }
    const QByteArray line = "InputMethod=" + name.toUtf8();
    bool found = false;
    for (QByteArray &l : lines) {
        if (l.startsWith("InputMethod=")) {
            // Another method chosen in the engine's own settings (Telex +
            // VNI, VIQR, a custom one) is the person's and stays.
            if (l == "InputMethod=Telex" || l == "InputMethod=VNI") {
                l = line;
            }
            found = true;
            break;
        }
    }
    if (!found) {
        lines.prepend(line);
    }
    QByteArrayList defaults;
    if (engine == QLatin1String("lotus")) {
        // Lotus starts out underlining the word being typed, as Bamboo does;
        // typing on Windows shows no line. In this mode it types straight
        // into the app and corrects the word with backspaces.
        defaults << QByteArray("Mode=Uinput (Smooth)");
    } else if (engine == QLatin1String("bamboo")) {
        defaults << QByteArray("DisplayUnderline=False");
    }
    int at = lines.indexOf(line) + 1;
    for (const QByteArray &d : std::as_const(defaults)) {
        const QByteArray key = d.left(d.indexOf('=') + 1);
        if (std::none_of(lines.cbegin(), lines.cend(), [&key](const QByteArray &l) {
                return l.startsWith(key);
            })) {
            // Beside InputMethod, which is never inside a [section].
            lines.insert(at++, d);
        }
    }
    return writeFile(path, lines.join('\n') + '\n');
}

// Older apps that run through XWayland (Steam, Wine) find Fcitx through XIM,
// which they look for at start-up; this reaches them from the next sign-in.
QString xwaylandFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/environment.d/90-wildos-typing.conf");
}

void setKeyboardEnabled(bool on)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/VirtualKeyboard"),
                                                          QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    message << QStringLiteral("org.kde.kwin.VirtualKeyboard") << QStringLiteral("enabled") << QVariant::fromValue(QDBusVariant(on));
    QDBusConnection::sessionBus().asyncCall(message);
}

bool kwinRunsFcitx()
{
    return fresh(QStringLiteral("kwinrc"))->group(QStringLiteral("Wayland")).readPathEntry("InputMethod", QString()) == TypingMethods::fcitxDesktopFile();
}
}

TypingFollower::TypingFollower(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<FcitxItem>();
    qDBusRegisterMetaType<FcitxItems>();

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString(), TypingMethods::signalPath(), TypingMethods::signalInterface(), QStringLiteral("changed"), this, SLOT(sync()));
    bus.connect(keyboardService, layoutsPath, layoutsInterface, QStringLiteral("layoutListChanged"), this, SLOT(sync()));
    bus.connect(keyboardService, layoutsPath, layoutsInterface, QStringLiteral("layoutChanged"), this, SLOT(follow(uint)));
    auto watcher = new QDBusServiceWatcher(fcitxService, bus, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &TypingFollower::fcitxStarted);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &TypingFollower::fcitxStopped);

    const QDBusReply<uint> current =
        bus.call(QDBusMessage::createMethodCall(keyboardService, layoutsPath, layoutsInterface, QStringLiteral("getLayout")), QDBus::Block, 2000);
    if (current.isValid()) {
        m_current = current.value();
    }
    sync();
}

QStringList TypingFollower::enginesWanted() const
{
    QStringList engines;
    for (const QString &method : m_methods) {
        const QString engine = TypingMethods::engineInUse(method);
        if (!engine.isEmpty() && !engines.contains(engine)) {
            engines.append(engine);
        }
    }
    return engines;
}

void TypingFollower::sync()
{
    const KConfigGroup layout = fresh(QStringLiteral("kxkbrc"))->group(QStringLiteral("Layout"));
    m_methods = layout.readEntry(TypingMethods::configKey(), QString()).split(QLatin1Char(','), Qt::KeepEmptyParts);
    for (QString &method : m_methods) {
        method = method.trimmed();
    }
    const bool wanted = !enginesWanted().isEmpty();

    const bool fcitx = kwinRunsFcitx();
    // What was there before is only written when Wild OS hands typing to
    // Fcitx, so Fcitx someone set up themselves is never taken away (it was,
    // from build 922, at every sign-in). KConfig leaves out an empty
    // InputMethodBefore (no input method before, the usual case), so the
    // on-screen keyboard's setting, always written, is what marks it.
    const KConfigGroup before = fresh(QStringLiteral("wildosrc"))->group(QStringLiteral("TypingMethods"));
    const bool ours = before.hasKey("InputMethodBefore") || before.hasKey("VirtualKeyboardBefore");
    if (wanted && !fcitx) {
        handToFcitx();
    } else if (!wanted && fcitx && ours) {
        giveBack();
    } else if (wanted) {
        // Those set up before the settings moved out of /etc/xdg.
        ensureConfig();
        addEngines();
        follow(m_current);
    }
}

void TypingFollower::handToFcitx()
{
    KSharedConfigPtr kwin = fresh(QStringLiteral("kwinrc"));
    KConfigGroup wayland = kwin->group(QStringLiteral("Wayland"));

    // What the on-screen keyboard was, to put back when the last typing
    // method goes.
    KSharedConfigPtr wildos = fresh(QStringLiteral("wildosrc"));
    KConfigGroup before = wildos->group(QStringLiteral("TypingMethods"));
    before.writePathEntry("InputMethodBefore", wayland.readPathEntry("InputMethod", QString()));
    before.writeEntry("VirtualKeyboardBefore", wayland.readEntry("VirtualKeyboardEnabled", false));
    wildos->sync();

    writeProfileIfNone(enginesWanted());
    ensureConfig();
    writeFile(xwaylandFile(), "XMODIFIERS=@im=fcitx\n");
    // KWin watches the key and starts Fcitx at once; fcitxStarted then
    // sets the method for the layout in use.
    wayland.writePathEntry("InputMethod", TypingMethods::fcitxDesktopFile(), KConfig::Notify);
    wayland.writeEntry("VirtualKeyboardEnabled", true, KConfig::Notify);
    kwin->sync();
    setKeyboardEnabled(true);
}

void TypingFollower::giveBack()
{
    const KConfigGroup before = fresh(QStringLiteral("wildosrc"))->group(QStringLiteral("TypingMethods"));
    const QString inputMethod = before.readPathEntry("InputMethodBefore", QString());
    const bool enabled = before.readEntry("VirtualKeyboardBefore", false);

    KSharedConfigPtr wildos = fresh(QStringLiteral("wildosrc"));
    KConfigGroup group = wildos->group(QStringLiteral("TypingMethods"));
    group.deleteEntry("InputMethodBefore");
    group.deleteEntry("VirtualKeyboardBefore");
    wildos->sync();

    KSharedConfigPtr kwin = fresh(QStringLiteral("kwinrc"));
    KConfigGroup wayland = kwin->group(QStringLiteral("Wayland"));
    if (inputMethod.isEmpty() || inputMethod == TypingMethods::fcitxDesktopFile()) {
        wayland.deleteEntry("InputMethod", KConfig::Notify);
    } else {
        wayland.writePathEntry("InputMethod", inputMethod, KConfig::Notify);
    }
    wayland.writeEntry("VirtualKeyboardEnabled", enabled, KConfig::Notify);
    kwin->sync();
    setKeyboardEnabled(enabled);
    QFile::remove(xwaylandFile());
}

// The engines the list needs, in Fcitx's group beside whatever the person
// has there, through Fcitx itself: it keeps its profile in memory and writes
// it back when it stops, so a change to the file would be lost. Bamboo,
// which Wild OS put there before Lotus, gives its place to Lotus.
void TypingFollower::addEngines()
{
    const QStringList engines = enginesWanted();
    if (engines.isEmpty() || !fcitxRunning()) {
        return;
    }
    const QDBusMessage groupReply = ask(QStringLiteral("CurrentInputMethodGroup"));
    if (groupReply.type() != QDBusMessage::ReplyMessage || groupReply.arguments().isEmpty()) {
        return;
    }
    const QString group = groupReply.arguments().constFirst().toString();
    const QDBusMessage info = ask(QStringLiteral("InputMethodGroupInfo"), {group});
    if (info.type() != QDBusMessage::ReplyMessage || info.arguments().size() < 2) {
        return;
    }
    const QString layout = info.arguments().at(0).toString();
    FcitxItems items = qdbus_cast<FcitxItems>(info.arguments().at(1));
    const QSet<QString> available = availableEngines();

    bool changed = false;
    if (engines.contains(QLatin1String("lotus")) && available.contains(QStringLiteral("lotus"))) {
        for (FcitxItem &item : items) {
            if (item.name == QLatin1String("bamboo")) {
                item.name = QStringLiteral("lotus");
                changed = true;
            }
        }
        // Two Lotus entries if the person had added it as well.
        QSet<QString> seen;
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [&seen](const FcitxItem &item) {
                                       const bool again = seen.contains(item.name);
                                       seen.insert(item.name);
                                       return again;
                                   }),
                    items.end());
    }
    bool restart = false;
    for (const QString &engine : engines) {
        if (std::any_of(items.cbegin(), items.cend(), [&engine](const FcitxItem &item) {
                return item.name == engine;
            })) {
            continue;
        }
        if (available.contains(engine)) {
            items.append({engine, QString()});
            changed = true;
        } else if (!m_restartedFor.contains(engine)) {
            m_restartedFor.append(engine);
            restart = true;
        }
    }
    if (changed) {
        QDBusMessage set = controller(QStringLiteral("SetInputMethodGroupInfo"));
        set << group << layout << QVariant::fromValue(items);
        QDBusConnection::sessionBus().call(set, QDBus::Block, 2000);
    }
    // Only Fcitx that KWin started can be started again from here; Fcitx
    // someone starts themselves sees Lotus when they next start it.
    if (restart && kwinRunsFcitx()) {
        restartFcitx();
    }
}

void TypingFollower::restartFcitx()
{
    m_restarting = true;
    setKeyboardEnabled(false);
}

void TypingFollower::fcitxStopped()
{
    if (m_restarting) {
        m_restarting = false;
        setKeyboardEnabled(true);
    }
}

void TypingFollower::fcitxStarted()
{
    if (enginesWanted().isEmpty()) {
        return;
    }
    addEngines();
    follow(m_current);
}

void TypingFollower::follow(uint index)
{
    m_current = index;
    // Someone with no Wild OS typing method runs Fcitx their own way. While
    // KWin starts Fcitx again, fcitxStarted follows the layout.
    if (m_restarting || enginesWanted().isEmpty() || !fcitxRunning()) {
        return;
    }
    const QDBusMessage reply = ask(QStringLiteral("CurrentInputMethod"));
    const QString current = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty() ? reply.arguments().constFirst().toString() : QString();
    // An engine the person switched to in Fcitx is theirs to switch off.
    if (!current.isEmpty() && !current.startsWith(QLatin1String("keyboard-")) && !TypingMethods::ours(current)) {
        return;
    }
    const QString method = m_methods.value(int(index));
    const QString engine = TypingMethods::engineInUse(method);
    if (engine.isEmpty()) {
        if (TypingMethods::ours(current)) {
            call(QStringLiteral("Deactivate"));
        }
        return;
    }
    if (setEngineMethod(engine, TypingMethods::methodName(method))) {
        call(QStringLiteral("ReloadAddonConfig"), {engine});
    }
    // Typing off (Fcitx's plain keyboard) after a restart: if the person
    // last typed with an engine of their own in this group, that is the one
    // to come back, as Fcitx itself would bring it back.
    if (current.startsWith(QLatin1String("keyboard-"))) {
        const QDBusMessage group = ask(QStringLiteral("CurrentInputMethodGroup"));
        const QString last = group.type() == QDBusMessage::ReplyMessage && !group.arguments().isEmpty()
            ? lastEngine(group.arguments().constFirst().toString())
            : QString();
        if (!last.isEmpty() && !last.startsWith(QLatin1String("keyboard-")) && !TypingMethods::ours(last)) {
            call(QStringLiteral("Activate"));
            return;
        }
    }
    call(QStringLiteral("SetCurrentIM"), {engine});
}
