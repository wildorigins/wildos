#include "lights.h"

#include <KConfigGroup>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QJsonDocument>

namespace
{
const QString busName = QStringLiteral("io.wildos.Lighting");
const QString busPath = QStringLiteral("/io/wildos/Lighting");

constexpr int frameInterval = 66;
constexpr int flashFor = 3000;
constexpr int blink = 300;
constexpr int slowBlink = 500;
constexpr int mostSections = 12;
constexpr int longestName = 40;
const QRgb white = qRgb(255, 255, 255);

using Effects::Effect;
using Effects::Shape;

const char *kindOf(qint32 type)
{
    // OpenRGB's device types, in its own order.
    static const char *kinds[] = {"motherboard", "memory",  "graphics", "cooler",  "strip",   "keyboard",   "mouse",
                                  "mousemat",    "headset", "stand",    "gamepad", "light",   "speaker",    "virtual",
                                  "storage",     "case",    "microphone", "accessory", "keypad", "laptop", "monitor"};
    return type >= 0 && type < int(std::size(kinds)) ? kinds[type] : "other";
}

QString cleanKey(const QString &text)
{
    QString out;
    for (const QChar c : text) {
        out += c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('.') || c == QLatin1Char('-') ? c : QLatin1Char('_');
    }
    return out.simplified();
}

int between(qint64 from, qint64 to, int percent)
{
    return int(from + (to - from) * qBound(0, percent, 100) / 100);
}

Shape shapeOf(const Sdk::Zone &zone)
{
    if (zone.type == Sdk::ZoneLinearLoop) {
        return Shape::Ring;
    }
    if (zone.type == Sdk::ZoneSingle || (zone.ledsCount == 1 && !zone.resizable)) {
        return Shape::Single;
    }
    return Shape::Strip;
}
}

Lights::Lights(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("wildoslightingrc"), KConfig::SimpleConfig))
    , m_root(qEnvironmentVariable("WILDOS_LIGHTING_SYSFS", QStringLiteral("/sys")))
{
    m_clock.start();
    m_seen = Seen::graphicsCards(m_root);
    m_sensor = Seen::processorTemperature(m_root) >= 0;

    m_frames.setInterval(frameInterval);
    m_frames.setTimerType(Qt::PreciseTimer);
    connect(&m_frames, &QTimer::timeout, this, &Lights::tick);
    m_thermometer.setInterval(2000);
    connect(&m_thermometer, &QTimer::timeout, this, &Lights::readTemperature);
    m_again.setSingleShot(true);
    m_again.setInterval(2000);
    connect(&m_again, &QTimer::timeout, this, [this] {
        if (m_server == Server::Ready) {
            m_client.open(port());
        }
    });

    connect(&m_client, &Sdk::Client::listChanged, this, &Lights::rebuild);
    connect(&m_client, &Sdk::Client::lost, this, [this](const QString &why) {
        qWarning("Lost the OpenRGB server: %s", qPrintable(why));
        m_parts.clear();
        updateTimers();
        changed();
        if (m_server == Server::Ready) {
            m_again.start();
        }
    });

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.registerObject(busPath, this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportScriptableSignals);
    if (!bus.registerService(busName)) {
        qWarning("Could not take the name %s on the session bus", qPrintable(busName));
    }
    // KDE's lock screen says so under both paths, depending on its version.
    for (const QString &path : {QStringLiteral("/ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver")}) {
        bus.connect(QStringLiteral("org.freedesktop.ScreenSaver"), path, QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("ActiveChanged"), this,
                    SLOT(screenLocked(bool)));
    }
}

quint16 Lights::port()
{
    bool ok = false;
    const int port = qEnvironmentVariableIntValue("WILDOS_LIGHTING_PORT", &ok);
    // OpenRGB's own default, which its command line looks for when it
    // joins a running server.
    return ok && port > 0 && port < 65536 ? quint16(port) : 6742;
}

bool Lights::inCharge() const
{
    return m_config->group(QStringLiteral("Everything")).hasKey("Effect");
}

void Lights::setServer(Server state)
{
    if (state == m_server) {
        return;
    }
    m_server = state;
    m_again.stop();
    m_parts.clear();
    m_resized.clear();
    if (state == Server::Ready) {
        m_client.open(port());
        // Detection can still be running when the server starts
        // listening; these catch parts it found without saying so.
        for (const int later : {2000, 15000}) {
            QTimer::singleShot(later, this, [this] {
                if (m_client.ready()) {
                    m_client.readAgain();
                }
            });
        }
    } else {
        m_client.close();
    }
    updateTimers();
    changed();
}

