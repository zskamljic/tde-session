cmake_minimum_required(VERSION 3.25)
project(tde-session VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

option(TDE_SESSION_BUILD_TESTS "Build the unit tests" ON)
if(TDE_SESSION_BUILD_TESTS)
    enable_testing()
endif()

set(TDE_DATA_DIR ${CMAKE_INSTALL_PREFIX}/share/tde)
configure_file(tde-session.in tde-session @ONLY)

install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/tde-session DESTINATION bin)
install(FILES tde.desktop DESTINATION share/wayland-sessions)
install(FILES data/autostart DESTINATION share/tde)
install(FILES data/tde-session.target DESTINATION lib/systemd/user)
install(FILES data/tde-portals.conf DESTINATION share/xdg-desktop-portal)
# Screenshots and colours picked for other programs, by Argus.
install(FILES data/portal/tde-argus.portal DESTINATION share/xdg-desktop-portal/portals)
# What to share of the screen, asked by Argus too, as XDG_CURRENT_DESKTOP names the session.
include(GNUInstallDirs)
install(FILES data/portal/xdg-desktop-portal-wlr.conf
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/xdg/xdg-desktop-portal-wlr RENAME TDE)
configure_file(data/dbus/org.freedesktop.impl.portal.desktop.tde.argus.service.in
    org.freedesktop.impl.portal.desktop.tde.argus.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.tde.argus.service
    DESTINATION share/dbus-1/services)

add_subdirectory(shell)
add_subdirectory(atlas)
add_subdirectory(argus)
add_subdirectory(hermes)
add_subdirectory(cerberus)
add_subdirectory(daedalus)
