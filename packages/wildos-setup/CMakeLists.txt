cmake_minimum_required(VERSION 3.25)
project(wildos-setup VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core DBus Network)
qt_standard_project_setup(REQUIRES 6.8)

find_library(PAM_LIBRARY pam REQUIRED)

qt_add_executable(wildos-setup-helper src/main.cpp src/service.cpp src/service.h src/walletkey.cpp src/walletkey.h)
target_link_libraries(wildos-setup-helper PRIVATE Qt6::Core Qt6::DBus Qt6::Network)

# Root, activated by the system bus: the key that opens the wallet of the
# account that signs in automatically.
qt_add_executable(wildos-wallet-keys src/keys.cpp src/keys.h src/walletkey.cpp src/walletkey.h)
target_link_libraries(wildos-wallet-keys PRIVATE Qt6::Core Qt6::DBus Qt6::Network ${PAM_LIBRARY})

# Runs as the account, at the start of a session it signed in to without
# a password.
qt_add_executable(wildos-setup-wallet src/wallet.cpp src/walletkey.cpp src/walletkey.h)
target_link_libraries(wildos-setup-wallet PRIVATE Qt6::Core Qt6::DBus Qt6::Network)

set(LIBEXEC ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
install(TARGETS wildos-setup-helper wildos-setup-wallet wildos-wallet-keys RUNTIME DESTINATION ${LIBEXEC})
install(PROGRAMS data/wildos-setup-boot data/wildos-setup-session DESTINATION ${LIBEXEC})
install(FILES data/wildos-setup-boot.service data/wildos-setup-helper.service data/wildos-wallet-keys.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
install(FILES data/wildos-setup-wallet.service data/wildos-wallet-notice.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
install(FILES data/org.wildos.Setup1.service data/org.wildos.WalletKeys1.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services)
install(FILES data/org.wildos.Setup1.conf data/org.wildos.WalletKeys1.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d)
install(FILES data/pam/wildos-wallet-keys DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d)
install(FILES data/org.wildos.setup.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/10-wildos-setup.rules DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/rules.d)
install(FILES data/wildos-setup.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions)
install(FILES data/kwinrc DESTINATION ${CMAKE_INSTALL_DATADIR}/wildos-setup)

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