void Lights::setUnderglow(Plan::Look look, const QColor &colour, const QColor &accent, bool still)
{
    if (m_under.taskbar == look && m_under.taskbarColour == colour && m_under.accent == accent && m_under.still == still) {
        return;
    }
    m_under.taskbar = look;
    m_under.taskbarColour = colour;
    m_under.accent = accent;
    m_under.still = still;
    pushAll(false);
    updateTimers();
    changed();
}

void Lights::sendAgain()
{
    for (Part &part : m_parts) {
        part.customSet = false;
    }
    pushAll(true);
}

void Lights::screenLocked(bool locked)
{
    if (locked == m_locked) {
        return;
    }
    m_locked = locked;
    pushAll(false);
    updateTimers();
    changed();
}

void Lights::classify(Part &part) const
{
    const Sdk::Controller &c = part.controller;
    part.kind = Kind::None;
    quint64 inZones = 0;
    for (const Sdk::Zone &zone : c.zones) {
        inZones += zone.ledsCount;
    }
    if (c.modes.isEmpty() || inZones != quint64(c.leds)) {
        return;
    }
    const auto named = [&c](const char *name) {
        for (int i = 0; i < c.modes.size(); ++i) {
            if (c.modes[i].name.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) {
                return i;
            }
        }
        return -1;
    };
    quint32 any = 0;
    for (const Sdk::Mode &mode : c.modes) {
        any |= mode.flags;
    }

    const int direct = named("Direct");
    if (direct >= 0 && (c.modes[direct].flags & Sdk::HasPerLedColour) && !(c.modes[direct].flags & Sdk::AutomaticSave)) {
        part.kind = Kind::Stream;
        return;
    }
    if (any & Sdk::HasPerLedColour) {
        part.kind = Kind::Still;
        return;
    }
    const int fixed = named("Static");
    if (fixed >= 0 && (c.modes[fixed].flags & Sdk::HasModeColour) && c.modes[fixed].coloursMax >= 1) {
        part.kind = Kind::OneColour;
        part.steadyMode = fixed;
        return;
    }
    if (any & (Sdk::HasModeColour | Sdk::HasRandomColour)) {
        return;
    }

    for (const char *name : {"Static", "Direct", "On", "Steady"}) {
        if (part.steadyMode < 0) {
            part.steadyMode = named(name);
        }
    }
    part.offMode = named("Off");
    for (int i = 0; i < c.modes.size(); ++i) {
        const bool breathes = c.modes[i].name.contains(QLatin1String("breath"), Qt::CaseInsensitive);
        if (breathes && part.breatheMode < 0) {
            part.breatheMode = i;
        }
        if (!breathes && i != part.offMode && part.steadyMode < 0) {
            part.steadyMode = i;
        }
    }
    if (part.steadyMode >= 0) {
        part.kind = Kind::White;
    }
}

void Lights::readChoices(Part &part)
{
    const KConfigGroup choices = m_config->group(QStringLiteral("Part")).group(part.key);
    part.own = choices.readEntry("Own", false);
    part.effect = Effects::fromId(choices.readEntry("Effect", QString()), part.kind == Kind::White ? Effect::Steady : Effect::Theme);
    part.brightness = qBound(0, choices.readEntry("Brightness", 100), 100);
    part.speed = qBound(0, choices.readEntry("Speed", 35), 100);
    if (!offers(&part, part.effect)) {
        part.effect = Effect::Steady;
    }

    const KConfigGroup zones = m_config->group(QStringLiteral("Zone")).group(part.key);
    for (int i = 0; i < part.zones.size(); ++i) {
        Zone &zone = part.zones[i];
        const KConfigGroup saved = zones.group(QString::number(i));
        zone.name = saved.readEntry("Name", QString()).trimmed();
        if (zone.name.isEmpty()) {
            zone.name = zone.reported.name;
        }
        zone.shape = Effects::shapeFromId(saved.readEntry("Shape", QString()), shapeOf(zone.reported));
        zone.split = qBound(1, saved.readEntry("Split", 1), qBound(1, int(zone.reported.ledsCount), mostSections));
    }
}

