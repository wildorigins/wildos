cmake_minimum_required(VERSION 3.25)
project(wildos-system-dialogs VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus Qml Quick QuickControls2)
find_package(KF6Config REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(LayerShellQt REQUIRED)
find_package(PolkitQt6-1 REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-dialogs src/main.cpp)
target_compile_definitions(wildos-dialogs PRIVATE WILDOS_DIALOGS_VERSION="${PROJECT_VERSION}")

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-dialogs
    URI WildOS.SystemDialogs
    QML_FILES ${QML_SOURCES}
    SOURCES
        src/surface.cpp src/surface.h
        src/agent.cpp src/agent.h
        src/verifier.cpp src/verifier.h
        src/security.cpp src/security.h
        src/prompts.cpp src/prompts.h
        src/session.cpp src/session.h
)

target_include_directories(wildos-dialogs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildos-dialogs PRIVATE
    Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::ConfigCore KF6::Service KF6::WindowSystem
    LayerShellQt::Interface
    PolkitQt6-1::Agent PolkitQt6-1::Core
)

# Runs inside whatever started a 32-bit program, sandboxes included, so it
# carries its own C library (see the file).
add_executable(32bit-blocked src/32bit-blocked.c)
target_link_options(32bit-blocked PRIVATE -static)

# sudo in a terminal: one prompt that takes a password or a finger.
add_library(pam_wildos_fingerprint MODULE src/pam_wildos_fingerprint.c)
set_target_properties(pam_wildos_fingerprint PROPERTIES PREFIX "")
target_compile_options(pam_wildos_fingerprint PRIVATE -Wall -Wextra)
target_link_libraries(pam_wildos_fingerprint PRIVATE pam)
install(TARGETS pam_wildos_fingerprint LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/security)

# Advanced protection's fingerprint rule: notes each password sign-in.
add_executable(fingerprint-stamp src/fingerprint-stamp.c)
target_compile_options(fingerprint-stamp PRIVATE -Wall -Wextra)

set(LIBEXEC ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
install(TARGETS wildos-dialogs 32bit-blocked fingerprint-stamp RUNTIME DESTINATION ${LIBEXEC})
install(FILES data/fingerprint-stamp.socket data/fingerprint-stamp@.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
install(PROGRAMS data/32bit-prompt-setup DESTINATION ${LIBEXEC})
install(FILES data/wildos-32bit-prompt.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
install(PROGRAMS data/record-package-sources DESTINATION ${LIBEXEC})
install(FILES data/80wildos-package-sources DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/apt/apt.conf.d)
install(FILES data/org.wildos.security.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/wildos-dialogs.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
install(FILES data/plasma-polkit-agent-wildos.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user/plasma-polkit-agent.service.d RENAME wildos.conf)
install(FILES data/powerdevil.notifyrc DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg)
install(FILES data/wildos-updates.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
