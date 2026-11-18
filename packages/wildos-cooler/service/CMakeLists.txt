# wildos-coolerd, the system service that owns the cooler. It is Python:
# nothing to build, only files to put in place.
install(DIRECTORY wildos_cooler DESTINATION lib/wildos-cooler
    FILES_MATCHING PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE
)
install(PROGRAMS data/coolerd DESTINATION libexec/wildos)
install(FILES data/wildos-coolerd.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system)
install(FILES data/io.wildos.Cooler.service DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services)
install(FILES data/io.wildos.Cooler.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system.d)
install(FILES data/io.wildos.cooler.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/70-wildos-cooler.rules DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/udev/rules.d)