void Lights::rebuild()
{
    const qint64 now = m_clock.elapsed();
    QHash<QString, qint64> flashes;
    for (const Part &part : std::as_const(m_parts)) {
        for (int i = 0; i < part.zones.size(); ++i) {
            if (part.zones[i].flashUntil > now) {
                flashes.insert(part.key + QLatin1Char('/') + QString::number(i), part.zones[i].flashUntil);
            }
        }
    }

    QList<Part> parts;
    QHash<QString, int> used;
    const QList<Sdk::Controller> &controllers = m_client.controllers();
    for (int i = 0; i < controllers.size(); ++i) {
        Part part;
        part.device = i;
        part.controller = controllers[i];
        const QString base = cleanKey(part.controller.serial.isEmpty() ? part.controller.name : part.controller.name + QLatin1Char(' ') + part.controller.serial);
        const int nth = ++used[base];
        part.key = nth == 1 ? base : base + QStringLiteral(" #%1").arg(nth);
        classify(part);
        for (const Sdk::Zone &reported : std::as_const(part.controller.zones)) {
            Zone zone;
            zone.reported = reported;
            part.zones.append(zone);
        }
        readChoices(part);
        for (int z = 0; z < part.zones.size(); ++z) {
            part.zones[z].flashUntil = flashes.value(part.key + QLatin1Char('/') + QString::number(z), 0);
        }
        // A part that was already here keeps what it was last sent, so
        // reading the list again writes nothing to it.
        if (const Part *old = find(part.key); old && old->kind == part.kind && old->controller.leds == part.controller.leds) {
            part.customSet = old->customSet;
            part.hasSent = old->hasSent;
            part.sent = old->sent;
            part.sentMode = old->sentMode;
        }
        parts.append(part);
    }
    m_parts = parts;

    // A strip connector forgets how many lights it was told about when
    // the server restarts: tell it again.
    bool resizing = false;
    if (m_client.protocol() > 0) {
        for (const Part &part : std::as_const(m_parts)) {
            if (part.kind == Kind::None) {
                continue;
            }
            const KConfigGroup zones = m_config->group(QStringLiteral("Zone")).group(part.key);
            for (int z = 0; z < part.zones.size(); ++z) {
                const Sdk::Zone &reported = part.zones[z].reported;
                const int count = zones.group(QString::number(z)).readEntry("Count", -1);
                const QString id = QStringLiteral("%1/%2/%3").arg(part.key).arg(z).arg(count);
                if (!reported.resizable || count < 0 || quint32(count) == reported.ledsCount || quint32(count) < reported.ledsMin
                    || quint32(count) > reported.ledsMax || m_resized.contains(id)) {
                    continue;
                }
                m_resized.insert(id);
                m_client.resizeZone(part.device, z, count);
                resizing = true;
            }
        }
    }
    if (resizing) {
        return;
    }

    pushAll(false);
    updateTimers();
    changed();
    if (m_legacyAfterRead) {
        m_legacyAfterRead = false;
        Q_EMIT legacyWanted();
    }
}

Lights::Part *Lights::find(const QString &key)
{
    for (Part &part : m_parts) {
        if (part.key == key) {
            return &part;
        }
    }
    return nullptr;
}

Lights::Zone *Lights::find(const QString &key, int zone, Part **owner)
{
    Part *part = find(key);
    if (!part || zone < 0 || zone >= part->zones.size()) {
        return nullptr;
    }
    if (owner) {
        *owner = part;
    }
    return &part->zones[zone];
}

void Lights::takeCharge()
{
    if (inCharge()) {
        return;
    }
    KConfigGroup everything = m_config->group(QStringLiteral("Everything"));
    everything.writeEntry("Effect", Effects::id(Effect::Taskbar));
    everything.writeEntry("Brightness", 100);
    everything.writeEntry("Speed", 35);
    m_config->sync();
    qInfo("Cooler has chosen the lighting: setting the lights from here from now on");
}

