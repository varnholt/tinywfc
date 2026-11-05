add_executable(tinywfc_test tinywfc_test.cpp test_framework.h)
target_link_libraries(tinywfc_test PRIVATE tinywfc::tinywfc tinywfc_warnings)
add_test(NAME tinywfc_test COMMAND tinywfc_test)

# the testbed's tileset has no gl dependency, so it is tested without building the testbed
add_executable(tinywfc_city_test city_test.cpp ${PROJECT_SOURCE_DIR}/testbed/city.cpp test_framework.h)
target_include_directories(tinywfc_city_test PRIVATE ${PROJECT_SOURCE_DIR}/testbed)
target_link_libraries(tinywfc_city_test PRIVATE tinywfc::tinywfc tinywfc_warnings)
add_test(NAME tinywfc_city_test COMMAND tinywfc_city_test)
