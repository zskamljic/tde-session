# What the shell's programs share: the installed applications and their icons, the windows
# open on the compositor, its displays, Bluetooth and Wi-Fi, the session's settings, and owning
# Wayland objects.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui DBus)
find_package(Tde 0.2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(WLR_PROTOCOLS wlr-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_PROTOCOLS OR NOT WLR_PROTOCOLS OR NOT WAYLAND_SCANNER)
    message(FATAL_ERROR "wayland-protocols, wlr-protocols and wayland-scanner are needed")
endif()

set(CMAKE_AUTOMOC ON)

# C bindings for the protocols of the window list and the displays, generated from their
# descriptions.
set(protocols
    ${WLR_PROTOCOLS}/unstable/wlr-foreign-toplevel-management-unstable-v1.xml
    ${WLR_PROTOCOLS}/unstable/wlr-output-management-unstable-v1.xml
    ${WAYLAND_PROTOCOLS}/staging/xdg-activation/xdg-activation-v1.xml
)
set(protocol_sources)
foreach(xml ${protocols})
    get_filename_component(name ${xml} NAME_WE)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${name}-client-protocol.h)
    set(code ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c)
    add_custom_command(OUTPUT ${header} COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header} DEPENDS ${xml})
    add_custom_command(OUTPUT ${code} COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code} DEPENDS ${xml})
    list(APPEND protocol_sources ${header} ${code})
endforeach()

add_library(tde-shell STATIC
    src/Applications.cpp
    src/Bluetooth.cpp
    src/BusProperties.cpp
    src/Catalog.cpp
    src/DesktopSettings.cpp
    src/Displays.cpp
    src/Icons.cpp
    src/LoginSession.cpp
    src/PowerProfiles.cpp
    src/SessionConfig.cpp
    src/Wifi.cpp
    src/Windows.cpp
    ${protocol_sources}
)
target_include_directories(tde-shell PUBLIC src ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(tde-shell PUBLIC Qt6::Core Qt6::Gui Qt6::DBus Tde::Tde PkgConfig::WAYLAND_CLIENT)

if(TDE_SESSION_BUILD_TESTS)
    add_subdirectory(tests)
endif()
