# wildos-cooler-faces: draws the chosen face on the cooler's screen from the
# person's session. Built as part of wildos-cooler, after faces/.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus OpenGL Qml Quick)
find_package(KF6Config REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6IdleTime REQUIRED)

qt_add_executable(wildos-cooler-faces-streamer
    main.cpp
    streamer.cpp streamer.h
    session.cpp session.h
    games.cpp games.h
    ../faces/render/facerenderer.cpp ../faces/render/facerenderer.h
)
# The target is named apart from the faces' QML module; the program is not.
set_target_properties(wildos-cooler-faces-streamer PROPERTIES
    OUTPUT_NAME wildos-cooler-faces
    QT_QML_MODULE_NO_IMPORT_SCAN TRUE
)
qt_add_resources(wildos-cooler-faces-streamer "streamer"
    PREFIX /wildos-cooler-faces
    FILES Theme.qml
)
target_include_directories(wildos-cooler-faces-streamer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../faces/render)
target_compile_definitions(wildos-cooler-faces-streamer PRIVATE QT_NO_KEYWORDS)
target_link_libraries(wildos-cooler-faces-streamer PRIVATE
    Qt6::Gui Qt6::DBus Qt6::OpenGL Qt6::Qml Qt6::Quick
    KF6::ConfigCore KF6::CoreAddons KF6::IdleTime
    wildos-cooler-facesplugin wildos-cooler-client wildos-cooler-sensors
)

install(TARGETS wildos-cooler-faces-streamer RUNTIME DESTINATION libexec/wildos)
install(FILES wildos-cooler-faces.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
