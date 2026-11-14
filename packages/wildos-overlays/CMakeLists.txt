cmake_minimum_required(VERSION 3.25)
project(wildos-overlays VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Qml Quick QuickControls2 WaylandClient)
find_package(KF6WindowSystem REQUIRED)
find_package(KF6GuiAddons REQUIRED)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6Config REQUIRED)
find_package(LayerShellQt REQUIRED)
find_package(PlasmaWaylandProtocols REQUIRED)
find_package(WaylandScanner REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

qt_standard_project_setup(REQUIRES 6.8)

# The pieces both the shell (toasts, on-screen displays) and the apps below
# need: placing a window on a screen edge above everything, and the state of
# Caps Lock and Num Lock.
qt_add_qml_module(wildosoverlays
    URI org.wildos.overlays
    VERSION 1.0
    PLUGIN_TARGET wildosoverlays
    NO_PLUGIN_OPTIONAL
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/org/wildos/overlays
    SOURCES
        plugin/overlayplacement.cpp plugin/overlayplacement.h
        plugin/keystate.cpp plugin/keystate.h
)
target_include_directories(wildosoverlays PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/plugin)
target_link_libraries(wildosoverlays PRIVATE
    Qt6::Gui Qt6::Qml Qt6::Quick
    KF6::WindowSystem KF6::GuiAddons LayerShellQt::Interface
)
set(OVERLAYS_QML_DIR ${QT6_INSTALL_QML}/org/wildos/overlays)
install(TARGETS wildosoverlays LIBRARY DESTINATION ${OVERLAYS_QML_DIR})
install(FILES
    ${CMAKE_BINARY_DIR}/qml/org/wildos/overlays/qmldir
    ${CMAKE_BINARY_DIR}/qml/org/wildos/overlays/wildosoverlays.qmltypes
    DESTINATION ${OVERLAYS_QML_DIR})

# Ctrl+V on behalf of the clipboard and emoji panel, through KWin's fake
# input.
add_library(wildosoverlays-wayland STATIC common/fakekeyboard.cpp common/fakekeyboard.h)
qt6_generate_wayland_protocol_client_sources(wildosoverlays-wayland FILES
    ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml
)
target_include_directories(wildosoverlays-wayland PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(wildosoverlays-wayland PUBLIC Qt6::Gui Qt6::WaylandClient Wayland::Client)
set_target_properties(wildosoverlays-wayland PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Win+V and Win+full stop.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/emoji.json
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/make-emoji.py
        /usr/share/unicode/emoji/emoji-test.txt
        /usr/share/unicode/cldr/common/annotations/en.xml
        /usr/share/unicode/cldr/common/annotationsDerived/en.xml
        ${CMAKE_CURRENT_BINARY_DIR}/emoji.json
    DEPENDS tools/make-emoji.py
    COMMENT "Building the emoji list from Unicode and CLDR data")

qt_add_executable(wildos-clipboard clipboard/main.cpp)
file(GLOB CLIPBOARD_QML RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} clipboard/qml/*.qml)
foreach(file ${CLIPBOARD_QML})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
qt_add_qml_module(wildos-clipboard
    URI WildOS.Clipboard
    QML_FILES ${CLIPBOARD_QML}
    SOURCES
        clipboard/panelapp.cpp clipboard/panelapp.h
        clipboard/secretwiper.cpp clipboard/secretwiper.h
        clipboard/pins.cpp clipboard/pins.h
        clipboard/emojimodel.cpp clipboard/emojimodel.h
)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/emoji.json PROPERTIES QT_RESOURCE_ALIAS emoji.json)
qt_add_resources(wildos-clipboard emojidata PREFIX /wildos FILES ${CMAKE_CURRENT_BINARY_DIR}/emoji.json)
target_include_directories(wildos-clipboard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/clipboard)
target_link_libraries(wildos-clipboard PRIVATE
    Qt6::Widgets Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::DBusAddons KF6::GuiAddons KF6::ConfigCore KF6::WindowSystem
    wildosoverlays-wayland
)

install(TARGETS wildos-clipboard RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES
    data/org.wildos.clipboard.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.clipboard-autostart.desktop
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart RENAME org.wildos.clipboard.desktop)
install(FILES data/klipperrc DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg)
# Plasma reads the on-screen display from its shell package only; this shim
# there loads the Wild OS one from the look-and-feel package.
install(FILES shell-osd/Osd.qml DESTINATION ${CMAKE_INSTALL_DATADIR}/plasma/shells/org.kde.plasma.desktop/contents/osd)
