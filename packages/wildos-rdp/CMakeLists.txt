cmake_minimum_required(VERSION 3.25)
project(wildos-rdp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

# Qt6::GuiPrivate (below) only for QPlatformNativeInterface: the xdg_toplevel behind a
# window, which Qt 6.8 offers no public way to reach.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus)
find_package(KF6Config REQUIRED)
find_package(KF6WindowSystem REQUIRED)
include(engine.cmake)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
find_package(PlasmaWaylandProtocols REQUIRED)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Client code for the protocols wayland.cpp speaks to the compositor.
set(PROTOCOLS
    ${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml
    ${WAYLAND_PROTOCOLS}/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml
    ${PLASMA_WAYLAND_PROTOCOLS_DIR}/kde-output-device-v2.xml)
set(PROTOCOL_SOURCES)
foreach(xml ${PROTOCOLS})
    get_filename_component(name ${xml} NAME_WE)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${name}-client-protocol.h ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${CMAKE_CURRENT_BINARY_DIR}/${name}-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c
        DEPENDS ${xml})
    list(APPEND PROTOCOL_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${name}-client-protocol.h ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c)
endforeach()

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-rdp
    src/main.cpp
    src/railwindow.cpp src/railwindow.h
    src/desktopwindow.cpp src/desktopwindow.h
    src/rail.cpp src/rail.h
    src/loading.cpp src/loading.h
    src/style.cpp src/style.h
    src/service.cpp src/service.h
    src/selftest.cpp src/selftest.h
    src/wayland.cpp src/wayland.h
    src/kwin.cpp src/kwin.h
    src/screens.cpp src/screens.h
    ${PROTOCOL_SOURCES}
)
target_include_directories(wildos-rdp PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(wildos-rdp PRIVATE WILDOS_RDP_VERSION="${PROJECT_VERSION}")

target_link_libraries(wildos-rdp PRIVATE
    Qt6::Gui Qt6::GuiPrivate Qt6::DBus
    KF6::ConfigCore KF6::WindowSystem
    wildos-rdp-engine PkgConfig::WAYLAND
)

install(TARGETS wildos-rdp RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/wildos)
# Puts each Windows app where it was last time; enabled by default.
install(DIRECTORY kwin/org.wildos.rdp.place DESTINATION ${CMAKE_INSTALL_DATADIR}/kwin/scripts)

# How Windows' monitors are laid out for this desktop's screens; runs in the
# package build, needing no display.
option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(monitors-test tests/monitors-test.cpp)
    target_link_libraries(monitors-test PRIVATE Qt6::Test wildos-rdp-engine)
    add_test(NAME monitors COMMAND monitors-test)
endif()