Effects::Look Lights::lookFor(const Part &part) const
{
    Effects::Look look = m_under;
    look.temperature = m_temperature;
    if (!inCharge()) {
        look.effect = Effect::Taskbar;
        look.brightness = 100;
        look.speed = 35;
        return look;
    }
    const KConfigGroup everything = m_config->group(QStringLiteral("Everything"));
    if (part.own) {
        look.effect = part.effect;
        look.brightness = part.brightness;
        look.speed = part.speed;
    } else {
        look.effect = Effects::fromId(everything.readEntry("Effect", QString()), Effect::Taskbar);
        look.brightness = qBound(0, everything.readEntry("Brightness", 100), 100);
        look.speed = qBound(0, everything.readEntry("Speed", 35), 100);
        if (look.effect == Effect::Temperature && !m_sensor) {
            look.effect = Effect::Steady;
        }
        // A part that can only do white follows how bright the rest is
        // and whether it breathes, and ignores the colour.
        if (part.kind == Kind::White) {
            const bool breathes = look.effect == Effect::Breathe || (look.effect == Effect::Taskbar && look.taskbar == Plan::Look::Breathing);
            look.effect = look.effect == Effect::Off ? Effect::Off : breathes && part.breatheMode >= 0 ? Effect::Breathe : Effect::Steady;
        }
    }
    if (m_locked && everything.readEntry("OffWhenLocked", false)) {
        look.effect = Effect::Off;
    }
    return look;
}

bool Lights::flashing(const Part &part, qint64 now) const
{
    for (const Zone &zone : part.zones) {
        if (zone.flashUntil > now) {
            return true;
        }
    }
    return false;
}

bool Lights::movesNow(const Part &part) const
{
    const Effects::Look look = lookFor(part);
    if (part.kind == Kind::White) {
        return look.effect == Effect::Breathe && !look.still;
    }
    // Before Cooler has chosen anything the part runs the taskbar's
    // effect by itself; what is drawn for it moves all the same.
    return (part.kind == Kind::Stream || !inCharge()) && Effects::moves(look);
}

QList<QRgb> Lights::compose(const Part &part, const Effects::Look &look, double seconds, qint64 now) const
{
    QList<QRgb> frame;
    frame.reserve(part.controller.leds);
    for (const Zone &zone : part.zones) {
        const int lights = int(zone.reported.ledsCount);
        if (zone.flashUntil > now) {
            // A part that cannot be streamed to is held white instead of
            // blinked: two writes rather than ten.
            const bool on = part.kind != Kind::Stream || ((zone.flashUntil - now) / blink) % 2 == 1;
            for (int i = 0; i < lights; ++i) {
                frame.append(on ? white : qRgb(0, 0, 0));
            }
            continue;
        }
        QList<Effects::Run> runs;
        for (const int length : Effects::split(lights, zone.split)) {
            runs.append({length, zone.shape});
        }
        Effects::fill(frame, look, runs, seconds);
    }
    return frame;
}

void Lights::pushWhite(Part &part, const Effects::Look &look, bool dark)
{
    const QList<Sdk::Mode> &modes = part.controller.modes;
    const bool off = dark || look.effect == Effect::Off || look.brightness == 0;
    int index = part.steadyMode;
    Sdk::Mode mode;
    if (off) {
        if (part.offMode >= 0) {
            index = part.offMode;
            mode = modes[index];
        } else {
            mode = modes[index];
            if (!(mode.flags & Sdk::HasBrightness)) {
                return;
            }
            mode.brightness = mode.brightnessMin;
        }
    } else {
        const bool breathes = look.effect == Effect::Breathe && !look.still && part.breatheMode >= 0;
        index = breathes ? part.breatheMode : part.steadyMode;
        mode = modes[index];
        if (mode.flags & Sdk::HasBrightness) {
            mode.brightness = quint32(between(mode.brightnessMin, mode.brightnessMax, look.brightness));
        }
        if (breathes && (mode.flags & Sdk::HasSpeed)) {
            mode.speed = quint32(between(mode.speedMin, mode.speedMax, look.speed));
        }
    }
    const QByteArray data = Sdk::modeData(index, mode, m_client.protocol());
    if (data == part.sentMode) {
        return;
    }
    m_client.updateMode(part.device, index, mode);
    part.sentMode = data;
}

