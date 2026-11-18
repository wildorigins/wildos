cmake_minimum_required(VERSION 3.25)
project(wildos-winmode-setup VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Network Qml Quick QuickControls2 Svg)

qt_standard_project_setup(REQUIRES 6.8)

# Both modules are laid out in the build tree as they are installed, so the
# setup window finds the shared one before install too.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

# WildOS.WinMode.Setup: the checks, the choices and the pages that the setup
# window and first run's step 7 (wildos-welcome) share, installed where any
# Qt Quick app can import it.
file(GLOB SHARED_QML RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} shared/*.qml)
foreach(file ${SHARED_QML})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildoswinmodesetup
    URI WildOS.WinMode.Setup
    VERSION 1.0
    PLUGIN_TARGET wildoswinmodesetup
    NO_PLUGIN_OPTIONAL
    QML_FILES ${SHARED_QML}
    SOURCES
        src/setupapp.cpp src/setupapp.h
        src/licence.cpp src/licence.h
        src/pccheck.cpp src/pccheck.h
        src/options.cpp src/options.h
)

target_include_directories(wildoswinmodesetup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildoswinmodesetup PRIVATE
    Qt6::Gui Qt6::DBus Qt6::Network Qt6::Qml Qt6::Quick
)

set(WINMODE_SETUP_QML_DIR ${QT6_INSTALL_QML}/WildOS/WinMode/Setup)
install(TARGETS wildoswinmodesetup LIBRARY DESTINATION ${WINMODE_SETUP_QML_DIR})
install(FILES
    ${CMAKE_BINARY_DIR}/qml/WildOS/WinMode/Setup/qmldir
    ${CMAKE_BINARY_DIR}/qml/WildOS/WinMode/Setup/wildoswinmodesetup.qmltypes
    DESTINATION ${WINMODE_SETUP_QML_DIR})

# The setup window itself.
qt_add_executable(wildos-winmode-setup src/main.cpp)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml qml/*.js)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-winmode-setup
    URI WildOS.WinMode
    QML_FILES ${QML_SOURCES}
    RESOURCES images/wild-os-icon.svg
    IMPORTS WildOS.WinMode.Setup
)

target_link_libraries(wildos-winmode-setup PRIVATE
    Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg
)
add_dependencies(wildos-winmode-setup wildoswinmodesetup)

install(TARGETS wildos-winmode-setup RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/wildos)

# Translations: ../i18n/wildos-winmode_<lang>.ts, compiled to .qm and loaded
# by WildOS.UI for the language chosen in Settings. They cover the setup
# window, the pages first run shares and the system service's own lines.
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
file(GLOB TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/../i18n/*.ts)
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
