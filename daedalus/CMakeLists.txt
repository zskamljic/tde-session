find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Widgets DBus)
find_package(Tde 0.2 REQUIRED)

set(CMAKE_AUTOMOC ON)

add_executable(tde-daedalus
    src/main.cpp
    src/BackgroundPage.cpp
    src/DisplaysPage.cpp
    src/LockPage.cpp
    src/NetworkPage.cpp
    src/Pages.cpp
    src/SettingsWindow.cpp
    src/Widgets.cpp
)
target_compile_definitions(tde-daedalus PRIVATE TDE_SESSION_VERSION="${PROJECT_VERSION}")
target_link_libraries(tde-daedalus PRIVATE Qt6::Gui Qt6::Widgets Qt6::DBus Tde::Tde tde-shell)
install(TARGETS tde-daedalus)
install(FILES tde-daedalus.desktop DESTINATION share/applications)

if(TDE_SESSION_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(tst_lockpage tests/tst_lockpage.cpp src/LockPage.cpp src/Widgets.cpp)
    target_include_directories(tst_lockpage PRIVATE src)
    target_link_libraries(tst_lockpage PRIVATE Qt6::Widgets Qt6::Test Tde::Tde tde-shell)
    add_test(NAME tst_lockpage COMMAND tst_lockpage)

    add_executable(tst_arrangement tests/tst_arrangement.cpp src/DisplaysPage.cpp src/Widgets.cpp)
    target_include_directories(tst_arrangement PRIVATE src)
    target_link_libraries(tst_arrangement PRIVATE Qt6::Widgets Qt6::Test Tde::Tde tde-shell)
    add_test(NAME tst_arrangement COMMAND tst_arrangement)
endif()
