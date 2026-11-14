cmake_minimum_required(VERSION 3.25)
project(wildos-capture VERSION 0.1.4 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Qml Quick QuickControls2 WaylandClient)
find_package(KF6WindowSystem REQUIRED)
find_package(KF6GuiAddons REQUIRED)
find_package(KF6Notifications REQUIRED)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6Purpose REQUIRED)
find_package(KPipeWire REQUIRED)
find_package(PlasmaWaylandProtocols REQUIRED)
find_package(WaylandScanner REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)
# Scanning goes through SANE: sane-airscan for driverless network scanners,
# SANE's own backends for USB ones.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SANE REQUIRED IMPORTED_TARGET sane-backends)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-capture src/main.cpp)
target_compile_definitions(wildos-capture PRIVATE WILDOS_CAPTURE_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/CaptureIcons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-capture
    URI WildOS.Capture
    QML_FILES ${QML_SOURCES}
    RESOURCES data/wildos-capture.svg
    SOURCES
        src/captureapp.cpp src/captureapp.h
        src/grabber.cpp src/grabber.h
        src/recorder.cpp src/recorder.h
        src/sharer.cpp src/sharer.h
        src/frozenimage.cpp src/frozenimage.h
        src/scanner.cpp src/scanner.h
        src/scanworker.cpp src/scanworker.h
        src/scanprocess.cpp src/scanprocess.h
)

# The recording stream comes from KWin's screencast protocol.
qt6_generate_wayland_protocol_client_sources(wildos-capture FILES
    ${PLASMA_WAYLAND_PROTOCOLS_DIR}/zkde-screencast-unstable-v1.xml
)

target_include_directories(wildos-capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(wildos-capture PRIVATE
    Qt6::Widgets Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::WaylandClient
    KF6::Notifications KF6::DBusAddons KF6::GuiAddons KF6::WindowSystem KF6::ConfigCore KF6::Purpose
    K::KPipeWireRecord
    PkgConfig::SANE
    Wayland::Client
)

# The program keeps its old name: KWin grants screenshots by matching the
# running program against the Exec line of org.wildos.screenshot.desktop,
# and until the next sign-in it still holds the old Exec. Renaming the
# program would leave Print Screen broken after an update.
set_target_properties(wildos-capture PROPERTIES OUTPUT_NAME wildos-screenshot)
install(TARGETS wildos-capture RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# The id stays org.wildos.screenshot: Print Screen and Win+Shift+S are bound
# to it in every account's kglobalshortcutsrc, and KWin's window list script
# calls back on the D-Bus name of the same name.
install(FILES data/org.wildos.screenshot.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.screenshot.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/wildos-screenshot.notifyrc DESTINATION ${CMAKE_INSTALL_DATADIR}/knotifications6)
install(FILES data/wildos-capture.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
