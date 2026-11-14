cmake_minimum_required(VERSION 3.25)
project(wildos-report VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

option(WILDOS_REPORT_TESTS "Build the redactor and issue watch tests" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent Network Qml Quick QuickControls2)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6Notifications REQUIRED)
find_package(KF6Archive REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-report src/main.cpp)
target_compile_definitions(wildos-report PRIVATE WILDOS_REPORT_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/ReportIcons.qml qml/Look.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-report
    URI WildOS.Report
    QML_FILES ${QML_SOURCES}
    RESOURCES data/wildos-report.svg
    SOURCES
        src/report.cpp src/report.h
        src/collector.cpp src/collector.h
        src/crashnotify.cpp src/crashnotify.h
        src/issuewatch.cpp src/issuewatch.h
        src/parts.cpp src/parts.h
        src/redactor.cpp src/redactor.h
        src/sysinfo.cpp src/sysinfo.h
)

target_include_directories(wildos-report PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildos-report PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::DBus Qt6::Concurrent Qt6::Network Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::DBusAddons KF6::Notifications KF6::Archive KF6::WindowSystem
)

if(WILDOS_REPORT_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(redactor-test tests/redactor_test.cpp src/redactor.cpp)
    target_include_directories(redactor-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(redactor-test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME redactor COMMAND redactor-test)
    qt_add_executable(issuewatch-test tests/issuewatch_test.cpp src/issuewatch.cpp)
    target_include_directories(issuewatch-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(issuewatch-test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME issuewatch COMMAND issuewatch-test)
endif()

install(TARGETS wildos-report RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.report.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.report.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES data/wildos-report.notifyrc DESTINATION ${CMAKE_INSTALL_DATADIR}/knotifications6)
# Also in the WildOS icon theme (wildos-branding); here too so the app has
# its icon under any theme, and the splash reads it from here.
install(FILES data/wildos-report.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

install(PROGRAMS data/report-logs data/report-crash-note DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
install(FILES data/org.wildos.report.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/50-wildos-report.rules DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/rules.d)
install(FILES data/995-wildos-crash-reports.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/sysctl.d)
install(FILES data/coredump-wildos-report.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/systemd-coredump@.service.d
    RENAME wildos-report.conf)
install(FILES data/wildos-report-crash.path data/wildos-report-crash.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
# KDE's crash reporter steps aside; see drkonqi-wildos-report.conf. Its
# socket is masked instead (debian/links): sockets.target upholds it, so a
# condition that stops it made systemd try it again, and log a dozen lines,
# whenever an app started.
install(FILES data/drkonqi-wildos-report.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system/drkonqi-coredump-processor@.service.d
    RENAME wildos-report.conf)
foreach(unit drkonqi-coredump-pickup.service drkonqi-sentry-postman.path drkonqi-sentry-postman.timer
        drkonqi-sentry-postman.service)
    install(FILES data/drkonqi-wildos-report.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user/${unit}.d RENAME wildos-report.conf)
endforeach()
