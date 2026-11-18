cmake_minimum_required(VERSION 3.25)
project(wildos-desktop-view VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(KF6KIO REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

# Laid out in the build tree as installed, so the rig can import it from
# there before installing.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildosdesktop
    URI WildOS.Desktop
    VERSION 1.0
    PLUGIN_TARGET wildosdesktop
    NO_PLUGIN_OPTIONAL
    QML_FILES ${QML_SOURCES}
    SOURCES
        src/deskops.cpp src/deskops.h
        src/desktopmodel.cpp src/desktopmodel.h
        src/newitems.cpp
        src/format.cpp src/format.h
        src/iteminfo.cpp src/iteminfo.h
        src/thumbnail.cpp src/thumbnail.h
)

target_include_directories(wildosdesktop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildosdesktop PRIVATE
    Qt6::Gui Qt6::Qml Qt6::Quick
    KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::ConfigCore KF6::CoreAddons KF6::Service KF6::WindowSystem
)

set(WILDOS_DESKTOP_QML_DIR ${QT6_INSTALL_QML}/WildOS/Desktop)
install(TARGETS wildosdesktop LIBRARY DESTINATION ${WILDOS_DESKTOP_QML_DIR})
install(FILES
    ${CMAKE_BINARY_DIR}/qml/WildOS/Desktop/qmldir
    ${CMAKE_BINARY_DIR}/qml/WildOS/Desktop/wildosdesktop.qmltypes
    DESTINATION ${WILDOS_DESKTOP_QML_DIR})
install(DIRECTORY plasmoid/ DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/plasmoids/org.wildos.desktop)

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
