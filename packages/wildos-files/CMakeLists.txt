cmake_minimum_required(VERSION 3.25)
project(wildos-files VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Qml Quick QuickControls2 Svg Concurrent)
find_package(KF6KIO REQUIRED)
find_package(KF6Solid REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6Service REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(KF6Purpose REQUIRED)
find_package(KF6Archive REQUIRED)
find_package(KF6FileMetaData REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SECCOMP REQUIRED IMPORTED_TARGET libseccomp)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-files src/main.cpp)
target_compile_definitions(wildos-files PRIVATE WILDOS_FILES_VERSION="${PROJECT_VERSION}")

file(GLOB_RECURSE QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/Metrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(wildos-files
    URI WildOS.Files
    QML_FILES ${QML_SOURCES}
    SOURCES
        src/app.cpp src/app.h
        src/format.cpp src/format.h
        src/settings.cpp src/settings.h
        src/drives.cpp src/drives.h
        src/driveformat.cpp src/driveformat.h
        src/formatsafety.cpp src/formatsafety.h
        src/foldermodel.cpp src/foldermodel.h
        src/placesmodel.cpp src/placesmodel.h
        src/fileops.cpp src/fileops.h
        src/newitems.cpp
        src/iteminfo.cpp src/iteminfo.h
        src/sharemodel.cpp src/sharemodel.h
        src/fileextras.cpp
        src/zipjob.cpp src/zipjob.h
        src/filemeta.cpp src/filemeta.h
        src/imageproviders.cpp src/imageproviders.h
        src/previewsource.cpp src/previewsource.h
        src/programs.cpp src/programs.h
        src/protection.cpp src/protection.h
        src/protectionops.cpp
        src/safeboxplan.cpp src/safeboxplan.h
)

target_include_directories(wildos-files PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(wildos-files PRIVATE
    Qt6::Widgets Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::Concurrent
    KF6::KIOCore KF6::KIOGui KF6::KIOWidgets KF6::Solid KF6::ConfigCore
    KF6::CoreAddons KF6::Service KF6::WindowSystem KF6::Purpose
    KF6::Archive KF6::FileMetaData
)

# The safe box: a small program of its own, so the box outlives the Files
# window it was opened from.
add_executable(wildos-safebox src/safebox/main.cpp src/safeboxplan.cpp src/programs.cpp)
target_include_directories(wildos-safebox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildos-safebox PRIVATE Qt6::Core Qt6::DBus KF6::KIOCore KF6::Service PkgConfig::SECCOMP)

if(WILDOS_FILES_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    qt_add_executable(formatsafety-test tests/formatsafety_test.cpp src/formatsafety.cpp)
    target_include_directories(formatsafety-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(formatsafety-test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME formatsafety COMMAND formatsafety-test)
endif()

install(TARGETS wildos-files wildos-safebox RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS data/remove-details DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
# Advanced protection's rows for these two appear when their hooks exist;
# Files itself reads the switches (src/protection.cpp).
install(PROGRAMS data/advanced-protection/scrub_files data/advanced-protection/safebox DESTINATION lib/wildos/advanced-protection.d)
install(FILES data/org.wildos.files.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.Files.service data/org.wildos.files.FileManager1.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES data/fm-shim-wildos-files.conf DESTINATION lib/systemd/user/fm-shim.service.d RENAME 50-wildos-files.conf)
