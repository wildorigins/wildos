cmake_minimum_required(VERSION 3.25)
project(wildos-appbridge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Qml Quick QuickControls2 Svg)
find_package(KF6Config REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-appbridge src/main.cpp)
target_compile_definitions(wildos-appbridge PRIVATE WILDOS_APPBRIDGE_VERSION="${PROJECT_VERSION}")

file(GLOB_RECURSE QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)

qt_add_qml_module(wildos-appbridge
    URI WildOS.AppBridge
    QML_FILES ${QML_SOURCES}
    RESOURCES images/windows-app.svg data/alternatives.json
    SOURCES
        src/bridge.cpp src/bridge.h
        src/peinfo.cpp src/peinfo.h
        src/winapps.cpp src/winapps.h
)

target_include_directories(wildos-appbridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wildos-appbridge PRIVATE
    Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg
    KF6::ConfigCore KF6::WindowSystem
)

install(TARGETS wildos-appbridge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.appbridge.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/alternatives.json DESTINATION ${CMAKE_INSTALL_DATADIR}/wildos-appbridge)
install(FILES images/windows-app.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME wildos-windows-app.svg)
install(FILES data/autostart/org.wildos.appbridge-tidy.desktop DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/autostart)

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
