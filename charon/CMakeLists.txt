find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)
pkg_check_modules(GDK_PIXBUF REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(WLR_PROTOCOLS wlr-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
include(GNUInstallDirs)

# The login screen covers the screens as a layer surface, sets the pointer by its shape, and
# turns the screens off when left alone. Layer shell's popups are of xdg-shell, and cursor
# shapes name tablet tools, so their code is needed too.
set(protocol_sources)
foreach(xml ${WLR_PROTOCOLS}/unstable/wlr-layer-shell-unstable-v1.xml
            ${WLR_PROTOCOLS}/unstable/wlr-output-power-management-unstable-v1.xml
            ${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml
            ${WAYLAND_PROTOCOLS}/stable/tablet/tablet-v2.xml
            ${WAYLAND_PROTOCOLS}/staging/cursor-shape/cursor-shape-v1.xml
            ${WAYLAND_PROTOCOLS}/staging/ext-idle-notify/ext-idle-notify-v1.xml)
    get_filename_component(name ${xml} NAME_WE)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${name}-client-protocol.h)
    set(code ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c)
    add_custom_command(OUTPUT ${header} COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header} DEPENDS ${xml})
    add_custom_command(OUTPUT ${code} COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code} DEPENDS ${xml})
    list(APPEND protocol_sources ${header} ${code})
endforeach()

# What can be tested without a screen or greetd.
add_library(charon-core STATIC src/Greetd.cpp src/Json.cpp src/Sessions.cpp src/Users.cpp)
target_include_directories(charon-core PUBLIC src)
target_link_libraries(charon-core PUBLIC cerberus-common PkgConfig::SYSTEMD)
# X sessions are started by startx with Charon itself as their first client; see sessionCommand.
target_compile_definitions(charon-core PRIVATE TDE_CHARON_PATH="${CMAKE_INSTALL_FULL_BINDIR}/tde-charon")

add_executable(tde-charon
    src/main.cpp
    src/Greeter.cpp
    src/Power.cpp
    src/Screen.cpp
    ${protocol_sources}
)
target_include_directories(tde-charon PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tde-charon PRIVATE
    TDE_SESSION_VERSION="${PROJECT_VERSION}"
    TDE_GREETER_DIR="${TDE_GREETER_DIR}")
target_link_libraries(tde-charon PRIVATE charon-core
    PkgConfig::WAYLAND_CLIENT PkgConfig::XKBCOMMON PkgConfig::PANGOCAIRO PkgConfig::GDK_PIXBUF)
install(TARGETS tde-charon)

# The login screen's user, which greetd makes too, and the folder it keeps its memory and the
# settings it was given in.
install(FILES data/sysusers.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/sysusers.d RENAME tde-charon.conf)
configure_file(data/tmpfiles.conf.in tmpfiles.conf @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/tmpfiles.conf DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/tmpfiles.d
    RENAME tde-charon.conf)
# Examples of greetd's configuration and PAM service, to copy over its own.
install(FILES data/greetd.toml data/greetd.pam data/greetd-greeter.pam DESTINATION ${CMAKE_INSTALL_DATADIR}/tde/charon)

if(TDE_SESSION_BUILD_TESTS)
    add_executable(tst_charon tests/tst_charon.cpp)
    target_link_libraries(tst_charon PRIVATE charon-core)
    add_test(NAME tst_charon COMMAND tst_charon)

    # The login screen drawn, as PNG files, to look at.
    add_executable(charon-pictures tests/pictures.cpp src/Screen.cpp)
    target_link_libraries(charon-pictures PRIVATE charon-core)
endif()
