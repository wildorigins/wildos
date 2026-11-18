#include "parts.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QVariantMap>

namespace Parts
{
const QList<Part> &all()
{
    static const QList<Part> parts{
        {QStringLiteral("files"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Files")), {QStringLiteral("wildos-files")}, QStringLiteral("wildos-files")},
        {QStringLiteral("settings"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Settings")), {QStringLiteral("wildos-settings")}, QStringLiteral("wildos-settings")},
        {QStringLiteral("store"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Store")), {QStringLiteral("wildos-store"), QStringLiteral("packagekitd"), QStringLiteral("flatpak")},
         QStringLiteral("wildos-store")},
        {QStringLiteral("mail"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Mail")),
         {QStringLiteral("wildos-mail"), QStringLiteral("evolution-source-registry"), QStringLiteral("evolution-addressbook-factory")},
         QStringLiteral("wildos-mail")},
        {QStringLiteral("calendar"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Calendar")),
         {QStringLiteral("wildos-calendar"), QStringLiteral("wildos-mail"), QStringLiteral("evolution-calendar-factory")}, QStringLiteral("wildos-mail")},
        {QStringLiteral("remote"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Remote")), {QStringLiteral("wildos-remote")}, QStringLiteral("wildos-remote")},
        {QStringLiteral("pdf"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "PDF")), {QStringLiteral("wildos-pdf")}, QStringLiteral("wildos-pdf")},
        {QStringLiteral("notes"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Notes")), {QStringLiteral("wildos-notes")}, QStringLiteral("wildos-notes")},
        {QStringLiteral("capture"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Capture")), {QStringLiteral("wildos-capture"), QStringLiteral("wildos-screenshot")},
         QStringLiteral("wildos-capture")},
        {QStringLiteral("music"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Music")), {QStringLiteral("wildos-music")}, QStringLiteral("wildos-music")},
        {QStringLiteral("videos"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Videos")), {QStringLiteral("wildos-videos")}, QStringLiteral("wildos-videos")},
        {QStringLiteral("taskmanager"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Task Manager")), {QStringLiteral("wildos-taskmanager")}, QStringLiteral("wildos-taskmanager")},
        {QStringLiteral("terminal"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Terminal")), {QStringLiteral("wildos-terminal")}, QStringLiteral("wildos-terminal")},
        {QStringLiteral("office"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Write and Tables")), {QStringLiteral("soffice.bin"), QStringLiteral("oosplash")},
         QStringLiteral("wildos-office")},
        {QStringLiteral("windows"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Windows mode")),
         {QStringLiteral("wildos-winmode"), QStringLiteral("wildos-winmode-agent"), QStringLiteral("wildos-rdp"), QStringLiteral("wild-run"),
          QStringLiteral("wildos-appbridge")},
         QStringLiteral("wildos-winmode")},
        {QStringLiteral("desktop"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Desktop, Start and taskbar")),
         {QStringLiteral("plasmashell"), QStringLiteral("kwin_wayland"), QStringLiteral("ksmserver"), QStringLiteral("startplasma-wayland")},
         QStringLiteral("wildos-shell")},
        {QStringLiteral("signin"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Signing in and the lock screen")),
         {QStringLiteral("sddm"), QStringLiteral("sddm-greeter-qt6"), QStringLiteral("kscreenlocker_greet")}, QStringLiteral("wildos-branding")},
        {QStringLiteral("network"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Wi-Fi and network")),
         {QStringLiteral("NetworkManager"), QStringLiteral("wpa_supplicant"), QStringLiteral("nm-applet")}, {}},
        {QStringLiteral("bluetooth"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Bluetooth")), {QStringLiteral("bluetoothd")}, {}},
        {QStringLiteral("sound"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Sound")),
         {QStringLiteral("pipewire"), QStringLiteral("wireplumber"), QStringLiteral("pipewire-pulse")}, {}},
        {QStringLiteral("updates"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Updates")),
         {QStringLiteral("packagekitd"), QStringLiteral("unattended-upgrade"), QStringLiteral("apt"), QStringLiteral("apt-get"),
          QStringLiteral("wildos-store")},
         {}},
        {QStringLiteral("welcome"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Welcome")), {QStringLiteral("wildos-welcome")}, QStringLiteral("wildos-welcome")},
        {QStringLiteral("system"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Starting up, sleep or shutting down")), {}, {}},
        {QStringLiteral("other"), QStringLiteral(QT_TRANSLATE_NOOP("Parts", "Something else")), {}, {}},
    };
    return parts;
}

const Part &byId(const QString &id)
{
    for (const Part &part : all()) {
        if (part.id == id) {
            return part;
        }
    }
    return all().last();
}

bool known(const QString &id)
{
    for (const Part &part : all()) {
        if (part.id == id) {
            return true;
        }
    }
    return false;
}

const Part &byProgram(const QString &program)
{
    const QString name = QFileInfo(program).fileName();
    for (const Part &part : all()) {
        if (part.programs.contains(name)) {
            return part;
        }
    }
    return all().last();
}

QString shownName(const Part &part)
{
    return QCoreApplication::translate("Parts", part.name.toUtf8().constData());
}

QVariantList choices()
{
    QVariantList list;
    for (const Part &part : all()) {
        list.append(QVariantMap{{QStringLiteral("value"), part.id}, {QStringLiteral("text"), shownName(part)}});
    }
    return list;
}
}
