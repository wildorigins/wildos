cmake_minimum_required(VERSION 3.25)
project(wildos-store VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Network Qml Quick QuickControls2)
find_package(KF6KIO REQUIRED)
find_package(KF6Service REQUIRED)
find_package(packagekitqt6 REQUIRED)
find_package(AppStreamQt 1.0 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FLATPAK REQUIRED IMPORTED_TARGET flatpak)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-store src/main.cpp)
target_compile_definitions(wildos-store PRIVATE
    WILDOS_STORE_VERSION="${PROJECT_VERSION}"
    WILDOS_STORE_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/wildos-store"
    # GLib headers use "signals" as an identifier.
    QT_NO_KEYWORDS
)

file(GLOB_RECURSE QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/components/Metrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(wildos-store
    URI WildOS.Store
    QML_FILES ${QML_SOURCES}
    RESOURCES
        data/curated.json
        data/flathub.flatpakrepo
        data/heroes/contours.jpg
        data/photos/create-timeline.jpg
        data/photos/make-krita.jpg
        data/photos/play-controller.jpg
        data/photos/swap-desk.jpg
        data/heroes/dusk-hills.jpg
        data/heroes/waves-green.jpg
        data/org.wildos.store.svg
    SOURCES
        src/catalog.cpp src/catalog.h
        src/storeapp.cpp src/storeapp.h
        src/packagekitbackend.cpp src/packagekitbackend.h
        src/packagesizes.cpp src/packagesizes.h
        src/flatpakbackend.cpp src/flatpakbackend.h
        src/localpackage.cpp src/localpackage.h
        src/network.cpp src/network.h
        src/themeiconprovider.h src/timing.h
)

target_include_directories(wildos-store PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wildos-store PRIVATE
    Qt6::Gui Qt6::DBus Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::KIOGui KF6::Service
    PK::packagekitqt6
    AppStreamQt
    PkgConfig::FLATPAK
)

install(TARGETS wildos-store RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.store.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/curated.json DESTINATION ${CMAKE_INSTALL_DATADIR}/wildos-store)
install(FILES data/org.wildos.store.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# Apps listed in the Wild OS section before they are installed need their
# Wild OS theme icon to be here already (Ganger's package used to ship it).
install(FILES data/icons/wildos-ganger.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/WildOS/scalable/apps)
install(FILES data/60wildos-store-icons DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/apt/apt.conf.d)
install(FILES data/wildos-store-update.service data/wildos-store-update.timer DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
