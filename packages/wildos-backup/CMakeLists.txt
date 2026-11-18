cmake_minimum_required(VERSION 3.25)
project(wildos-backup VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Qml Quick QuickControls2)
find_package(Qt6Keychain REQUIRED)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6Notifications REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-backup src/main.cpp)
target_compile_definitions(wildos-backup PRIVATE WILDOS_BACKUP_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/BackupIcons.qml qml/BackupStyle.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-backup
    URI WildOS.Backup
    QML_FILES ${QML_SOURCES}
    RESOURCES data/wildos-backup.svg
    SOURCES
        src/paths.h
        src/settings.cpp src/settings.h
        src/keyring.cpp src/keyring.h
        src/restic.cpp src/restic.h
        src/plan.cpp src/plan.h
        src/driveclass.cpp src/driveclass.h
        src/drives.cpp src/drives.h
        src/history.cpp src/history.h
        src/windowsmode.cpp src/windowsmode.h
        src/schedule.cpp src/schedule.h
        src/runner.cpp src/runner.h
        src/watcher.cpp src/watcher.h
        src/sizes.cpp src/sizes.h
        src/backupstatus.cpp src/backupstatus.h
        src/setup.cpp src/setup.h
        src/restore.cpp src/restore.h
)

target_include_directories(wildos-backup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildos-backup PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::DBusAddons KF6::Notifications KF6::WindowSystem
    qt6keychain
)

install(TARGETS wildos-backup RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.backup.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.backup.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/wildos-backup.notifyrc DESTINATION ${CMAKE_INSTALL_DATADIR}/knotifications6)
# Here rather than in the WildOS icon theme, so Backup has its icon under
# any theme, and the splash reads it from here.
install(FILES data/wildos-backup.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# For every account, but switched on per person only once they set a
# backup up (Schedule::apply), never globally.
install(FILES
    data/systemd/wildos-backup.timer
    data/systemd/wildos-backup@.service
    data/systemd/wildos-backup-watch.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
install(FILES data/systemd/manual.d/50-now.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user/wildos-backup@manual.service.d)
install(PROGRAMS data/windows-disk data/make-folder DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos-backup)
install(FILES data/org.wildos.backup.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)

option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(driveclass-test tests/driveclass-test.cpp src/driveclass.cpp)
    target_include_directories(driveclass-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(driveclass-test PRIVATE Qt6::Test Qt6::DBus)
    add_test(NAME driveclass COMMAND driveclass-test)
endif()

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
