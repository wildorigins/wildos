cmake_minimum_required(VERSION 3.25)
project(wildos-ui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

option(WILDOS_UI_GALLERY "Build the gallery that shows every control in every state" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 DBus)
find_package(KF6Config REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

# The module is laid out in the build tree exactly as it is installed, so
# the gallery (and any app built next to it) can import it before install.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/Style.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# Files in qml/ are listed by their bare names in the module.
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildosui
    URI WildOS.UI
    VERSION 1.0
    PLUGIN_TARGET wildosui
    NO_PLUGIN_OPTIONAL
    QML_FILES ${QML_SOURCES}
    RESOURCES data/wildos-mark.svg
    SOURCES
        src/theme.cpp src/theme.h
        src/windowframe.cpp src/windowframe.h
        src/themeicon.cpp src/themeicon.h
        src/system.cpp src/system.h
        src/accessshortcuts.cpp src/accessshortcuts.h
        src/historyswipe.cpp src/historyswipe.h
        src/nopaste.cpp src/nopaste.h
        src/secretcopy.cpp src/secretcopy.h
        src/timeformat.cpp src/timeformat.h src/clockformat.h
)

target_include_directories(wildosui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildosui PRIVATE
    Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::DBus
    KF6::ConfigCore KF6::WindowSystem
)

set(WILDOS_UI_QML_DIR ${QT6_INSTALL_QML}/WildOS/UI)
install(TARGETS wildosui LIBRARY DESTINATION ${WILDOS_UI_QML_DIR})
install(FILES
    ${CMAKE_BINARY_DIR}/qml/WildOS/UI/qmldir
    ${CMAKE_BINARY_DIR}/qml/WildOS/UI/wildosui.qmltypes
    DESTINATION ${WILDOS_UI_QML_DIR})
install(FILES gallery/Gallery.qml DESTINATION ${CMAKE_INSTALL_DATADIR}/wildos-ui)

if(WILDOS_UI_GALLERY)
    add_executable(wildos-ui-gallery gallery/main.cpp)
    target_link_libraries(wildos-ui-gallery PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
    target_compile_definitions(wildos-ui-gallery PRIVATE
        WILDOS_UI_BUILD_QML="${CMAKE_BINARY_DIR}/qml"
        WILDOS_UI_GALLERY_QML="${CMAKE_CURRENT_SOURCE_DIR}/gallery/Gallery.qml")
    add_dependencies(wildos-ui-gallery wildosui)
    install(TARGETS wildos-ui-gallery RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/libexec/wildos)
endif()
