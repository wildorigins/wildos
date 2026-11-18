cmake_minimum_required(VERSION 3.25)
project(wildos-taskmanager VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Network Qml Quick QuickControls2)
find_package(KSysGuard REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Service REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-taskmanager src/main.cpp)
target_compile_definitions(wildos-taskmanager PRIVATE WILDOS_TASKMANAGER_VERSION="${PROJECT_VERSION}")

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/Format.qml qml/Colours.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-taskmanager
    URI WildOS.TaskManager
    QML_FILES ${QML_SOURCES}
    SOURCES
        src/processes.cpp src/processes.h
        src/systemstats.cpp src/systemstats.h
        src/winmode.cpp src/winmode.h
        src/startupapps.cpp src/startupapps.h
        src/windowsapps.cpp src/windowsapps.h
        src/services.cpp src/services.h
        src/prefs.cpp src/prefs.h
)

target_include_directories(wildos-taskmanager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wildos-taskmanager PRIVATE
    Qt6::Gui Qt6::DBus Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KSysGuard::ProcessCore KSysGuard::Sensors
    KF6::ConfigCore KF6::CoreAddons KF6::Service
)

install(TARGETS wildos-taskmanager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.taskmanager.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.taskmanager.svg data/wildos-process.svg data/wildos-windows-mode.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

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
