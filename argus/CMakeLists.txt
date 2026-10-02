find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Widgets DBus)
find_package(LayerShellQt 6 REQUIRED)
find_package(Tde 0.2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

set(CMAKE_AUTOMOC ON)

# C bindings for the protocols the overview speaks, generated from their descriptions.
set(protocols
    ${WAYLAND_PROTOCOLS}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml
    ${WAYLAND_PROTOCOLS}/staging/ext-image-capture-source/ext-image-capture-source-v1.xml
    ${WAYLAND_PROTOCOLS}/staging/ext-image-copy-capture/ext-image-copy-capture-v1.xml
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

add_executable(tde-argus
    src/main.cpp
    src/AppGrid.cpp
    src/Overview.cpp
    src/Toplevels.cpp
    ${protocol_sources}
)
target_include_directories(tde-argus PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(tde-argus PRIVATE TDE_SESSION_VERSION="${PROJECT_VERSION}")
target_link_libraries(tde-argus PRIVATE
    Qt6::Gui Qt6::Widgets Qt6::DBus LayerShellQt::Interface Tde::Tde PkgConfig::WAYLAND_CLIENT tde-shell)
install(TARGETS tde-argus)
