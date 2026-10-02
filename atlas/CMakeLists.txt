find_package(PkgConfig REQUIRED)
pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.20)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)
pkg_get_variable(WLR_PROTOCOLS wlr-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

# The wlroots headers include the server side of layer-shell, which compositors generate.
set(layer_shell_xml ${WLR_PROTOCOLS}/unstable/wlr-layer-shell-unstable-v1.xml)
set(layer_shell_header ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.h)
add_custom_command(OUTPUT ${layer_shell_header}
    COMMAND ${WAYLAND_SCANNER} server-header ${layer_shell_xml} ${layer_shell_header}
    DEPENDS ${layer_shell_xml})

add_executable(tde-atlas
    src/main.cpp
    src/Decoration.cpp
    src/Input.cpp
    src/Lock.cpp
    src/Parts.cpp
    src/Placement.cpp
    src/Server.cpp
    src/View.cpp
    ${layer_shell_header}
)
target_include_directories(tde-atlas PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tde-atlas PRIVATE
    WLR_USE_UNSTABLE
    TDE_SESSION_VERSION="${PROJECT_VERSION}"
    TDE_DATA_DIR="${TDE_DATA_DIR}")
# The wlroots headers are C; their C-isms are worked around in wlr.hpp.
target_compile_options(tde-atlas PRIVATE -Wno-pedantic -Wno-missing-field-initializers)
target_link_libraries(tde-atlas PRIVATE PkgConfig::WLROOTS PkgConfig::WAYLAND_SERVER PkgConfig::XKBCOMMON PkgConfig::PANGOCAIRO)
install(TARGETS tde-atlas)

if(TDE_SESSION_BUILD_TESTS)
    add_executable(tst_placement tests/tst_placement.cpp src/Placement.cpp)
    target_include_directories(tst_placement PRIVATE src)
    add_test(NAME tst_placement COMMAND tst_placement)
endif()
