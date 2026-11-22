#pragma once

#include <QColor>
#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

#include <KConfigWatcher>

// Everything the Wild OS pages read about the computer, and the small set of
// changes they make directly. Each change goes through the same tool or
// D-Bus service the desktop itself uses, so the rest of Plasma notices.
class System : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString osName READ osName CONSTANT)
    Q_PROPERTY(QString osVersion READ osVersion CONSTANT)
    Q_PROPERTY(QString kernel READ kernel CONSTANT)
    Q_PROPERTY(QString deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(QString processor READ processor CONSTANT)
    Q_PROPERTY(QString memory READ memory CONSTANT)
    Q_PROPERTY(QString architecture READ architecture CONSTANT)
    Q_PROPERTY(QString secureBoot READ secureBoot CONSTANT)
    Q_PROPERTY(QString hostName READ hostName NOTIFY hostNameChanged)

    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(QString userFullName READ userFullName NOTIFY userChanged)
    Q_PROPERTY(QString userAvatar READ userAvatar NOTIFY userChanged)
    Q_PROPERTY(bool userIsAdmin READ userIsAdmin CONSTANT)

    Q_PROPERTY(QVariantMap security READ security NOTIFY securityChanged)
    Q_PROPERTY(QDateTime securityChecked READ securityChecked NOTIFY securityChanged)
    Q_PROPERTY(bool securityChecking READ securityChecking NOTIFY securityCheckingChanged)
    Q_PROPERTY(bool locationServices READ locationServices CONSTANT)

    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)
    Q_PROPERTY(QString colourScheme READ colourScheme NOTIFY colourSchemeChanged)
    // On while wildos-auto-theme picks the theme by time of day.
    Q_PROPERTY(bool automaticTheme READ automaticTheme NOTIFY colourSchemeChanged)
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QString lockWallpaper READ lockWallpaper NOTIFY lockWallpaperChanged)
    Q_PROPERTY(int taskbarHiding READ taskbarHiding NOTIFY taskbarHidingChanged)
    Q_PROPERTY(bool taskbarCentred READ taskbarCentred NOTIFY taskbarCentredChanged)
    // Floating panels instead of the docked bar (wildos-shell reads it).
    Q_PROPERTY(bool taskbarFloating READ taskbarFloating NOTIFY taskbarFloatingChanged)
    // The taskbar along the top of the screen instead of the bottom.
    Q_PROPERTY(bool taskbarTop READ taskbarTop NOTIFY taskbarTopChanged)
    // The taskbar shows windows from every desktop, not only the one in
    // use. Read on demand, as it costs a call to plasmashell.
    Q_PROPERTY(bool taskbarAllDesktops READ taskbarAllDesktops NOTIFY taskbarAllDesktopsChanged)
    Q_PROPERTY(bool desktopWidgets READ desktopWidgets NOTIFY desktopWidgetsChanged)
    Q_PROPERTY(bool taskbarWidgets READ taskbarWidgets NOTIFY taskbarWidgetsChanged)
    Q_PROPERTY(QVariantList addedTaskbarWidgets READ addedTaskbarWidgets NOTIFY addedTaskbarWidgetsChanged)
    // Windows mode's status at the taskbar's left end (org.wildos.winmode),
    // which only shows once Windows mode is set up. Read on demand too.
    Q_PROPERTY(bool taskbarWinModeStatus READ taskbarWinModeStatus NOTIFY taskbarWinModeStatusChanged)
    // The sliver at the taskbar's far right end that shows the desktop, and
    // whether resting the pointer on it shows the desktop until it moves
    // away (wildosrc [Taskbar], which the panel in wildos-shell follows).
    Q_PROPERTY(bool taskbarShowDesktop READ taskbarShowDesktop WRITE setTaskbarShowDesktop NOTIFY taskbarShowDesktopChanged)
    Q_PROPERTY(bool taskbarPeek READ taskbarPeek WRITE setTaskbarPeek NOTIFY taskbarShowDesktopChanged)
    Q_PROPERTY(QString wallpaperName READ wallpaperName NOTIFY wallpaperChanged)
    Q_PROPERTY(bool slideshow READ slideshow NOTIFY wallpaperChanged)
    Q_PROPERTY(QString slideshowFolder READ slideshowFolder NOTIFY wallpaperChanged)
    Q_PROPERTY(bool transparency READ transparency NOTIFY transparencyChanged)
    Q_PROPERTY(int textScale READ textScale NOTIFY textScaleChanged)
    Q_PROPERTY(QVariantMap updatePolicy READ updatePolicy NOTIFY updatePolicyChanged)
    // standard, strict or maximum (Privacy and security), and whether a
    // change is being applied.
    Q_PROPERTY(QString protectionLevel READ protectionLevel NOTIFY protectionLevelChanged)
    Q_PROPERTY(bool protectionBusy READ protectionBusy NOTIFY protectionLevelChanged)
    Q_PROPERTY(int appUpdates READ appUpdates NOTIFY appUpdatesChanged)
    Q_PROPERTY(QString storeApp READ storeApp CONSTANT)
    Q_PROPERTY(QString powerProfile READ powerProfile NOTIFY powerChanged)
    Q_PROPERTY(QStringList powerProfiles READ powerProfiles NOTIFY powerChanged)

    Q_PROPERTY(bool snapEdges READ snapEdges WRITE setSnapEdges NOTIFY kwinChanged)
    Q_PROPERTY(bool snapLayouts READ snapLayouts WRITE setSnapLayouts NOTIFY kwinChanged)
    Q_PROPERTY(bool snapAssist READ snapAssist WRITE setSnapAssist NOTIFY kwinChanged)
    Q_PROPERTY(bool altTabAllDesktops READ altTabAllDesktops WRITE setAltTabAllDesktops NOTIFY kwinChanged)
    // Hot corners, as wildosrc [HotCorners] holds them for KWin's
    // wildos_hotcorners plugin: TopLeft, TopRight, BottomLeft, BottomRight
    // (an action), the same with Key after them (a key to hold), Delay (ms)
    // and SkipFullScreen.
    Q_PROPERTY(QVariantMap hotCorners READ hotCorners NOTIFY hotCornersChanged)

    Q_PROPERTY(QString timeZone READ timeZone CONSTANT)
    Q_PROPERTY(QString region READ region CONSTANT)

