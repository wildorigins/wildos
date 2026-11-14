# Built the same way as roundcorners, which explains the finds.
find_package(ECM 6.0 REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${ECM_FIND_MODULE_DIR})
find_package(Qt6 6.8 REQUIRED COMPONENTS DBus Quick Widgets)
find_package(KWin 6.3 REQUIRED)

add_library(wildos_crossfade MODULE crossfade.cpp crossfade.h crossfade.qrc)
set_target_properties(wildos_crossfade PROPERTIES AUTOMOC ON AUTORCC ON PREFIX "")
set_property(TARGET wildos_crossfade APPEND PROPERTY AUTOMOC_MACRO_NAMES KWIN_EFFECT_FACTORY_SUPPORTED)
target_link_libraries(wildos_crossfade PRIVATE KWin::kwin Qt6::Core Qt6::Gui Qt6::DBus)

install(TARGETS wildos_crossfade LIBRARY DESTINATION ${QT6_INSTALL_PLUGINS}/kwin/effects/plugins)
