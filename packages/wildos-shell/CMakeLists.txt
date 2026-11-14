cmake_minimum_required(VERSION 3.25)
project(wildos-shell VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml DBus)
find_package(KF6Config REQUIRED)
find_package(KF6Screen REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_qml_module(wildosshell
    URI org.wildos.shell
    VERSION 1.0
    PLUGIN_TARGET wildosshell
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/org/wildos/shell
    NO_GENERATE_PLUGIN_SOURCE
    CLASS_NAME WildOSShellPlugin
    SOURCES src/pillmask.cpp src/pillmask.h src/plugin.cpp src/launchwatcher.cpp src/launchwatcher.h src/taskbarstyle.cpp src/taskbarstyle.h
            src/underglow.cpp src/underglow.h src/underglowsettings.cpp src/underglowsettings.h
            src/session.cpp src/session.h src/shellrequests.cpp src/shellrequests.h src/keepawake.cpp src/keepawake.h
)
target_include_directories(wildosshell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildosshell PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::DBus KF6::ConfigCore KF6::ScreenDpms)

set(QML_DIR ${QT6_INSTALL_QML}/org/wildos/shell)
install(TARGETS wildosshell LIBRARY DESTINATION ${QML_DIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org/wildos/shell/qmldir DESTINATION ${QML_DIR})

# The underglow is a KWin script, not an effect: it uses only KWin's QML
# scripting API, which holds across KWin releases, where a C++ effect has to
# be rebuilt for each KWin version.
install(DIRECTORY kwin/org.wildos.underglow DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts)
install(DIRECTORY kwin/org.wildos.appwindows DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts)
install(DIRECTORY kwin/org.wildos.closewindow DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts)
install(DIRECTORY kwin/org.wildos.peekfade DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/effects)
# Overclock's Warp wallpaper with its live W. It lives here rather than in
# wildos-branding because it reads the underglow's clock and pauses from
# org.wildos.shell.
install(DIRECTORY wallpaper/org.wildos.warp DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/wallpapers)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_subdirectory(effects/roundcorners)
add_subdirectory(effects/outline)
add_subdirectory(effects/blurinactive)
add_subdirectory(effects/crossfade)
add_subdirectory(effects/gestures)
add_subdirectory(windowmenu)
add_subdirectory(slowkeys)
add_subdirectory(hotcorners)