public:
    explicit System(QObject *parent = nullptr);

    QString osName() const { return m_osName; }
    QString osVersion() const { return m_osVersion; }
    QString kernel() const;
    QString deviceModel() const { return m_deviceModel; }
    QString processor() const { return m_processor; }
    QString memory() const { return m_memory; }
    QString architecture() const;
    QString secureBoot() const;
    QString hostName() const;

    QString userName() const;
    QString userFullName() const;
    QString userAvatar() const;
    bool userIsAdmin() const;

    QVariantMap security() const { return m_security; }
    QDateTime securityChecked() const { return m_securityChecked; }
    bool securityChecking() const { return m_securityChecking; }
    bool locationServices() const;

    QColor accent() const { return m_accent; }
    QString colourScheme() const { return m_colourScheme; }
    bool automaticTheme() const { return m_automaticTheme; }
    QString wallpaper() const { return m_wallpaper; }
    QString lockWallpaper() const { return m_lockWallpaper; }
    int taskbarHiding() const { return m_taskbarHiding; }
    bool taskbarCentred() const { return m_taskbarCentred; }
    bool taskbarFloating() const { return m_taskbarFloating; }
    bool taskbarTop() const { return m_taskbarTop; }
    bool taskbarAllDesktops() const { return m_taskbarAllDesktops; }
    bool desktopWidgets() const { return m_desktopWidgets; }
    bool taskbarWidgets() const { return m_taskbarWidgets; }
    QVariantList addedTaskbarWidgets() const { return m_addedTaskbarWidgets; }
    bool taskbarWinModeStatus() const { return m_taskbarWinModeStatus; }
    bool taskbarShowDesktop() const;
    void setTaskbarShowDesktop(bool on);
    bool taskbarPeek() const;
    void setTaskbarPeek(bool on);
    QString wallpaperName() const;
    bool slideshow() const { return m_slideshow; }
    QString slideshowFolder() const { return m_slideshowFolder; }
    bool transparency() const;
    int textScale() const { return m_textScale; }
    QVariantMap updatePolicy() const { return m_updatePolicy; }
    QString protectionLevel() const { return m_protectionLevel; }
    bool protectionBusy() const { return m_protectionBusy; }
    int appUpdates() const { return m_appUpdates; }
    QString storeApp() const;
    QString powerProfile() const { return m_powerProfile; }
    QStringList powerProfiles() const { return m_powerProfiles; }

    bool snapEdges() const;
    void setSnapEdges(bool on);
    bool snapLayouts() const;
    void setSnapLayouts(bool on);
    bool snapAssist() const;
    void setSnapAssist(bool on);
    bool altTabAllDesktops() const;
    void setAltTabAllDesktops(bool on);
    QVariantMap hotCorners() const;
    Q_INVOKABLE void setHotCorner(const QString &key, const QVariant &value);
    Q_INVOKABLE void resetHotCorners();

    QString timeZone() const;
    QString region() const;

    Q_INVOKABLE void refreshSecurity();
    Q_INVOKABLE void renameHost(const QString &name);

    Q_INVOKABLE QVariantList volumes() const;
    Q_INVOKABLE qint64 binSize() const;
    Q_INVOKABLE void emptyBin();

    Q_INVOKABLE QVariantList wallpapers() const;
    Q_INVOKABLE QString thumbnail(const QString &path) const;
    Q_INVOKABLE void setWallpaper(const QString &path);
    Q_INVOKABLE void setLockWallpaper(const QString &path);
    Q_INVOKABLE void setAccent(const QColor &colour);
    // Light or dark: WildOS or WildOSLight, once that scheme is installed.
    Q_INVOKABLE bool hasColourScheme(const QString &name) const;
    Q_INVOKABLE void setColourScheme(const QString &name);
    // A scheme and an accent together (a theme), applied one after the
    // other so neither undoes the other.
    Q_INVOKABLE void setColours(const QString &name, const QColor &accent);
    Q_INVOKABLE void setAutomaticTheme();
    Q_INVOKABLE void setTaskbarHiding(int mode);
    Q_INVOKABLE void setTaskbarCentred(bool centred);
    Q_INVOKABLE void setTaskbarFloating(bool floating);
    Q_INVOKABLE void setTaskbarTop(bool top);
    Q_INVOKABLE void readTaskbarDesktops();
    Q_INVOKABLE void setTaskbarAllDesktops(bool all);
    Q_INVOKABLE void readDesktopWidgets();
    Q_INVOKABLE void setDesktopWidgets(bool allowed);
    Q_INVOKABLE void readTaskbarWidgets();
    Q_INVOKABLE void setTaskbarWidgets(bool allowed);
    Q_INVOKABLE void openWidgetExplorer();
    Q_INVOKABLE void readAddedTaskbarWidgets();
    Q_INVOKABLE void removeTaskbarWidget(int id);
    Q_INVOKABLE void readTaskbarWinModeStatus();
    Q_INVOKABLE void setTaskbarWinModeStatus(bool shown);
    Q_INVOKABLE void setSlideshow(bool on, const QString &folder = {});
    Q_INVOKABLE void setTransparency(bool on);
    Q_INVOKABLE void setTextScale(int percent);
    Q_INVOKABLE void setUsbProtection(bool on);
    Q_INVOKABLE bool canRestartToFirmware() const;
    Q_INVOKABLE void restartToFirmware();
    Q_INVOKABLE void setUpdatePolicy(const QString &key, const QString &value);
    Q_INVOKABLE void refreshUpdatePolicy();
    Q_INVOKABLE void setProtectionLevel(const QString &level);
    Q_INVOKABLE void refreshAppUpdates();
    Q_INVOKABLE void openStore(const QString &mode = {});

    Q_INVOKABLE QVariant config(const QString &file, const QString &group, const QString &key, const QVariant &fallback) const;
    Q_INVOKABLE void setConfig(const QString &file, const QString &group, const QString &key, const QVariant &value);
    Q_INVOKABLE void reloadKWin();
    Q_INVOKABLE void reloadPowerManagement();
    Q_INVOKABLE void reloadScreenLocker();
    Q_INVOKABLE void refreshPower();
    Q_INVOKABLE void setPowerProfile(const QString &profile);

    Q_INVOKABLE bool hasProgram(const QString &program) const;
    Q_INVOKABLE void launch(const QString &program, const QStringList &arguments = {});
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;

