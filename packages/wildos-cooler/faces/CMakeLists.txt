cmake_minimum_required(VERSION 3.25)

# The faces of the cooler's screen: the QML module WildOS.Cooler.Faces and the
# tool that renders a face to pictures. wildos-cooler takes this directory in
# with add_subdirectory(faces) and links wildos-cooler-facesplugin; it also
# builds alone, for work on the faces:
#
#   cmake -S packages/wildos-cooler/faces -B build && cmake --build build
if(NOT PROJECT_NAME)
    project(wildos-cooler-faces VERSION 0.1.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    include(GNUInstallDirs)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
    qt_standard_project_setup(REQUIRES 6.8)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui OpenGL Qml Quick ShaderTools)

set_source_files_properties(Faces.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Static, so the app and the renderer carry the faces inside themselves and
# nothing has to be installed for QML to find.
qt_add_qml_module(wildos-cooler-faces
    STATIC
    URI WildOS.Cooler.Faces
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        Faces.qml
        FaceView.qml
        FaceBase.qml
        FaceText.qml
        Reading.qml
        Degrees.qml
        Mark.qml
        Streak.qml
        TickRing.qml
        HistoryGraph.qml
        LiquidFace.qml
        ClockFace.qml
        LastMinuteFace.qml
        BothFace.qml
        CoresFace.qml
        WarpFace.qml
        TunnelFace.qml
        ReactorFace.qml
        HiveFace.qml
        MatrixFace.qml
        NeonFace.qml
        RgbRingFace.qml
        LimitFace.qml
        FaceMath.js
        LiquidData.js
        WarpData.js
        MatrixData.js
)

qt_add_shaders(wildos-cooler-faces "wildos-cooler-faces-shaders"
    PREFIX /qt/qml/WildOS/Cooler/Faces
    FILES
        shaders/round.frag
        shaders/hive.frag
        shaders/sweep.frag
        shaders/rainbow.frag
)

qt_add_executable(wildos-cooler-face-render
    render/main.cpp
    render/facerenderer.cpp render/facerenderer.h
)
target_link_libraries(wildos-cooler-face-render PRIVATE
    Qt6::Gui Qt6::OpenGL Qt6::Qml Qt6::Quick
    wildos-cooler-facesplugin
)
# The faces' plugin is linked by name above and imported in main.cpp. Qt's
# import scan would add nothing but a warning for each of Qt's own shared
# QML plugins.
set_target_properties(wildos-cooler-face-render PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
install(TARGETS wildos-cooler-face-render RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
