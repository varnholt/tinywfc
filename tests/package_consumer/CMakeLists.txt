# builds against an installed tinywfc to check that find_package works and pulls in nothing but the header
#   cmake --install <build> --prefix <prefix>
#   cmake -S tests/package_consumer -B <out> -DCMAKE_PREFIX_PATH=<prefix>
cmake_minimum_required(VERSION 3.25)
project(tinywfc_package_consumer LANGUAGES CXX)

find_package(tinywfc 1 REQUIRED)

get_target_property(link_libraries tinywfc::tinywfc INTERFACE_LINK_LIBRARIES)
if(link_libraries)
    message(FATAL_ERROR "tinywfc::tinywfc must not link anything, but links: ${link_libraries}")
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE tinywfc::tinywfc)
