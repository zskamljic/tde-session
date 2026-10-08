# tde-askpass, which asks for passwords for ssh and sudo, as SSH_ASKPASS and SUDO_ASKPASS.
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets DBus)
find_package(Tde 0.2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)

set(CMAKE_AUTOMOC ON)

add_library(askpass-core STATIC src/Prompt.cpp)
target_include_directories(askpass-core PUBLIC src)
target_link_libraries(askpass-core PUBLIC Qt6::Core)

add_executable(tde-askpass src/main.cpp src/Keyring.cpp)
target_compile_definitions(tde-askpass PRIVATE TDE_SESSION_VERSION="${PROJECT_VERSION}")
target_link_libraries(tde-askpass PRIVATE askpass-core Qt6::Widgets Qt6::DBus Tde::Tde PkgConfig::LIBSECRET)
install(TARGETS tde-askpass)

if(TDE_SESSION_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_prompt tests/tst_prompt.cpp)
    target_link_libraries(tst_prompt PRIVATE askpass-core Qt6::Test)
    add_test(NAME tst_prompt COMMAND tst_prompt)
endif()
