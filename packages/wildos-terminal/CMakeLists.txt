cmake_minimum_required(VERSION 3.25)
project(wildos-terminal VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickWidgets QuickControls2)
find_package(KF6Config REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(qtermwidget6 REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-terminal src/main.cpp)
target_compile_definitions(wildos-terminal PRIVATE
    WILDOS_TERMINAL_VERSION="${PROJECT_VERSION}"
    WILDOS_TERMINAL_DATADIR="${CMAKE_INSTALL_FULL_DATADIR}/wildos-terminal"
    WILDOS_TERMINAL_SOURCEDIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

file(GLOB_RECURSE QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/Metrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(wildos-terminal
    URI WildOS.Terminal
    QML_FILES ${QML_SOURCES}
    SOURCES
        src/config.cpp src/config.h
        src/schemes.cpp src/schemes.h
        src/terminalview.cpp src/terminalview.h
        src/termlinks.cpp src/termlinks.h
        src/tab.cpp src/tab.h
        src/terminalwindow.cpp src/terminalwindow.h
        src/windowframe.cpp src/windowframe.h
)

target_include_directories(wildos-terminal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wildos-terminal PRIVATE
    Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickWidgets Qt6::QuickControls2
    KF6::ConfigCore KF6::WindowSystem qtermwidget6
)

install(TARGETS wildos-terminal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.terminal.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(DIRECTORY data/schemes DESTINATION ${CMAKE_INSTALL_DATADIR}/wildos-terminal)

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