void Lights::push(Part &part, bool force)
{
    if (!m_client.ready() || part.kind == Kind::None) {
        return;
    }
    const qint64 now = m_clock.elapsed();
    const bool flash = flashing(part, now);
    if (!inCharge() && !flash) {
        return;
    }
    if (force) {
        part.hasSent = false;
        part.sentMode.clear();
    }
    Effects::Look look = lookFor(part);

    switch (part.kind) {
    case Kind::Stream:
    case Kind::Still: {
        look.still = look.still || part.kind == Kind::Still;
        const QList<QRgb> frame = compose(part, look, now / 1000.0, now);
        if (frame.isEmpty() || (part.hasSent && frame == part.sent)) {
            return;
        }
        // A server that has fallen behind is not sent more to catch up on.
        if (part.hasSent && !flash && m_client.backlog() > 65536) {
            return;
        }
        if (!part.customSet) {
            m_client.setCustomMode(part.device);
            part.customSet = true;
        }
        m_client.updateLeds(part.device, frame);
        part.sent = frame;
        part.hasSent = true;
        return;
    }
    case Kind::OneColour: {
        look.still = true;
        Sdk::Mode mode = part.controller.modes[part.steadyMode];
        mode.colourMode = Sdk::ColoursModeSpecific;
        mode.colours = QList<QRgb>(qMax<qsizetype>(1, mode.coloursMin), flash ? white : Effects::colourAt(look, Shape::Single, 0, 0));
        if (mode.flags & Sdk::HasBrightness) {
            mode.brightness = mode.brightnessMax;
        }
        const QByteArray data = Sdk::modeData(part.steadyMode, mode, m_client.protocol());
        if (data != part.sentMode) {
            m_client.updateMode(part.device, part.steadyMode, mode);
            part.sentMode = data;
        }
        return;
    }
    case Kind::White: {
        qint64 until = 0;
        for (const Zone &zone : std::as_const(part.zones)) {
            until = qMax(until, zone.flashUntil);
        }
        pushWhite(part, look, flash && ((until - now) / slowBlink) % 2 == 1);
        return;
    }
    case Kind::None:
        return;
    }
}

void Lights::pushAll(bool force)
{
    for (Part &part : m_parts) {
        push(part, force);
    }
}

void Lights::tick()
{
    const qint64 now = m_clock.elapsed();
    bool ended = false;
    for (Part &part : m_parts) {
        bool endedHere = false;
        for (Zone &zone : part.zones) {
            if (zone.flashUntil != 0 && zone.flashUntil <= now) {
                zone.flashUntil = 0;
                endedHere = true;
            }
        }
        if (endedHere) {
            ended = true;
            if (!inCharge()) {
                // The openrgb command sets the part's own mode again.
                part.customSet = false;
                part.hasSent = false;
                part.sentMode.clear();
            }
        }
        if (endedHere || flashing(part, now) || (inCharge() && movesNow(part))) {
            push(part, false);
        }
    }
    if (ended) {
        updateTimers();
        changed();
        if (!inCharge()) {
            Q_EMIT legacyWanted();
        }
    }
}

void Lights::readTemperature()
{
    const double read = Seen::processorTemperature(m_root);
    const double rounded = read < 0 ? -1 : qRound(read);
    if (rounded == m_temperature) {
        return;
    }
    m_temperature = rounded;
    pushAll(false);
    changed();
}

void Lights::updateTimers()
{
    const qint64 now = m_clock.elapsed();
    bool frames = false;
    bool thermometer = false;
    if (m_client.ready()) {
        for (const Part &part : std::as_const(m_parts)) {
            frames = frames || flashing(part, now) || (inCharge() && part.kind == Kind::Stream && movesNow(part));
            thermometer = thermometer || (inCharge() && part.kind != Kind::None && lookFor(part).effect == Effect::Temperature);
        }
    }
    if (frames != m_frames.isActive()) {
        frames ? m_frames.start() : m_frames.stop();
    }
    if (thermometer != m_thermometer.isActive()) {
        if (thermometer) {
            readTemperature();
            m_thermometer.start();
        } else {
            m_thermometer.stop();
        }
    }
}

void Lights::changed()
{
    Q_EMIT Changed();
}

bool Lights::offers(const Part *part, Effect effect) const
{
    if (effect == Effect::Temperature && !m_sensor) {
        return false;
    }
    if (!part || part->kind != Kind::White) {
        return true;
    }
    switch (effect) {
    case Effect::Steady:
        return true;
    case Effect::Breathe:
        return part->breatheMode >= 0;
    case Effect::Off:
        return part->offMode >= 0
            || ((part->controller.modes[part->steadyMode].flags & Sdk::HasBrightness) && part->controller.modes[part->steadyMode].brightnessMin == 0);
    default:
        return false;
    }
}

