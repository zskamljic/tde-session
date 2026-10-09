find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)
pkg_check_modules(PAM REQUIRED IMPORTED_TARGET pam)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
include(GNUInstallDirs)

set(xml ${WAYLAND_PROTOCOLS}/staging/ext-session-lock/ext-session-lock-v1.xml)
set(header ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-client-protocol.h)
set(code ${CMAKE_CURRENT_BINARY_DIR}/ext-session-lock-v1-protocol.c)
add_custom_command(OUTPUT ${header} COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${header} DEPENDS ${xml})
add_custom_command(OUTPUT ${code} COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code} DEPENDS ${xml})

add_executable(tde-cerberus
    src/main.cpp
    src/Auth.cpp
    src/Face.cpp
    src/Fingerprint.cpp
    src/Locker.cpp
    src/Password.cpp
    ${header}
    ${code}
)
target_include_directories(tde-cerberus PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tde-cerberus PRIVATE TDE_SESSION_VERSION="${PROJECT_VERSION}")
target_link_libraries(tde-cerberus PRIVATE
    PkgConfig::WAYLAND_CLIENT PkgConfig::XKBCOMMON PkgConfig::PANGOCAIRO PkgConfig::PAM PkgConfig::SYSTEMD)
install(TARGETS tde-cerberus)
# How passwords are checked: as at the login prompt.
install(FILES tde-cerberus.pam DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/pam.d RENAME tde-cerberus)

if(TDE_SESSION_BUILD_TESTS)
    add_executable(tst_password tests/tst_password.cpp src/Password.cpp)
    target_include_directories(tst_password PRIVATE src)
    add_test(NAME tst_password COMMAND tst_password)
endif()
