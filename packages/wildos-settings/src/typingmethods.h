#pragma once

#include <QFile>
#include <QString>
#include <QStringList>

// What Settings' keyboard page and wildos-access share about typing methods
// (see keyboard.h): where the list is kept and what each method needs.
namespace TypingMethods
{
// In kxkbrc's [Layout], beside LayoutList: one entry per layout, empty for
// a plain one. KDE's keyboard module leaves keys it doesn't know alone.
inline const char *configKey()
{
    return "WildOSInputMethods";
}

inline bool installed(const QString &engine)
{
    return !engine.isEmpty() && QFile::exists(QStringLiteral("/usr/share/fcitx5/addon/") + engine + QStringLiteral(".conf"));
}

// The Fcitx 5 engine a method is downloaded with. Both Vietnamese methods
// are Lotus's: it types straight into the app, with no underlined word
// waiting to be finished, which is what Vietnamese typists on Windows know.
inline QString engine(const QString &method)
{
    if (method == QLatin1String("telex") || method == QLatin1String("vni")) {
        return QStringLiteral("lotus");
    }
    return {};
}

// The engine a method runs on here: Lotus, or Bamboo (what Wild OS used
// before Lotus) on a PC that has not downloaded Lotus yet, so typing keeps
// working until it has.
inline QString engineInUse(const QString &method)
{
    const QString wanted = engine(method);
    if (wanted.isEmpty() || installed(wanted)) {
        return wanted;
    }
    if (installed(QStringLiteral("bamboo"))) {
        return QStringLiteral("bamboo");
    }
    return {};
}

// Engines Wild OS sets up itself. Fcitx running any other one was set up by
// the person using it, and is left alone.
inline bool ours(const QString &engine)
{
    return engine == QLatin1String("lotus") || engine == QLatin1String("bamboo");
}

// The name Lotus and Bamboo both give the method, in their own settings.
inline QString methodName(const QString &method)
{
    return method == QLatin1String("vni") ? QStringLiteral("VNI") : QStringLiteral("Telex");
}

// The launcher KWin takes as its input method, as kcm_virtualkeyboard
// writes it.
inline QString fcitxDesktopFile()
{
    return QStringLiteral("/usr/share/applications/org.fcitx.Fcitx5.desktop");
}

// Settings says on this when the typing methods in the list change, so
// wildos-access hands KWin to Fcitx or back without waiting for a restart.
inline QString signalPath()
{
    return QStringLiteral("/org/wildos/TypingMethods");
}

inline QString signalInterface()
{
    return QStringLiteral("org.wildos.TypingMethods");
}
}
