cmake_minimum_required(VERSION 3.25)
project(wildos-welcome VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Qml Quick QuickControls2 Concurrent Svg Network Sql)
find_package(KF6Config REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6JobWidgets REQUIRED)
find_package(packagekitqt6 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIVEX REQUIRED IMPORTED_TARGET hivex)
pkg_check_modules(PWQUALITY REQUIRED IMPORTED_TARGET pwquality)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-welcome src/main.cpp)
target_compile_definitions(wildos-welcome PRIVATE WILDOS_WELCOME_VERSION="${PROJECT_VERSION}")

file(GLOB_RECURSE QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/components/Metrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(wildos-welcome
    URI WildOS.Welcome
    QML_FILES ${QML_SOURCES}
    RESOURCES images/wild-os-icon.svg data/windows-programs.json
    SOURCES
        src/setup.cpp src/setup.h
        src/windowsimport.cpp src/windowsimport.h
        src/bookmarks.cpp src/bookmarks.h
        src/privacy.cpp src/privacy.h
        src/background.cpp src/background.h
        src/firstboot.cpp src/firstboot.h
        src/wifi.cpp src/wifi.h
        src/windowsprograms.cpp src/windowsprograms.h
        src/firstrun.cpp src/firstrun.h
        src/passwordcheck.cpp src/passwordcheck.h
)

target_include_directories(wildos-welcome PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wildos-welcome PRIVATE
    Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Svg Qt6::Network Qt6::Sql
    KF6::ConfigCore KF6::CoreAddons KF6::JobWidgets
    PK::packagekitqt6
    PkgConfig::HIVEX
    PkgConfig::PWQUALITY
)

install(TARGETS wildos-welcome RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.welcome.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/windows-programs.json DESTINATION ${CMAKE_INSTALL_DATADIR}/wildos-welcome)
install(FILES data/autostart/org.wildos.welcome.desktop DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart)

# Translations: i18n/<package>_<lang>.ts, compiled to .qm and loaded by
# WildOS.UI for the language chosen in Settings.
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
file(GLOB TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/*.ts)
set(QM_FILES)
foreach(ts ${TS_FILES})
    get_filename_component(name ${ts} NAME_WE)
    set(qm ${CMAKE_CURRENT_BINARY_DIR}/i18n/${name}.qm)
    add_custom_command(OUTPUT ${qm}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/i18n
        COMMAND Qt6::lrelease -silent ${ts} -qm ${qm}
        DEPENDS ${ts} VERBATIM)
    list(APPEND QM_FILES ${qm})
endforeach()
add_custom_target(translations ALL DEPENDS ${QM_FILES})
install(FILES ${QM_FILES} DESTINATION ${CMAKE_INSTALL_DATADIR}/wildos/translations)
