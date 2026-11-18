cmake_minimum_required(VERSION 3.25)
project(wildos-recovery VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

# Start-up recovery's screens (RecoveryStartup.dc, RecoveryStartupMore.dc),
# run alone in KWin by the recovery system.
qt_add_executable(wildos-recovery app/src/main.cpp)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} app/qml/*.qml)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-recovery
    URI WildOS.Recovery
    QML_FILES ${QML_SOURCES}
    SOURCES app/src/env.cpp app/src/env.h
)

target_include_directories(wildos-recovery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app/src)

target_link_libraries(wildos-recovery PRIVATE Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2)

install(TARGETS wildos-recovery RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/wildos)
install(PROGRAMS files/restore-points files/restore-points-menu files/reset-copy files/recovery-env
    files/recovery-boot files/settings-recovery files/recovery-session files/install-nocow
    files/restore-preview-notice
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/wildos)

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
