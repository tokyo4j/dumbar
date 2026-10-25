cmake_minimum_required(VERSION 3.21)

project(dumbar VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
# gtk4-layer-shell must be linked before libwayland-client.
pkg_check_modules(GTK4_LAYER_SHELL REQUIRED IMPORTED_TARGET gtk4-layer-shell-0)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0 gtk4-wayland)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse libpulse-mainloop-glib)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(FOREIGN_TOPLEVEL_XML
    "${CMAKE_CURRENT_SOURCE_DIR}/protocol/wlr-foreign-toplevel-management-unstable-v1.xml")
set(FOREIGN_TOPLEVEL_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/wayland-wlr-foreign-toplevel-management-unstable-v1-client-protocol.h")
set(FOREIGN_TOPLEVEL_CODE
    "${CMAKE_CURRENT_BINARY_DIR}/wayland-wlr-foreign-toplevel-management-unstable-v1-protocol.c")

add_custom_command(
    OUTPUT "${FOREIGN_TOPLEVEL_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${FOREIGN_TOPLEVEL_XML}" "${FOREIGN_TOPLEVEL_HEADER}"
    DEPENDS "${WAYLAND_SCANNER}" "${FOREIGN_TOPLEVEL_XML}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${FOREIGN_TOPLEVEL_CODE}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${FOREIGN_TOPLEVEL_XML}" "${FOREIGN_TOPLEVEL_CODE}"
    DEPENDS "${WAYLAND_SCANNER}" "${FOREIGN_TOPLEVEL_XML}" "${FOREIGN_TOPLEVEL_HEADER}"
    VERBATIM
)

add_executable(dumbar
    src/main.cpp
    src/config.h
    src/panel.cpp
    src/panel.h
    src/taskbar.cpp
    src/taskbar.h
    src/taskbutton.cpp
    src/taskbutton.h
    src/toplevelmanager.cpp
    src/toplevelmanager.h
    src/iconresolver.cpp
    src/iconresolver.h
    src/tray.cpp
    src/tray.h
    src/trayitem.cpp
    src/trayitem.h
    src/statusnotifierhost.cpp
    src/statusnotifierhost.h
    src/audio.cpp
    src/audio.h
    src/battery.cpp
    src/battery.h
    src/clock.cpp
    src/clock.h
    "${FOREIGN_TOPLEVEL_HEADER}"
    "${FOREIGN_TOPLEVEL_CODE}"
)

target_include_directories(dumbar PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(dumbar PRIVATE G_LOG_DOMAIN="dumbar")
target_compile_options(dumbar PRIVATE -Wall -Wextra)

target_link_libraries(dumbar PRIVATE
    PkgConfig::GTK4_LAYER_SHELL
    PkgConfig::GTKMM
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::PULSE
)

install(TARGETS dumbar RUNTIME DESTINATION bin)
