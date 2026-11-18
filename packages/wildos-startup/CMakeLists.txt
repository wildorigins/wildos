cmake_minimum_required(VERSION 3.25)
project(wildos-startup VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET cairo pangocairo blkid libcrypt)

add_executable(startup-manager
    src/main.c src/ui.c src/scan.c src/efi.c src/env.c src/text.c src/screen.c src/keys.c)
target_compile_definitions(startup-manager PRIVATE _GNU_SOURCE)
target_compile_options(startup-manager PRIVATE -Wall -Wextra -Wno-format-truncation)
target_link_libraries(startup-manager PRIVATE PkgConfig::DEPS m)
install(TARGETS startup-manager DESTINATION lib/wildos)

# The line icons, drawn once here as white stencils.
find_program(RSVG rsvg-convert REQUIRED)
set(ICONS usb restore disk gear power)
foreach(icon ${ICONS})
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${icon}.png
        COMMAND ${RSVG} -w 256 -h 256 ${CMAKE_CURRENT_SOURCE_DIR}/assets/${icon}.svg
                -o ${CMAKE_CURRENT_BINARY_DIR}/${icon}.png
        DEPENDS assets/${icon}.svg)
    list(APPEND ICON_FILES ${CMAKE_CURRENT_BINARY_DIR}/${icon}.png)
endforeach()
add_custom_target(icons ALL DEPENDS ${ICON_FILES})
install(FILES ${ICON_FILES} DESTINATION share/wildos-startup)
install(DIRECTORY i18n DESTINATION share/wildos-startup)