QJsonArray Lights::offered(const Part *part) const
{
    QJsonArray list;
    for (const Effect effect : {Effect::Theme, Effect::Taskbar, Effect::Temperature, Effect::Breathe, Effect::Steady, Effect::Off}) {
        const bool colour = effect == Effect::Theme || effect == Effect::Taskbar || effect == Effect::Temperature;
        const bool white = part && part->kind == Kind::White;
        QJsonObject entry;
        entry[QStringLiteral("id")] = Effects::id(effect);
        entry[QStringLiteral("offered")] = offers(part, effect);
        // Why not, for the page to say: it needs colour, there is no
        // temperature to read, or the part has no such mode.
        entry[QStringLiteral("why")] = offers(part, effect) ? QString()
            : white && colour ? QStringLiteral("colour")
            : effect == Effect::Temperature ? QStringLiteral("sensor")
            : QStringLiteral("part");
        list.append(entry);
    }
    return list;
}

QJsonObject Lights::describe() const
{
    static const char *states[] = {"off", "away", "starting", "ready", "failed"};
    const bool ready = m_server == Server::Ready && m_client.ready();
    const qint64 now = m_clock.elapsed();
    const KConfigGroup saved = m_config->group(QStringLiteral("Everything"));

    QJsonObject everything;
    everything[QStringLiteral("effect")] = Effects::id(Effects::fromId(saved.readEntry("Effect", QString()), Effect::Taskbar));
    everything[QStringLiteral("brightness")] = qBound(0, saved.readEntry("Brightness", 100), 100);
    everything[QStringLiteral("speed")] = qBound(0, saved.readEntry("Speed", 35), 100);
    everything[QStringLiteral("offWhenLocked")] = saved.readEntry("OffWhenLocked", false);
    everything[QStringLiteral("effects")] = offered(nullptr);

    bool moving = false;
    QJsonArray parts;
    if (ready) {
        for (const Part &part : m_parts) {
            const Effects::Look look = lookFor(part);
            moving = moving || flashing(part, now) || (part.kind != Kind::None && movesNow(part));
            QJsonObject p;
            p[QStringLiteral("key")] = part.key;
            p[QStringLiteral("kind")] = QString::fromLatin1(kindOf(part.controller.type));
            p[QStringLiteral("name")] = part.controller.name;
            p[QStringLiteral("controlled")] = part.kind != Kind::None;
            p[QStringLiteral("why")] = part.kind == Kind::None ? QStringLiteral("unknown") : QString();
            p[QStringLiteral("white")] = part.kind == Kind::White;
            // Moving effects stand still on it: it cannot be streamed to.
            p[QStringLiteral("still")] = part.kind == Kind::Still || part.kind == Kind::OneColour;
            p[QStringLiteral("own")] = part.own;
            p[QStringLiteral("effect")] = Effects::id(part.own ? part.effect : look.effect);
            p[QStringLiteral("brightness")] = part.own ? part.brightness : look.brightness;
            p[QStringLiteral("speed")] = part.own ? part.speed : look.speed;
            p[QStringLiteral("effects")] = offered(&part);
            QJsonArray zones;
            for (int i = 0; i < part.zones.size(); ++i) {
                const Zone &zone = part.zones[i];
                QJsonObject z;
                z[QStringLiteral("index")] = i;
                z[QStringLiteral("reported")] = zone.reported.name;
                z[QStringLiteral("name")] = zone.name;
                z[QStringLiteral("shape")] = Effects::id(zone.shape);
                z[QStringLiteral("lights")] = int(zone.reported.ledsCount);
                z[QStringLiteral("resizable")] = zone.reported.resizable;
                z[QStringLiteral("min")] = int(zone.reported.ledsMin);
                z[QStringLiteral("max")] = int(zone.reported.ledsMax);
                z[QStringLiteral("split")] = zone.split;
                z[QStringLiteral("white")] = part.kind == Kind::White;
                z[QStringLiteral("flashing")] = zone.flashUntil > now;
                zones.append(z);
            }
            p[QStringLiteral("zones")] = zones;
            parts.append(p);
        }
        for (const Seen::Part &seen : m_seen) {
            QJsonObject p;
            p[QStringLiteral("key")] = seen.key;
            p[QStringLiteral("kind")] = QStringLiteral("graphics");
            p[QStringLiteral("name")] = seen.name;
            p[QStringLiteral("controlled")] = false;
            p[QStringLiteral("why")] = QStringLiteral("bus");
            QJsonArray zones;
            bool allWhite = true;
            for (int i = 0; i < seen.lights.size(); ++i) {
                QJsonObject z;
                z[QStringLiteral("index")] = i;
                z[QStringLiteral("name")] = seen.lights[i].name;
                z[QStringLiteral("shape")] = QStringLiteral("single");
                z[QStringLiteral("lights")] = 1;
                z[QStringLiteral("white")] = seen.lights[i].white;
                allWhite = allWhite && seen.lights[i].white;
                zones.append(z);
            }
            p[QStringLiteral("white")] = allWhite;
            p[QStringLiteral("zones")] = zones;
            parts.append(p);
        }
    }

    QJsonObject all;
    all[QStringLiteral("state")] = QString::fromLatin1(m_server == Server::Ready && !ready ? "starting" : states[int(m_server)]);
    all[QStringLiteral("inCharge")] = inCharge();
    all[QStringLiteral("moving")] = moving;
    all[QStringLiteral("everything")] = everything;
    all[QStringLiteral("parts")] = parts;
    return all;
}