Q_SIGNALS:
    void hostNameChanged();
    void userChanged();
    void renameFailed(const QString &reason);
    void securityChanged();
    void securityCheckingChanged();
    void accentChanged();
    void colourSchemeChanged();
    void wallpaperChanged();
    void lockWallpaperChanged();
    void taskbarHidingChanged();
    void taskbarCentredChanged();
    void taskbarFloatingChanged();
    void taskbarTopChanged();
    void taskbarAllDesktopsChanged();
    void desktopWidgetsChanged();
    void taskbarWidgetsChanged();
    void addedTaskbarWidgetsChanged();
    void taskbarWinModeStatusChanged();
    void taskbarShowDesktopChanged();
    void transparencyChanged();
    void textScaleChanged();
    void updatePolicyChanged();
    void updatePolicyFailed(const QString &reason);
    void protectionLevelChanged();
    void protectionLevelFailed(const QString &reason);
    void usbProtectionFailed(const QString &reason);
    void firmwareRestartFailed(const QString &reason);
    void appUpdatesChanged();
    void powerChanged();
    void kwinChanged();
    void hotCornersChanged();
    void binEmptied();

private:
    void readOsRelease();
    void readHardware();
    void readAccent();
    void readColourScheme();
    void setThemeMode(const QString &mode);
    void readWallpaper();
    void readLockWallpaper();
    void readTaskbar();
    void readTextScale();
    void readTaskbarLayout();
    void reconfigureKWin();
    void applySnapEffect(bool layouts, bool assist);
    QString evaluatePlasmaScript(const QString &script) const;

    QString m_osName;
    QString m_osVersion;
    QString m_deviceModel;
    QString m_processor;
    QString m_memory;
    QString m_hostName;

    QVariantMap m_security;
    QDateTime m_securityChecked;
    bool m_securityChecking = false;

    QColor m_accent;
    QString m_colourScheme;
    bool m_automaticTheme = false;
    QString m_wallpaper;
    QString m_lockWallpaper;
    int m_taskbarHiding = 0;
    bool m_taskbarCentred = true;
    bool m_taskbarFloating = false;
    bool m_taskbarTop = false;
    bool m_taskbarAllDesktops = false;
    bool m_desktopWidgets = false;
    bool m_taskbarWidgets = false;
    QVariantList m_addedTaskbarWidgets;
    bool m_taskbarWinModeStatus = true;
    bool m_slideshow = false;
    QString m_slideshowFolder;
    int m_textScale = 100;
    QVariantMap m_updatePolicy;
    QString m_protectionLevel;
    bool m_protectionBusy = false;
    void readProtectionLevel();
    void applyLockTimes(const QString &level);
    int m_appUpdates = -1;
    QString m_powerProfile;
    QStringList m_powerProfiles;

    KConfigWatcher::Ptr m_globals;
};
