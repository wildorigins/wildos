cmake_minimum_required(VERSION 3.25)
project(wildos-settings VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Network Qml Quick QuickControls2)
find_package(KF6KCMUtils REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6Archive REQUIRED)
find_package(Qt6Svg 6.8 REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6ColorScheme REQUIRED)
find_package(KF6Sonnet REQUIRED)
find_package(KF6Screen REQUIRED)
find_package(KF6NetworkManagerQt REQUIRED)
find_package(KF6GlobalAccel REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(packagekitqt6 REQUIRED)
find_package(Cups REQUIRED)
find_package(SDL2 2.0.16 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(PWQUALITY REQUIRED IMPORTED_TARGET pwquality)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)
find_package(PolkitQt6-1 REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-settings src/main.cpp)
target_compile_definitions(wildos-settings PRIVATE WILDOS_SETTINGS_VERSION="${PROJECT_VERSION}")

file(GLOB_RECURSE QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/components/Metrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/Pages.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/Navigator.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/Format.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/Protection.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/HistoryWords.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(wildos-settings
    URI WildOS.Settings
    QML_FILES ${QML_SOURCES}
    SOURCES
        src/kcmhost.cpp src/kcmhost.h
        src/system.cpp src/system.h
        src/themes.cpp src/themes.h
        src/updates.cpp src/updates.h
        src/appsmodel.cpp src/appsmodel.h
        src/windowsapps.cpp src/windowsapps.h
        src/windowsmode.cpp src/windowsmode.h
        src/windowsusb.cpp src/windowsusb.h
        src/processor.cpp src/processor.h
        src/compat32.cpp src/compat32.h
        src/graphics.cpp src/graphics.h
        src/inputdevices.cpp src/inputdevices.h
        src/defaultapps.cpp src/defaultapps.h
        src/clock.cpp src/clock.h
        src/securetime.cpp src/securetime.h
        src/startupapps.cpp src/startupapps.h
        src/keyboard.cpp src/keyboard.h
        src/printers.cpp src/printers.h
        src/users.cpp src/users.h
        src/fingerprint.cpp src/fingerprint.h
        src/passwordcheck.cpp src/passwordcheck.h
        src/language.cpp src/language.h
        src/appaccess.cpp src/appaccess.h
        src/poweroptions.cpp src/poweroptions.h
        src/appnotifications.cpp src/appnotifications.h
        src/desktops.cpp src/desktops.h
        src/gestures.cpp src/gestures.h
        src/autoplay.cpp src/autoplay.h
        src/gamecontrollers.cpp src/gamecontrollers.h
        src/thunderbolt.cpp src/thunderbolt.h
        src/spelling.cpp src/spelling.h
        src/fonts.cpp src/fonts.h
        src/display.cpp src/display.h
        src/filesearch.cpp src/filesearch.h
        src/activityhistory.cpp src/activityhistory.h
        src/olderapps.cpp src/olderapps.h
        src/network.cpp src/network.h src/netshared.h
        src/soundtheme.cpp src/soundtheme.h
        src/accessibility.cpp src/accessibility.h
        src/recovery.cpp src/recovery.h
        src/freespace.cpp src/freespace.h
        src/firewall.cpp src/firewall.h
        src/advanced.cpp src/advanced.h
        src/history.cpp src/history.h
        src/devices.cpp src/devices.h
        src/accesscore.cpp src/accesscore.h
)

target_include_directories(wildos-settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Fingerprint setup's print, Fluent UI System Icons' fingerprint_48_regular
# exactly as Microsoft publishes it (MIT, data/fluent/LICENSE).
qt_add_resources(wildos-settings "fluent"
    PREFIX "/fluent"
    BASE data/fluent
    FILES data/fluent/fingerprint_48_regular.svg
)

target_link_libraries(wildos-settings PRIVATE
    Qt6::Widgets Qt6::DBus Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::KCMUtils KF6::KCMUtilsQuick KF6::ConfigCore KF6::CoreAddons KF6::Service KF6::I18n KF6::ColorScheme KF6::SonnetCore
    KF6::WindowSystem
    KF6::Screen
    KF6::NetworkManagerQt
    PK::packagekitqt6
    Cups::Cups
    SDL2::SDL2
    PkgConfig::GLIB
    PkgConfig::PWQUALITY
    PkgConfig::UDEV
    PolkitQt6-1::Agent PolkitQt6-1::Core
    util
)

install(TARGETS wildos-settings RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
# The helpers are looked up by these exact paths (polkit pins the update
# helper to it), so they do not follow the distribution's libexec layout.
install(PROGRAMS data/settings-update-policy data/settings-users data/settings-locale data/settings-protection data/settings-firewall data/settings-firewall-off data/settings-history data/settings-devices data/settings-network data/settings-cpu-threads data/settings-32bit data/settings-game-overlay data/settings-graphics-driver data/settings-brave-spelling data/settings-fingerprint data/settings-free-space data/settings-advanced data/advanced-protection-apply DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
install(FILES data/org.wildos.settings.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
# Privacy and security > Advanced protection: the hooks settings-advanced
# runs for the choices Settings carries out itself, and the services behind
# them. Other packages add hooks of their own to the same folder.
install(PROGRAMS data/advanced-protection.d/usb_locked data/advanced-protection.d/radios_idle_off data/advanced-protection.d/dns
    data/advanced-protection.d/dns_provider data/advanced-protection.d/dns_strict data/advanced-protection.d/vpn_only
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/wildos/advanced-protection.d)
install(PROGRAMS data/wildos-usb-lock data/wildos-radios-idle data/wildos-advanced-session DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
install(FILES data/wildos-usb-lock.service data/wildos-radios-idle.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
install(FILES data/wildos-advanced-session.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
# Opened from About > Credits. Not under /usr/share/doc, which the ISO
# leaves out.
install(FILES data/fluent/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/wildos-settings/licences RENAME fluent-ui-system-icons.txt)

# The automatic theme runs in every session and must not pull in the
# Settings app's QML and KCM dependencies.
qt_add_executable(wildos-auto-theme src/autotheme/main.cpp src/autotheme/sun.h)
target_link_libraries(wildos-auto-theme PRIVATE Qt6::Core Qt6::Gui Qt6::DBus KF6::ConfigCore)
install(TARGETS wildos-auto-theme RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.auto-theme.desktop DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)
# The accessibility shortcuts: a service in every session and the switches
# the lock and sign-in screens run. It shares AccessCore with Settings so a
# shortcut and its switch do the same thing.
qt_add_executable(wildos-access src/access/main.cpp src/accesscore.cpp src/accesscore.h)
target_include_directories(wildos-access PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildos-access PRIVATE Qt6::Core Qt6::Gui Qt6::DBus KF6::ConfigCore KF6::GlobalAccel)
install(TARGETS wildos-access RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.access.desktop DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)
# Hands Typing's spelling languages to Brave, from Settings and once a session.
qt_add_executable(wildos-brave-spelling src/bravespelling/main.cpp)
target_link_libraries(wildos-brave-spelling PRIVATE Qt6::Core KF6::SonnetCore)
install(TARGETS wildos-brave-spelling RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
install(FILES data/org.wildos.brave-spelling.desktop DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)
# Turns a Windows pointer, pointer pack or picture into the "Your own"
# pointer theme, and copies it to the sign-in screen for greeter-access. A
# process of its own, so an untrusted file can never take Settings down.
qt_add_executable(pointer-import src/pointerimport/main.cpp src/pointerimport/wincursor.cpp src/pointerimport/xcursor.cpp src/pointerimport/cursor.h)
target_link_libraries(pointer-import PRIVATE Qt6::Gui Qt6::Svg KF6::Archive)
install(TARGETS pointer-import RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
# Mono audio: a PipeWire filter run as its own user service.
install(FILES data/wildos-mono-audio.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/pipewire)
install(FILES data/wildos-mono-audio.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
# Loads the firewall for a Strict or Maximum protection level at start-up.
install(FILES data/wildos-protection.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
# System > History's records: brought up to date every hour and whenever
# the page opens (settings-history).
install(FILES data/wildos-history.socket data/wildos-history@.service data/wildos-history.service data/wildos-history.timer
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
# Free up space's Tidy up automatically, switched on per person from
# Settings with systemctl --user.
install(FILES data/wildos-free-space.service data/wildos-free-space.timer
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
# System > Devices: the kernel's firmware complaints and the drives' own
# health checks, which only root can read (settings-devices).
install(FILES data/wildos-devices.socket data/wildos-devices@.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
install(FILES data/fprintd-wildos-history.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/fprintd.service.d
    RENAME wildos-history.conf)

# The root half of Advanced network settings and the firewall: encrypted DNS
# for each new network, the VPN kill switch, loading the firewall again as
# networks change and keeping the list of what it blocked. A system
# service, so it carries none of the Settings app's QML or KDE dependencies.
qt_add_executable(wildos-network src/networkd/main.cpp src/networkd/blocked.cpp src/networkd/blocked.h src/netshared.h)
target_include_directories(wildos-network PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildos-network PRIVATE Qt6::Core Qt6::DBus Qt6::Network)
install(TARGETS wildos-network RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
install(FILES data/wildos-network.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
install(FILES data/org.wildos.Network.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d)
install(FILES data/90-wildos-network.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/NetworkManager/conf.d)
install(FILES data/60-wildos-resolved.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/resolved.conf.d)
