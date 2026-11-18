cmake_minimum_required(VERSION 3.25)
project(wildos-notes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

option(WILDOS_NOTES_TESTS "Build the Markdown round-trip test" OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus PrintSupport Qml Quick QuickControls2)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6WindowSystem REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-notes src/main.cpp)
target_compile_definitions(wildos-notes PRIVATE WILDOS_NOTES_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/NotesIcons.qml qml/NoteColours.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-notes
    URI WildOS.Notes
    QML_FILES ${QML_SOURCES}
    RESOURCES data/wildos-notes.svg
    SOURCES
        src/notestore.cpp src/notestore.h
        src/notedocument.cpp src/notedocument.h
        src/markdowneditor.cpp src/markdowneditor.h
        src/notesapp.cpp src/notesapp.h
        src/notefile.cpp src/notefile.h
        src/clockformat.h
)

target_include_directories(wildos-notes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildos-notes PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::DBus Qt6::PrintSupport Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::DBusAddons KF6::WindowSystem
)

if(WILDOS_NOTES_TESTS)
    qt_add_executable(notes-roundtrip tests/roundtrip.cpp src/notefile.cpp src/markdowneditor.cpp src/markdowneditor.h)
    target_include_directories(notes-roundtrip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(notes-roundtrip PRIVATE Qt6::Gui Qt6::Quick)
endif()

install(TARGETS wildos-notes RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.notes.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.notes.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# Also in the WildOS icon theme (wildos-branding); here too so Notes has
# its icon under any theme, and the splash reads it from here.
install(FILES data/wildos-notes.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

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
