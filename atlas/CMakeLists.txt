find_package(PkgConfig REQUIRED)
pkg_check_modules(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.20)
pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(PANGOCAIRO REQUIRED IMPORTED_TARGET pangocairo)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
pkg_search_module(LUA REQUIRED IMPORTED_TARGET lua lua5.5 lua-5.5 lua5.4 lua-5.4 lua54)
pkg_get_variable(WLR_PROTOCOLS wlr-protocols pkgdatadir)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

# The wlroots headers include the server side of layer-shell and of turning outputs off, which
# compositors generate.
set(wlr_headers)
foreach(name wlr-layer-shell-unstable-v1 wlr-output-power-management-unstable-v1)
    set(xml ${WLR_PROTOCOLS}/unstable/${name}.xml)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.h)
    add_custom_command(OUTPUT ${header} COMMAND ${WAYLAND_SCANNER} server-header ${xml} ${header} DEPENDS ${xml})
    list(APPEND wlr_headers ${header})
endforeach()

# TDE's own protocol, which names windows the way ext-foreign-toplevel-list does.
set(protocol_sources)
foreach(xml ${PROJECT_SOURCE_DIR}/protocols/tde-window-info-v1.xml
            ${WAYLAND_PROTOCOLS}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml)
    get_filename_component(name ${xml} NAME_WE)
    set(header ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.h)
    set(code ${CMAKE_CURRENT_BINARY_DIR}/${name}-protocol.c)
    add_custom_command(OUTPUT ${header} COMMAND ${WAYLAND_SCANNER} server-header ${xml} ${header} DEPENDS ${xml})
    add_custom_command(OUTPUT ${code} COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${code} DEPENDS ${xml})
    list(APPEND protocol_sources ${header} ${code})
endforeach()

add_executable(tde-atlas
    src/main.cpp
    src/Capture.cpp
    src/Decoration.cpp
    src/Input.cpp
    src/Lock.cpp
    src/Parts.cpp
    src/Placement.cpp
    src/Server.cpp
    src/Settings.cpp
    src/View.cpp
    src/WindowInfo.cpp
    ${wlr_headers}
    ${protocol_sources}
)
target_include_directories(tde-atlas PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tde-atlas PRIVATE
    WLR_USE_UNSTABLE
    TDE_SESSION_VERSION="${PROJECT_VERSION}"
    TDE_DATA_DIR="${TDE_DATA_DIR}"
    TDE_ASKPASS="${CMAKE_INSTALL_FULL_BINDIR}/tde-askpass")
# The wlroots headers are C; their C-isms are worked around in wlr.hpp.
target_compile_options(tde-atlas PRIVATE -Wno-pedantic -Wno-missing-field-initializers)
target_link_libraries(tde-atlas PRIVATE PkgConfig::WLROOTS PkgConfig::WAYLAND_SERVER PkgConfig::XKBCOMMON PkgConfig::PANGOCAIRO
    PkgConfig::PIXMAN PkgConfig::LIBINPUT PkgConfig::LUA)
install(TARGETS tde-atlas)

if(TDE_SESSION_BUILD_TESTS)
    add_executable(tst_placement tests/tst_placement.cpp src/Placement.cpp)
    target_include_directories(tst_placement PRIVATE src)
    add_test(NAME tst_placement COMMAND tst_placement)
endif()
