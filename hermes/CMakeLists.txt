find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Widgets DBus Network)
find_package(LayerShellQt 6 REQUIRED)
find_package(PolkitQt6-1 REQUIRED)
find_package(Tde 0.2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CANBERRA REQUIRED IMPORTED_TARGET libcanberra)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(WLR_PROTOCOLS wlr-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

set(CMAKE_AUTOMOC ON)

# Knowing when the user is away, to lock the screen and save power, and turning screens off.
set(protocol_sources)
foreach(xml ${WAYLAND_PROTOCOLS}/staging/ext-idle-notify/ext-idle-notify-v1.xml
            ${WLR_PROTOCOLS}/unstable/wlr-output-power-management-unstable-v1.xml)
    get_filename_component(name ${xml} NAME_WE)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${name}-client-protocol.h)
    set(code ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c)
    add_custom_command(OUTPUT ${header} COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header} DEPENDS ${xml})
    add_custom_command(OUTPUT ${code} COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code} DEPENDS ${xml})
    list(APPEND protocol_sources ${header} ${code})
endforeach()

# What can be tested without a compositor or a session bus.
add_library(hermes-core STATIC src/Groups.cpp src/DBusMenu.cpp src/Notifications.cpp)
target_include_directories(hermes-core PUBLIC src)
target_compile_definitions(hermes-core PRIVATE TDE_SESSION_VERSION="${PROJECT_VERSION}")
target_link_libraries(hermes-core PUBLIC tde-shell Qt6::Widgets Qt6::DBus)

add_executable(tde-hermes
    src/main.cpp
    src/Audio.cpp
    src/AuthDialog.cpp
    src/Bar.cpp
    src/DisplayLayouts.cpp
    src/Locking.cpp
    src/Media.cpp
    src/NotificationViews.cpp
    src/Osd.cpp
    src/PlayerCard.cpp
    src/PolkitAgent.cpp
    src/PowerSaving.cpp
    src/QuickSettings.cpp
    src/Sounds.cpp
    src/StatusNotifier.cpp
    src/SystemStatus.cpp
    src/Taskbar.cpp
    src/Tray.cpp
    ${protocol_sources}
)
target_include_directories(tde-hermes PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tde-hermes PRIVATE TDE_SESSION_VERSION="${PROJECT_VERSION}")
target_link_libraries(tde-hermes PRIVATE
    hermes-core Qt6::Gui Qt6::Widgets Qt6::DBus Qt6::Network LayerShellQt::Interface Tde::Tde PolkitQt6-1::Agent
    PkgConfig::CANBERRA PkgConfig::PULSE PkgConfig::WAYLAND_CLIENT)
install(TARGETS tde-hermes)

if(TDE_SESSION_BUILD_TESTS)
    add_subdirectory(tests)
endif()
