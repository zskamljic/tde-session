find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Widgets DBus)
find_package(LayerShellQt 6 REQUIRED)
find_package(PolkitQt6-1 REQUIRED)
find_package(Tde 0.2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CANBERRA REQUIRED IMPORTED_TARGET libcanberra)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)

pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

set(CMAKE_AUTOMOC ON)

# Knowing when the user is away, to lock the screen.
set(xml ${WAYLAND_PROTOCOLS}/staging/ext-idle-notify/ext-idle-notify-v1.xml)
set(idle_header ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h)
set(idle_code ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-protocol.c)
add_custom_command(OUTPUT ${idle_header} COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${idle_header} DEPENDS ${xml})
add_custom_command(OUTPUT ${idle_code} COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${idle_code} DEPENDS ${xml})

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
    src/NotificationViews.cpp
    src/Osd.cpp
    src/PolkitAgent.cpp
    src/QuickSettings.cpp
    src/Sounds.cpp
    src/StatusNotifier.cpp
    src/SystemStatus.cpp
    src/Taskbar.cpp
    src/Tray.cpp
    ${idle_header}
    ${idle_code}
)
target_include_directories(tde-hermes PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tde-hermes PRIVATE TDE_SESSION_VERSION="${PROJECT_VERSION}")
target_link_libraries(tde-hermes PRIVATE
    hermes-core Qt6::Gui Qt6::Widgets Qt6::DBus LayerShellQt::Interface Tde::Tde PolkitQt6-1::Agent
    PkgConfig::CANBERRA PkgConfig::PULSE PkgConfig::WAYLAND_CLIENT)
install(TARGETS tde-hermes)

if(TDE_SESSION_BUILD_TESTS)
    add_subdirectory(tests)
endif()
