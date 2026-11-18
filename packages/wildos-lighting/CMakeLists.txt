cmake_minimum_required(VERSION 3.25)
project(wildos-lighting VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Network)
find_package(KF6Config REQUIRED)
qt_standard_project_setup(REQUIRES 6.8)

# QtGui for QColor only; the program opens no windows.
qt_add_executable(wildos-lighting
    src/main.cpp
    src/lighting.cpp src/lighting.h
    src/plan.cpp src/plan.h
    src/lights.cpp src/lights.h
    src/effects.cpp src/effects.h
    src/sdk.cpp src/sdk.h
    src/seen.cpp src/seen.h
)
target_link_libraries(wildos-lighting PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Network KF6::ConfigCore)

install(TARGETS wildos-lighting RUNTIME DESTINATION libexec/wildos)
install(PROGRAMS libexec/rgb-devices DESTINATION libexec/wildos)
install(FILES data/wildos-lighting.service data/wildos-openrgb.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
install(FILES data/60-openrgb.rules DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(plantest tests/plantest.cpp src/plan.cpp)
    target_include_directories(plantest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(plantest PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME plan COMMAND plantest)
    add_executable(effectstest tests/effectstest.cpp src/effects.cpp src/plan.cpp src/seen.cpp)
    target_include_directories(effectstest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(effectstest PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME effects COMMAND effectstest)
    add_executable(sdktest tests/sdktest.cpp src/sdk.cpp)
    target_include_directories(sdktest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(sdktest PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
    add_test(NAME sdk COMMAND sdktest)
    # The whole program against a stand-in OpenRGB server. It takes half a
    # minute and counts frames against the clock, so a package build on a
    # busy machine does not run it: ask with -DWILDOS_LIGHTING_LIVE_TEST=ON.
    option(WILDOS_LIGHTING_LIVE_TEST "Test wildos-lighting against the stand-in OpenRGB server" OFF)
    find_program(DBUS_RUN_SESSION dbus-run-session)
    find_program(DBUS_SEND dbus-send)
    find_program(PYTHON3 python3)
    if(WILDOS_LIGHTING_LIVE_TEST AND DBUS_RUN_SESSION AND DBUS_SEND AND PYTHON3)
        add_test(NAME lights COMMAND ${DBUS_RUN_SESSION} -- ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tests/lights.py
            $<TARGET_FILE:wildos-lighting> ${CMAKE_CURRENT_SOURCE_DIR}/tests/fake-openrgb.py)
    endif()
    # rgb-devices against a fake sysfs and rules file.
    add_test(NAME scripts COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/scripts.sh ${CMAKE_CURRENT_SOURCE_DIR}/libexec)
endif()
