cmake_minimum_required(VERSION 3.25)
project(wildos-portal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Qml Quick QuickControls2)
find_package(KF6KIO REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-portal src/main.cpp)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-portal
    URI WildOS.Portal
    QML_FILES ${QML_SOURCES}
    SOURCES
        src/dbustypes.cpp src/dbustypes.h
        src/format.cpp src/format.h
        src/dirmodel.cpp src/dirmodel.h
        src/chooser.cpp src/chooser.h
        src/filechooser.cpp src/filechooser.h
        src/clash.cpp src/clash.h
        src/windows.cpp src/windows.h
        src/thumbnails.cpp src/thumbnails.h
)

target_include_directories(wildos-portal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wildos-portal PRIVATE
    Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::KIOCore KF6::KIOGui KF6::ConfigCore KF6::CoreAddons KF6::Service KF6::WindowSystem
)

install(TARGETS wildos-portal RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
install(FILES data/wildos.portal DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
install(FILES
    data/org.freedesktop.impl.portal.desktop.wildos.service
    data/org.wildos.Dialogs.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES data/wildos-portal.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
install(FILES data/60-wildos-portal.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/environment.d)
# The KDE desktop's portal preferences, read before the stock ones in
# /usr/share/xdg-desktop-portal.
install(FILES data/kde-portals.conf DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/xdg-desktop-portal)
