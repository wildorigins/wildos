cmake_minimum_required(VERSION 3.25)
project(wildos-voice VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent Network Multimedia Qml Quick QuickControls2)
find_package(KF6DBusAddons REQUIRED)
find_package(KF6WindowSystem REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUSENC REQUIRED IMPORTED_TARGET libopusenc>=0.2)
pkg_check_modules(OPUSFILE REQUIRED IMPORTED_TARGET opusfile>=0.12)

# whisper.cpp v1.9.5 (commit d1be6fde, 6 October 2026), trimmed to the
# library and ggml's CPU backend; Debian trixie does not package it. It is
# linked in statically so nothing else on the system loads it. The CPU
# features are fixed rather than taken from the build machine: AVX2, FMA
# and F16C, which every x86 PC since about 2014 has. Voice checks for them
# before it turns transcripts on, so an older processor gets a message
# instead of a crash.
# Static only for whisper.cpp: a plain variable, dropped again after, since
# Qt reads BUILD_SHARED_LIBS too and would treat Voice as a static build.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_SHARED_LIBS OFF)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GGML_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    foreach(feature SSE42 AVX AVX2 FMA F16C BMI2)
        set(GGML_${feature} ON CACHE BOOL "" FORCE)
    endforeach()
endif()
add_subdirectory(3rdparty/whisper.cpp EXCLUDE_FROM_ALL SYSTEM)
unset(BUILD_SHARED_LIBS)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(wildos-voice src/main.cpp)
target_compile_definitions(wildos-voice PRIVATE WILDOS_VOICE_VERSION="${PROJECT_VERSION}" QT_NO_KEYWORDS)

file(GLOB QML_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/*.qml)
set_source_files_properties(qml/VoiceIcons.qml qml/VoiceStyle.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(file ${QML_SOURCES})
    get_filename_component(name ${file} NAME)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_qml_module(wildos-voice
    URI WildOS.Voice
    QML_FILES ${QML_SOURCES}
    RESOURCES data/wildos-voice.svg
    SOURCES
        src/paths.h
        src/audio.cpp src/audio.h
        src/opuswriter.cpp src/opuswriter.h
        src/sidecar.cpp src/sidecar.h
        src/recordings.cpp src/recordings.h
        src/recorder.cpp src/recorder.h
        src/player.cpp src/player.h
        src/mpris.cpp src/mpris.h
        src/modeldownload.cpp src/modeldownload.h
        src/transcriber.cpp src/transcriber.h
        src/transcripts.cpp src/transcripts.h
        src/waveformview.cpp src/waveformview.h
        src/voiceapp.cpp src/voiceapp.h
)

target_include_directories(wildos-voice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(wildos-voice PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::DBus Qt6::Concurrent Qt6::Network Qt6::Multimedia Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::DBusAddons KF6::WindowSystem
    PkgConfig::OPUSENC PkgConfig::OPUSFILE
    whisper
)

install(TARGETS wildos-voice RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/org.wildos.voice.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/org.wildos.voice.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# The splash reads the icon from here, and it shows under any icon theme.
install(FILES data/wildos-voice.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

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
