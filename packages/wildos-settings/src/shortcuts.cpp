#include "shortcuts.h"

#include <KApplicationTrader>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <KKeySequenceRecorder>
#include <KService>
#include <KSharedConfig>

#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QWindow>

#include <algorithm>

namespace
{
struct Known {
    const char *component;
    const char *action;
    const char *group;
    const char *name;
    // What Wild OS sets for new accounts where that is not the action's own
    // default (branding's kglobalshortcutsrc), alternatives tab separated.
    const char *keys;
};

// The order is the order on the page.
const Known known[] = {
    {"plasmashell", "activate application launcher", QT_TRANSLATE_NOOP("Shortcuts", "Desktop"), QT_TRANSLATE_NOOP("Shortcuts", "Open Start"),
     "Meta\tMeta+R\tMeta+S\tAlt+F1"},
    {"kwin", "Overview", QT_TRANSLATE_NOOP("Shortcuts", "Desktop"), QT_TRANSLATE_NOOP("Shortcuts", "Task view"), "Meta+W\tMeta+Tab"},
    {"kwin", "Wild OS show desktop", QT_TRANSLATE_NOOP("Shortcuts", "Desktop"), QT_TRANSLATE_NOOP("Shortcuts", "Show the desktop"), "Meta+D"},
    {"ksmserver", "Lock Session", QT_TRANSLATE_NOOP("Shortcuts", "Desktop"), QT_TRANSLATE_NOOP("Shortcuts", "Lock the screen"), nullptr},
    {"org.wildos.security.desktop", "_launch", QT_TRANSLATE_NOOP("Shortcuts", "Desktop"),
     QT_TRANSLATE_NOOP("Shortcuts", "Lock, switch user, sign out or Task Manager"), "Ctrl+Alt+Del"},

    {"kwin", "Walk Through Windows", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Switch between windows"), nullptr},
    {"kwin", "Wild OS next window of this app", QT_TRANSLATE_NOOP("Shortcuts", "Windows"),
     QT_TRANSLATE_NOOP("Shortcuts", "Next window of the same app"), "Meta+`"},
    {"kwin", "Wild OS close window", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Close the window"), "Alt+F4"},
    {"kwin", "Window Maximize", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Maximise the window"), nullptr},
    {"kwin", "Window Minimize", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Minimise the window"), nullptr},
    {"kwin", "Window Quick Tile Left", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Snap the window to the left"), nullptr},
    {"kwin", "Window Quick Tile Right", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Snap the window to the right"),
     nullptr},
    {"kwin", "Window Quick Tile Top", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Snap the window to the top"), nullptr},
    {"kwin", "Window Quick Tile Bottom", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Snap the window to the bottom"),
     nullptr},
    {"kwin", "Wild OS snap layouts", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Show snap layouts"), "Meta+Z"},
    {"kwin", "Window to Next Screen", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Move the window to the next screen"),
     nullptr},
    {"kwin", "Window Operations Menu", QT_TRANSLATE_NOOP("Shortcuts", "Windows"), QT_TRANSLATE_NOOP("Shortcuts", "Open the window's menu"), "Alt+Space"},

    {"kwin", "Switch One Desktop to the Left", QT_TRANSLATE_NOOP("Shortcuts", "Desktops"),
     QT_TRANSLATE_NOOP("Shortcuts", "Go to the desktop on the left"), nullptr},
    {"kwin", "Switch One Desktop to the Right", QT_TRANSLATE_NOOP("Shortcuts", "Desktops"),
     QT_TRANSLATE_NOOP("Shortcuts", "Go to the desktop on the right"), nullptr},
    {"kwin", "Window One Desktop to the Left", QT_TRANSLATE_NOOP("Shortcuts", "Desktops"),
     QT_TRANSLATE_NOOP("Shortcuts", "Move the window to the desktop on the left"), nullptr},
    {"kwin", "Window One Desktop to the Right", QT_TRANSLATE_NOOP("Shortcuts", "Desktops"),
     QT_TRANSLATE_NOOP("Shortcuts", "Move the window to the desktop on the right"), nullptr},

    {"org.wildos.files.desktop", "_launch", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Files"), "Meta+E"},
    {"org.wildos.settings.desktop", "_launch", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Settings"), "Meta+I"},
    {"org.wildos.terminal.desktop", "_launch", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Terminal"), nullptr},
    {"org.wildos.taskmanager.desktop", "_launch", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Task Manager"),
     "Ctrl+Shift+Esc"},
    {"org.wildos.screenshot.desktop", "_launch", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Take a screenshot"),
     "Meta+Shift+S\tPrint"},
    {"org.wildos.screenshot.desktop", "Record", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Record the screen"),
     "Meta+Shift+R"},
    {"org.wildos.clipboard.desktop", "_launch", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Clipboard history"),
     "Meta+V"},
    {"org.wildos.clipboard.desktop", "Emoji", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Emoji"), "Meta+."},
    {"org.wildos.notes.desktop", "QuickNote", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Quick note"), "Meta+N"},
    {"kwin", "view_zoom_in", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Zoom the screen in"), nullptr},
    {"kwin", "view_zoom_out", QT_TRANSLATE_NOOP("Shortcuts", "Apps and tools"), QT_TRANSLATE_NOOP("Shortcuts", "Zoom the screen out"), nullptr},
};

const QChar separator = QLatin1Char('|');
const QString launch = QStringLiteral("_launch");
const QString ownPrefix = QStringLiteral("wildos-shortcut-");

QString idOf(const QString &component, const QString &action)
{
    return component + separator + action;
}

const Known *knownFor(const QString &id)
{
    for (const Known &entry : known) {
        if (idOf(QLatin1String(entry.component), QLatin1String(entry.action)) == id) {
            return &entry;
        }
    }
    return nullptr;
}

QDBusInterface &service()
{
    // KGlobalAccel teaches D-Bus the key and shortcut types on first use.
    KGlobalAccel::self();
    static QDBusInterface interface(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"), QStringLiteral("org.kde.KGlobalAccel"));
    return interface;
}

QList<QKeySequence> parse(const QString &keys)
{
    QList<QKeySequence> list;
    for (const QString &one : keys.split(QLatin1Char('\t'), Qt::SkipEmptyParts)) {
        const QKeySequence sequence = QKeySequence::fromString(one, QKeySequence::PortableText);
        if (!sequence.isEmpty()) {
            list.append(sequence);
        }
    }
    return list;
}

bool same(QList<QKeySequence> a, QList<QKeySequence> b)
{
    const auto order = [](const QKeySequence &x, const QKeySequence &y) {
        return x.toString(QKeySequence::PortableText) < y.toString(QKeySequence::PortableText);
    };
    std::sort(a.begin(), a.end(), order);
    std::sort(b.begin(), b.end(), order);
    return a == b;
}

KConfigGroup ownGroup()
{
    return KSharedConfig::openConfig(QStringLiteral("wildosrc"))->group(QStringLiteral("Shortcuts"));
}

QString commandFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/kglobalaccel");
}
}

Shortcuts::Shortcuts(QObject *parent)
    : QObject(parent)
{
    reload();
}

QStringList Shortcuts::actionId(const QString &id) const
{
    const int cut = id.indexOf(separator);
    // Component, action, and the two names shown for them, which the
    // service already has for anything it knows.
    return {id.left(cut), id.mid(cut + 1), QString(), QString()};
}

QList<QKeySequence> Shortcuts::keysOf(const QString &id, bool defaults) const
{
    const QDBusReply<QList<QKeySequence>> reply = service().call(defaults ? QStringLiteral("defaultShortcutKeys") : QStringLiteral("shortcutKeys"), actionId(id));
    QList<QKeySequence> keys = reply.isValid() ? reply.value() : QList<QKeySequence>();
    keys.removeAll(QKeySequence());
    return keys;
}

QStringList Shortcuts::parts(const QString &keys) const
{
    // "Meta++" is the Windows key with the plus key.
    QString text = keys;
    QString last;
    if (text.endsWith(QLatin1String("++")) || text == QLatin1String("+")) {
        last = QStringLiteral("+");
        text.chop(text.size() > 1 ? 2 : 1);
    }
    QStringList list = text.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (!last.isEmpty()) {
        list.append(last);
    }
    for (QString &key : list) {
        if (key == QLatin1String("Meta")) {
            key = QStringLiteral("Super");
        }
    }
    return list;
}

QString Shortcuts::display(const QString &keys) const
{
    return parts(keys).join(QStringLiteral(" + "));
}

void Shortcuts::reload()
{
    // Which actions the service has, asked once per program.
    QHash<QString, QStringList> actions;
    const auto present = [&actions](const QString &component, const QString &action) {
        if (!actions.contains(component)) {
            QStringList names;
            const QDBusReply<QList<QStringList>> reply = service().call(QStringLiteral("allActionsForComponent"), QStringList{component, {}, {}, {}});
            if (reply.isValid()) {
                for (const QStringList &one : reply.value()) {
                    names.append(one.value(1));
                }
            }
            actions.insert(component, names);
        }
        return actions.value(component).contains(action);
    };
    const auto shown = [this](const QList<QKeySequence> &keys) {
        QVariantList list;
        for (const QKeySequence &one : keys) {
            const QString portable = one.toString(QKeySequence::PortableText);
            list.append(QVariantMap{{QStringLiteral("keys"), portable}, {QStringLiteral("parts"), parts(portable)}});
        }
        return list;
    };

    QVariantList system;
    for (const Known &entry : known) {
        const QString component = QLatin1String(entry.component);
        const QString action = QLatin1String(entry.action);
        if (!present(component, action)) {
            continue;
        }
        const QString id = idOf(component, action);
        const QList<QKeySequence> keys = keysOf(id);
        const QList<QKeySequence> usual = entry.keys ? parse(QString::fromLatin1(entry.keys)) : keysOf(id, true);
        system.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("group"), tr(entry.group)},
            {QStringLiteral("name"), tr(entry.name)},
            {QStringLiteral("keys"), shown(keys)},
            {QStringLiteral("changed"), !same(keys, usual)},
        });
    }
    m_system = system;

    QVariantList own;
    for (const QString &component : ownIds()) {
        QString name;
        QString icon;
        QString command;
        if (component.startsWith(ownPrefix)) {
            const QString file = commandFolder() + QLatin1Char('/') + component;
            if (!QFile::exists(file)) {
                continue;
            }
            const KDesktopFile desktop(file);
            name = desktop.readName();
            command = desktop.desktopGroup().readEntry("Exec");
            icon = QStringLiteral("utilities-terminal");
        } else if (const KService::Ptr app = KService::serviceByStorageId(component)) {
            name = app->name();
            icon = app->icon();
        } else {
            continue;
        }
        own.append(QVariantMap{
            {QStringLiteral("id"), idOf(component, launch)},
            {QStringLiteral("name"), name},
            {QStringLiteral("icon"), icon},
            {QStringLiteral("command"), command},
            {QStringLiteral("keys"), shown(keysOf(idOf(component, launch)))},
        });
    }
    m_own = own;
    Q_EMIT changed();
}

bool Shortcuts::anyChanged() const
{
    return std::any_of(m_system.cbegin(), m_system.cend(), [](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("changed")).toBool();
    });
}

bool Shortcuts::recording() const
{
    return m_recorder && m_recorder->isRecording();
}

QStringList Shortcuts::shownParts() const
{
    if (!m_recorder || m_recorder->currentKeySequence().isEmpty()) {
        return {};
    }
    return parts(m_recorder->currentKeySequence().toString(QKeySequence::PortableText));
}

void Shortcuts::record(QWindow *window)
{
    if (m_recorder && m_recorder->window() != window) {
        delete m_recorder;
        m_recorder = nullptr;
    }
    if (!m_recorder) {
        m_recorder = new KKeySequenceRecorder(window, this);
        // One combination, not a sequence of them, and never a plain letter
        // or number, which would stop that key typing anywhere.
        m_recorder->setMultiKeyShortcutsAllowed(false);
        m_recorder->setModifierlessAllowed(false);
        connect(m_recorder, &KKeySequenceRecorder::recordingChanged, this, &Shortcuts::recordingChanged);
        connect(m_recorder, &KKeySequenceRecorder::currentKeySequenceChanged, this, &Shortcuts::shownChanged);
        connect(m_recorder, &KKeySequenceRecorder::gotKeySequence, this, [this](const QKeySequence &keys) {
            if (!keys.isEmpty()) {
                Q_EMIT recorded(keys.toString(QKeySequence::PortableText));
            }
        });
    }
    m_recorder->startRecording();
}

void Shortcuts::stopRecording()
{
    if (m_recorder && m_recorder->isRecording()) {
        m_recorder->cancelRecording();
    }
}

QString Shortcuts::usedBy(const QString &keys, const QString &exceptId) const
{
    const QKeySequence sequence = QKeySequence::fromString(keys, QKeySequence::PortableText);
    for (const KGlobalShortcutInfo &info : KGlobalAccel::globalShortcutsByKey(sequence)) {
        const QString id = idOf(info.componentUniqueName(), info.uniqueName());
        if (id == exceptId) {
            continue;
        }
        if (const Known *entry = knownFor(id)) {
            return tr(entry->name);
        }
        for (const QVariant &mine : m_own) {
            if (mine.toMap().value(QStringLiteral("id")).toString() == id) {
                return mine.toMap().value(QStringLiteral("name")).toString();
            }
        }
        // An app's own start shortcut reads best as the app's name.
        if (info.uniqueName() == launch && !info.componentFriendlyName().isEmpty()) {
            return info.componentFriendlyName();
        }
        return info.friendlyName().isEmpty() ? info.uniqueName() : info.friendlyName();
    }
    return {};
}

void Shortcuts::apply(const QString &id, const QList<QKeySequence> &keys)
{
    // The foreign call changes a shortcut that belongs to another program
    // and tells that program, which is what a settings app is for.
    service().call(QStringLiteral("setForeignShortcutKeys"), actionId(id), QVariant::fromValue(keys));
}

void Shortcuts::release(const QKeySequence &keys, const QString &exceptId)
{
    for (const KGlobalShortcutInfo &info : KGlobalAccel::globalShortcutsByKey(keys)) {
        const QString id = idOf(info.componentUniqueName(), info.uniqueName());
        if (id == exceptId) {
            continue;
        }
        QList<QKeySequence> rest = keysOf(id);
        rest.removeAll(keys);
        apply(id, rest);
    }
}

void Shortcuts::setKeys(const QString &id, const QString &keys, bool replace)
{
    const QKeySequence sequence = QKeySequence::fromString(keys, QKeySequence::PortableText);
    if (sequence.isEmpty()) {
        return;
    }
    release(sequence, id);
    QList<QKeySequence> all = replace ? QList<QKeySequence>() : keysOf(id);
    if (!all.contains(sequence)) {
        all.append(sequence);
    }
    apply(id, all);
    reload();
}

void Shortcuts::removeKeys(const QString &id, const QString &keys)
{
    QList<QKeySequence> all = keysOf(id);
    all.removeAll(QKeySequence::fromString(keys, QKeySequence::PortableText));
    apply(id, all);
    reload();
}

void Shortcuts::reset(const QString &id)
{
    const Known *entry = knownFor(id);
    const QList<QKeySequence> usual = entry && entry->keys ? parse(QString::fromLatin1(entry->keys)) : keysOf(id, true);
    for (const QKeySequence &one : usual) {
        release(one, id);
    }
    apply(id, usual);
    reload();
}

void Shortcuts::resetAll()
{
    const QVariantList before = m_system;
    for (const QVariant &entry : before) {
        if (entry.toMap().value(QStringLiteral("changed")).toBool()) {
            reset(entry.toMap().value(QStringLiteral("id")).toString());
        }
    }
}

QVariantList Shortcuts::apps() const
{
    KService::List services = KApplicationTrader::query([](const KService::Ptr &app) {
        return !app->noDisplay() && !app->exec().isEmpty();
    });
    std::sort(services.begin(), services.end(), [](const KService::Ptr &a, const KService::Ptr &b) {
        return a->name().localeAwareCompare(b->name()) < 0;
    });
    const QStringList taken = ownIds();
    QVariantList list;
    for (const KService::Ptr &app : std::as_const(services)) {
        if (taken.contains(app->storageId())) {
            continue;
        }
        list.append(QVariantMap{{QStringLiteral("id"), app->storageId()}, {QStringLiteral("name"), app->name()}, {QStringLiteral("icon"), app->icon()}});
    }
    return list;
}

QStringList Shortcuts::ownIds() const
{
    return ownGroup().readEntry("Own", QStringList());
}

void Shortcuts::setOwnIds(const QStringList &ids)
{
    KConfigGroup group = ownGroup();
    group.writeEntry("Own", ids);
    group.sync();
}

void Shortcuts::registerOwn(const QString &component, const QString &name, const QString &keys)
{
    // Registering and unregistering a nameless action makes the service
    // read the .desktop file, which gives it the start action to hold keys.
    service().call(QStringLiteral("doRegister"), QStringList{component, QString(), name, QString()});
    service().call(QStringLiteral("unregister"), component, QString());

    QStringList ids = ownIds();
    if (!ids.contains(component)) {
        ids.append(component);
        setOwnIds(ids);
    }
    setKeys(idOf(component, launch), keys, true);
}

void Shortcuts::addApp(const QString &app, const QString &keys)
{
    const KService::Ptr found = KService::serviceByStorageId(app);
    if (found) {
        registerOwn(found->storageId(), found->name(), keys);
    }
}

void Shortcuts::addCommand(const QString &name, const QString &command, const QString &keys)
{
    if (command.trimmed().isEmpty() || !QDir().mkpath(commandFolder())) {
        return;
    }
    QString component;
    for (int n = 1; component.isEmpty(); ++n) {
        const QString candidate = ownPrefix + QString::number(n) + QStringLiteral(".desktop");
        if (!QFile::exists(commandFolder() + QLatin1Char('/') + candidate)) {
            component = candidate;
        }
    }
    const QString label = name.trimmed().isEmpty() ? command.trimmed() : name.trimmed();
    {
        KDesktopFile desktop(commandFolder() + QLatin1Char('/') + component);
        KConfigGroup entry = desktop.desktopGroup();
        entry.writeEntry("Type", "Application");
        entry.writeEntry("Name", label);
        entry.writeEntry("Exec", command.trimmed());
        entry.writeEntry("NoDisplay", true);
        entry.writeEntry("StartupNotify", false);
        entry.writeEntry("X-KDE-GlobalAccel-CommandShortcut", true);
        entry.sync();
    }
    registerOwn(component, label, keys);
}

void Shortcuts::removeOwn(const QString &id)
{
    const QString component = actionId(id).value(0);
    apply(id, {});
    QStringList ids = ownIds();
    ids.removeAll(component);
    setOwnIds(ids);
    if (component.startsWith(ownPrefix)) {
        service().call(QStringLiteral("unregister"), component, launch);
        QFile::remove(commandFolder() + QLatin1Char('/') + component);
    }
    reload();
}
