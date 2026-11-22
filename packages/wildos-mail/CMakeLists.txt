cmake_minimum_required(VERSION 3.25)
project(wildos-mail VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Network PrintSupport Qml Quick QuickControls2 Svg)
find_package(Qt6Keychain REQUIRED)
find_package(KF6Sonnet REQUIRED)
find_package(litehtml REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CAMEL REQUIRED IMPORTED_TARGET camel-1.2)
pkg_check_modules(EDS REQUIRED IMPORTED_TARGET libedataserver-1.2)
pkg_check_modules(ECAL REQUIRED IMPORTED_TARGET libecal-2.0)
pkg_check_modules(EBOOK REQUIRED IMPORTED_TARGET libebook-1.2)
pkg_check_modules(EBACKEND REQUIRED IMPORTED_TARGET libebackend-1.2)
pkg_check_modules(SQLITE REQUIRED IMPORTED_TARGET sqlite3)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-mail src/main.cpp)
target_compile_definitions(wildos-mail PRIVATE WILDOS_MAIL_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml qml/*.js)
set_source_files_properties(qml/MailIcons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-mail
    URI WildOS.Mail
    QML_FILES ${QML_SOURCES}
    RESOURCES data/litehtml-master.css data/caught-up.svg
    SOURCES
        src/camelsession.cpp src/camelsession.h
        src/accounts.cpp src/accounts.h
        src/models.cpp src/models.h
        src/mailengine.cpp src/mailengine.h
        src/messageview.cpp src/messageview.h
        src/htmlview.cpp src/htmlview.h
        src/composer.cpp src/composer.h
        src/autoconfig.cpp src/autoconfig.h
        src/notifier.cpp src/notifier.h
        src/formatter.cpp src/formatter.h
        src/spellcheck.cpp src/spellcheck.h
        src/googlesignin.cpp src/googlesignin.h
        src/microsoftsignin.cpp src/microsoftsignin.h
        src/calendarengine.cpp src/calendarengine.h
        src/reminders.cpp src/reminders.h
        src/peopleengine.cpp src/peopleengine.h
        src/tasksengine.cpp src/tasksengine.h
        src/senderimages.cpp src/senderimages.h
        src/apps.h
        src/formats.h
)

target_include_directories(wildos-mail PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wildos-mail PRIVATE Qt6::Widgets
    Qt6::Gui Qt6::DBus Qt6::Network Qt6::PrintSupport Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg
    qt6keychain
    KF6::SonnetCore
    litehtml
    PkgConfig::CAMEL
    PkgConfig::EDS
    PkgConfig::ECAL
    PkgConfig::EBOOK
    PkgConfig::EBACKEND
    PkgConfig::SQLITE
)

install(TARGETS wildos-mail RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Calendar is the same program started by another name (main.cpp).
install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink wildos-mail \$ENV{DESTDIR}${CMAKE_INSTALL_FULL_BINDIR}/wildos-calendar)")

install(FILES data/org.wildos.mail.desktop data/org.wildos.calendar.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.mail.svg data/org.wildos.calendar.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/org.wildos.calendar-reminders.desktop data/org.wildos.mail-background.desktop DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)

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
