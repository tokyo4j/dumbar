cmake_minimum_required(VERSION 3.21)

project(dumbar VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets DBus WaylandClient)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
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

set_source_files_properties(
    "${FOREIGN_TOPLEVEL_HEADER}"
    "${FOREIGN_TOPLEVEL_CODE}"
    PROPERTIES GENERATED TRUE
)
set_source_files_properties("${FOREIGN_TOPLEVEL_CODE}" PROPERTIES LANGUAGE C)

add_executable(dumbar
    src/main.cpp
    src/panel.cpp
    src/panel.h
    src/taskbar.cpp
    src/taskbar.h
    src/taskbutton.cpp
    src/taskbutton.h
    src/toplevelmanager.cpp
    src/toplevelmanager.h
    src/battery.cpp
    src/battery.h
    src/audio.cpp
    src/audio.h
    src/tray.cpp
    src/tray.h
    src/trayitem.cpp
    src/trayitem.h
    src/layershell/popup.cpp
    src/layershell/popup.h
    src/layershell/layershellmenu.cpp
    src/layershell/layershellmenu.h
    src/layershell/layershelltooltip.cpp
    src/layershell/layershelltooltip.h
    src/clock.cpp
    src/clock.h
    src/iconresolver.cpp
    src/iconresolver.h
    src/dumbar.h
    "${FOREIGN_TOPLEVEL_HEADER}"
    "${FOREIGN_TOPLEVEL_CODE}"
)

target_include_directories(dumbar PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(dumbar PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::DBus
    Qt6::WaylandClient
    LayerShellQt::Interface
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::PULSE
)

install(TARGETS dumbar RUNTIME DESTINATION bin)
