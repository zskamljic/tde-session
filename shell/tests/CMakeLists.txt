find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_applications tst_applications.cpp)
target_link_libraries(tst_applications PRIVATE tde-shell Qt6::Test)
add_test(NAME tst_applications COMMAND tst_applications)

add_executable(tst_sessionconfig tst_sessionconfig.cpp)
target_link_libraries(tst_sessionconfig PRIVATE tde-shell Qt6::Test)
add_test(NAME tst_sessionconfig COMMAND tst_sessionconfig)

add_executable(tst_defaultapps tst_defaultapps.cpp)
target_link_libraries(tst_defaultapps PRIVATE tde-shell Qt6::Test)
add_test(NAME tst_defaultapps COMMAND tst_defaultapps)

add_executable(tst_gtksettings tst_gtksettings.cpp)
target_link_libraries(tst_gtksettings PRIVATE tde-shell Qt6::Test)
add_test(NAME tst_gtksettings COMMAND tst_gtksettings)
