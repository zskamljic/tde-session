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

add_subdirectory(shell)
add_subdirectory(atlas)
add_subdirectory(argus)