QString Lights::Describe()
{
    return QString::fromUtf8(QJsonDocument(describe()).toJson(QJsonDocument::Compact));
}

QByteArray Lights::Colours()
{
    QByteArray out;
    if (m_server != Server::Ready || !m_client.ready()) {
        return out;
    }
    const qint64 now = m_clock.elapsed();
    const double seconds = now / 1000.0;
    const auto put = [&out](QRgb c) {
        out.append(char(qRed(c)));
        out.append(char(qGreen(c)));
        out.append(char(qBlue(c)));
    };
    for (const Part &part : std::as_const(m_parts)) {
        Effects::Look look = lookFor(part);
        look.forScreen = true;
        const bool flash = flashing(part, now);
        switch (part.kind) {
        case Kind::None:
            out.append(part.controller.leds * 3, '\0');
            break;
        case Kind::White: {
            qint64 until = 0;
            for (const Zone &zone : part.zones) {
                until = qMax(until, zone.flashUntil);
            }
            const bool dark = flash && ((until - now) / slowBlink) % 2 == 1;
            const int level = dark ? 0 : qRound(255 * Effects::whiteLevel(look, seconds));
            for (int i = 0; i < part.controller.leds; ++i) {
                put(qRgb(level, level, level));
            }
            break;
        }
        case Kind::OneColour: {
            look.still = true;
            const QRgb colour = flash ? white : Effects::colourAt(look, Shape::Single, 0, 0);
            for (int i = 0; i < part.controller.leds; ++i) {
                put(colour);
            }
            break;
        }
        case Kind::Stream:
        case Kind::Still:
            look.still = look.still || (part.kind == Kind::Still && inCharge());
            for (const QRgb colour : compose(part, look, seconds, now)) {
                put(colour);
            }
            break;
        }
    }
    for (const Seen::Part &seen : std::as_const(m_seen)) {
        out.append(seen.lights.size() * 3, '\0');
    }
    return out;
}

void Lights::SetOn(bool on)
{
    Q_EMIT onRequested(on);
}

void Lights::SetEverything(const QString &effect, int brightness, int speed)
{
    const Effect chosen = Effects::fromId(effect, Effect::Taskbar);
    if (Effects::id(chosen) != effect || !offers(nullptr, chosen)) {
        return;
    }
    takeCharge();
    KConfigGroup everything = m_config->group(QStringLiteral("Everything"));
    everything.writeEntry("Effect", effect);
    everything.writeEntry("Brightness", qBound(0, brightness, 100));
    everything.writeEntry("Speed", qBound(0, speed, 100));
    m_config->sync();
    pushAll(false);
    updateTimers();
    changed();
}

void Lights::SetOffWhenLocked(bool off)
{
    if (off) {
        takeCharge();
    }
    m_config->group(QStringLiteral("Everything")).writeEntry("OffWhenLocked", off);
    m_config->sync();
    pushAll(false);
    updateTimers();
    changed();
}

