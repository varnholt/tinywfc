add_executable(tinywfc_bench tinywfc_bench.cpp scenarios.h)
target_link_libraries(tinywfc_bench PRIVATE tinywfc::tinywfc tinywfc_warnings)
