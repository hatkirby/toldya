cmake_minimum_required (VERSION 3.1)
project (toldya)

add_subdirectory(vendor/libtwittercpp)
add_subdirectory(vendor/yaml-cpp EXCLUDE_FROM_ALL)

include_directories(vendor/libtwittercpp/src vendor/yaml-cpp/include)
add_executable(toldya toldya.cpp)
set_property(TARGET toldya PROPERTY CXX_STANDARD 11)
set_property(TARGET toldya PROPERTY CXX_STANDARD_REQUIRED ON)
target_link_libraries(toldya yaml-cpp twitter++)