void Lights::SetPartOwn(const QString &key, bool own)
{
    Part *part = find(key);
    if (!part || part->kind == Kind::None) {
        return;
    }
    takeCharge();
    KConfigGroup choices = m_config->group(QStringLiteral("Part")).group(key);
    if (own && !choices.hasKey("Effect")) {
        // It starts out looking as it did while it followed the rest.
        const Effects::Look look = lookFor(*part);
        choices.writeEntry("Effect", Effects::id(offers(part, look.effect) ? look.effect : Effect::Steady));
        choices.writeEntry("Brightness", look.brightness);
        choices.writeEntry("Speed", look.speed);
    }
    choices.writeEntry("Own", own);
    m_config->sync();
    readChoices(*part);
    push(*part, false);
    updateTimers();
    changed();
}

void Lights::SetPart(const QString &key, const QString &effect, int brightness, int speed)
{
    Part *part = find(key);
    const Effect chosen = Effects::fromId(effect, Effect::Steady);
    if (!part || part->kind == Kind::None || Effects::id(chosen) != effect || !offers(part, chosen)) {
        return;
    }
    takeCharge();
    KConfigGroup choices = m_config->group(QStringLiteral("Part")).group(key);
    choices.writeEntry("Own", true);
    choices.writeEntry("Effect", effect);
    choices.writeEntry("Brightness", qBound(0, brightness, 100));
    choices.writeEntry("Speed", qBound(0, speed, 100));
    m_config->sync();
    readChoices(*part);
    push(*part, false);
    updateTimers();
    changed();
}

void Lights::SetZoneName(const QString &key, int index, const QString &name)
{
    Zone *zone = find(key, index);
    if (!zone) {
        return;
    }
    const QString wanted = name.simplified().left(longestName);
    KConfigGroup saved = m_config->group(QStringLiteral("Zone")).group(key).group(QString::number(index));
    if (wanted.isEmpty() || wanted == zone->reported.name) {
        saved.deleteEntry("Name");
        zone->name = zone->reported.name;
    } else {
        saved.writeEntry("Name", wanted);
        zone->name = wanted;
    }
    m_config->sync();
    changed();
}

void Lights::SetZoneShape(const QString &key, int index, const QString &shape)
{
    Part *part = nullptr;
    Zone *zone = find(key, index, &part);
    const Shape chosen = Effects::shapeFromId(shape, Shape::Strip);
    if (!zone || Effects::id(chosen) != shape) {
        return;
    }
    m_config->group(QStringLiteral("Zone")).group(key).group(QString::number(index)).writeEntry("Shape", shape);
    m_config->sync();
    zone->shape = chosen;
    push(*part, false);
    changed();
}

void Lights::SetZoneCount(const QString &key, int index, int count)
{
    Part *part = nullptr;
    Zone *zone = find(key, index, &part);
    if (!zone || !zone->reported.resizable || part->kind == Kind::None) {
        return;
    }
    const int wanted = qBound(int(zone->reported.ledsMin), count, int(zone->reported.ledsMax));
    m_config->group(QStringLiteral("Zone")).group(key).group(QString::number(index)).writeEntry("Count", wanted);
    m_config->sync();
    if (quint32(wanted) == zone->reported.ledsCount || !m_client.ready()) {
        return;
    }
    m_resized.insert(QStringLiteral("%1/%2/%3").arg(key).arg(index).arg(wanted));
    m_legacyAfterRead = !inCharge();
    m_client.resizeZone(part->device, index, wanted);
}

void Lights::SetZoneSplit(const QString &key, int index, int sections)
{
    Part *part = nullptr;
    Zone *zone = find(key, index, &part);
    if (!zone) {
        return;
    }
    const int wanted = qBound(1, sections, qBound(1, int(zone->reported.ledsCount), mostSections));
    m_config->group(QStringLiteral("Zone")).group(key).group(QString::number(index)).writeEntry("Split", wanted);
    m_config->sync();
    zone->split = wanted;
    push(*part, false);
    changed();
}

void Lights::Flash(const QString &key, int index)
{
    Part *part = find(key);
    if (!part || part->kind == Kind::None || !m_client.ready() || index >= part->zones.size()) {
        return;
    }
    const qint64 until = m_clock.elapsed() + flashFor;
    // A part set through one mode for all of it cannot blink one zone
    // apart from the others.
    const bool whole = index < 0 || part->kind == Kind::OneColour || part->kind == Kind::White;
    for (int i = 0; i < part->zones.size(); ++i) {
        if ((whole || i == index) && part->zones[i].reported.ledsCount > 0) {
            part->zones[i].flashUntil = until;
        }
    }
    push(*part, false);
    updateTimers();
    changed();
}
