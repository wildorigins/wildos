#pragma once

#include "effects.h"
#include "sdk.h"
#include "seen.h"

#include <KSharedConfig>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QTimer>

// The lights of the whole PC, for Cooler's Lighting page.
//
// It talks to the OpenRGB server through Sdk::Client and is the only
// thing in Wild OS that sets a light: Cooler's window asks for everything
// through the session bus (io.wildos.Lighting, /io/wildos/Lighting,
// interface io.wildos.Lighting1) and writes nothing itself, so the effect
// carries on with the window closed and comes back after a restart.
//
// One effect runs across every part; a part can be given its own. Each
// zone has a name, a shape it is drawn as, and, where the part cannot
// count them, how many lights are plugged in and how many sections they
// are in. All of that is kept in ~/.config/wildoslightingrc.
//
// Until something is chosen in Cooler this does not set a light: the
// lighting follows the taskbar underglow through the openrgb command, as
// it did before Cooler existed (Lighting::apply). From the first choice
// on, the effects are worked out here and sent as frames, about fifteen a
// second while something moves and nothing at all while the lights are
// steady or off.
//
// Hardware comes first. Frames go only to a part's Direct mode, which
// OpenRGB keeps for colours that are not saved to the part. A part with
// no such mode is set once when something changes and never streamed to,
// since each write may be stored in its memory.
class Lights : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.wildos.Lighting1")

public:
    enum class Server {
        Off,
        Away,
        Starting,
        Ready,
        Failed,
    };

    explicit Lights(QObject *parent = nullptr);

    // Where wildos-openrgb.service listens on the loopback.
    static quint16 port();

    void setServer(Server state);
    void setUnderglow(Plan::Look look, const QColor &colour, const QColor &accent, bool still);
    // Something has been chosen in Cooler, so the lights are set from here
    // and no longer through the openrgb command.
    bool inCharge() const;
    // Parts come back from suspend in their own default lighting.
    void sendAgain();

public Q_SLOTS:
    // Everything the page shows, as JSON text: see describe().
    QString Describe();
    // The colour of every light in Describe's order, three bytes each.
    QByteArray Colours();
    void SetOn(bool on);
    void SetEverything(const QString &effect, int brightness, int speed);
    void SetOffWhenLocked(bool off);
    void SetPartOwn(const QString &part, bool own);
    void SetPart(const QString &part, const QString &effect, int brightness, int speed);
    void SetZoneName(const QString &part, int zone, const QString &name);
    void SetZoneShape(const QString &part, int zone, const QString &shape);
    void SetZoneCount(const QString &part, int zone, int count);
    void SetZoneSplit(const QString &part, int zone, int sections);
    // Blinks a zone white for three seconds, then puts back what it
    // showed. Zone -1 is every zone of the part.
    void Flash(const QString &part, int zone);

Q_SIGNALS:
    Q_SCRIPTABLE void Changed();
    void onRequested(bool on);
    // The lights were borrowed (a flash, a new count) while the openrgb
    // command is still what sets them: it has to set them again.
    void legacyWanted();

private Q_SLOTS:
    void screenLocked(bool locked);

private:
    enum class Kind {
        // Has a Direct mode: every light can be set, as often as wanted.
        Stream,
        // Every light can be set, but only through a mode that may be
        // saved to the part: set on change, never streamed.
        Still,
        // One colour for the whole part, through its Static mode.
        OneColour,
        // No colour at all: on, off, brighter, dimmer.
        White,
        None,
    };

    struct Zone {
        Sdk::Zone reported;
        QString name;
        Effects::Shape shape = Effects::Shape::Strip;
        int split = 1;
        // On m_clock, 0 when not flashing.
        qint64 flashUntil = 0;
    };

    struct Part {
        QString key;
        int device = -1;
        Sdk::Controller controller;
        Kind kind = Kind::None;
        QList<Zone> zones;
        bool own = false;
        Effects::Effect effect = Effects::Effect::Taskbar;
        int brightness = 100;
        int speed = 35;
        int steadyMode = -1;
        int breatheMode = -1;
        int offMode = -1;
        bool customSet = false;
        bool hasSent = false;
        QList<QRgb> sent;
        QByteArray sentMode;
    };

    void rebuild();
    void classify(Part &part) const;
    void readChoices(Part &part);
    Part *find(const QString &key);
    Zone *find(const QString &key, int zone, Part **part = nullptr);
    void takeCharge();

    Effects::Look lookFor(const Part &part) const;
    bool flashing(const Part &part, qint64 now) const;
    bool movesNow(const Part &part) const;
    QList<QRgb> compose(const Part &part, const Effects::Look &look, double seconds, qint64 now) const;
    void push(Part &part, bool force);
    void pushAll(bool force);
    void pushWhite(Part &part, const Effects::Look &look, bool dark);
    void tick();
    void readTemperature();
    void updateTimers();
    void changed();

    QJsonObject describe() const;
    QJsonArray offered(const Part *part) const;
    bool offers(const Part *part, Effects::Effect effect) const;

    KSharedConfigPtr m_config;
    Sdk::Client m_client;
    Server m_server = Server::Off;
    QString m_root;
    QList<Part> m_parts;
    QList<Seen::Part> m_seen;
    // Resizes already asked for on this connection, so a server that
    // refuses one is not asked for ever.
    QSet<QString> m_resized;
    bool m_legacyAfterRead = false;

    Effects::Look m_under;
    double m_temperature = -1;
    bool m_sensor = false;
    bool m_locked = false;

    QElapsedTimer m_clock;
    QTimer m_frames;
    QTimer m_thermometer;
    QTimer m_again;
};
