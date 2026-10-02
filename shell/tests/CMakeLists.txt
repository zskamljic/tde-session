find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(tst_applications tst_applications.cpp)
target_link_libraries(tst_applications PRIVATE tde-shell Qt6::Test)
add_test(NAME tst_applications COMMAND tst_applications)
