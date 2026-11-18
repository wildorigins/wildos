cmake_minimum_required(VERSION 3.25)
project(wildos-cooler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Qml Quick QuickControls2)
find_package(KF6Config REQUIRED)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

# What the window and the program that draws the cooler's screen share: the
# cooler service's D-Bus interface with the names in wildoscoolerrc, and
# this PC's own temperature and load readings.
add_library(wildos-cooler-client STATIC client/coolerclient.cpp client/coolerclient.h client/settings.h)
target_include_directories(wildos-cooler-client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/client)
target_link_libraries(wildos-cooler-client PUBLIC Qt6::Core Qt6::DBus)
add_library(wildos-cooler-sensors STATIC sensors/sensors.cpp sensors/sensors.h)
target_include_directories(wildos-cooler-sensors PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sensors)
target_link_libraries(wildos-cooler-sensors PUBLIC Qt6::Core)

# The screen faces (WildOS.Cooler.Faces) are a part of this package with a
# build file of their own; the window carries them inside itself for its
# previews. Without them (-DWILDOS_COOLER_FACES=OFF) a preview is a plain
# disc with the face's name.
option(WILDOS_COOLER_FACES "Build the screen faces and put them in the window's previews" ON)
if(WILDOS_COOLER_FACES AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/faces/CMakeLists.txt)
    add_subdirectory(faces)
endif()
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/service/CMakeLists.txt)
    add_subdirectory(service)
endif()
# wildos-cooler-faces, which draws the chosen face on the cooler's screen.
if(TARGET wildos-cooler-facesplugin)
    add_subdirectory(streamer)
endif()

qt_add_executable(wildos-cooler app/src/main.cpp)
target_compile_definitions(wildos-cooler PRIVATE WILDOS_COOLER_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} app/qml/*.qml app/qml/lighting/*.qml app/qml/lighting/*.js)
set_source_files_properties(app/qml/CoolerIcons.qml app/qml/Colours.qml app/qml/Format.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(app/qml/lighting/LightWords.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-cooler
    URI WildOS.Cooler
    QML_FILES ${QML_SOURCES}
    SOURCES
        app/src/backend.h
        app/src/fakebackend.cpp app/src/fakebackend.h
        app/src/curves.h
        app/src/servicebackend.cpp app/src/servicebackend.h
        app/src/coolerapp.cpp app/src/coolerapp.h
        app/src/lighting/lighting.cpp app/src/lighting/lighting.h
)

target_include_directories(wildos-cooler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app/src ${CMAKE_CURRENT_SOURCE_DIR}/app/src/lighting)
target_link_libraries(wildos-cooler PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::DBus Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::ConfigCore KF6::DBusAddons KF6::WindowSystem
    wildos-cooler-client wildos-cooler-sensors
)
if(TARGET wildos-cooler-facesplugin)
    target_link_libraries(wildos-cooler PRIVATE wildos-cooler-facesplugin)
endif()

install(TARGETS wildos-cooler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.cooler.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.cooler.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# The window's title bar and the splash read the icon from here.
install(FILES data/wildos-cooler.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

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
