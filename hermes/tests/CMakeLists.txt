find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_taskbar tst_taskbar.cpp)
target_link_libraries(tst_taskbar PRIVATE hermes-core Qt6::Test)
add_test(NAME tst_taskbar COMMAND tst_taskbar)

add_executable(tst_dbusmenu tst_dbusmenu.cpp)
target_link_libraries(tst_dbusmenu PRIVATE hermes-core Qt6::Test)
add_test(NAME tst_dbusmenu COMMAND tst_dbusmenu)
set_tests_properties(tst_dbusmenu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_notifications tst_notifications.cpp)
target_link_libraries(tst_notifications PRIVATE hermes-core Qt6::Test)
add_test(NAME tst_notifications COMMAND tst_notifications)
