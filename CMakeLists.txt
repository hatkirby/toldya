cmake_minimum_required (VERSION 3.1)
project (toldya)

add_subdirectory(vendor/libtwittercpp)

find_package(PkgConfig)
pkg_check_modules(yaml-cpp yaml-cpp REQUIRED)

include_directories(
  vendor/libtwittercpp/src
  ${yaml-cpp_INCLUDE_DIRS})

add_executable(toldya toldya.cpp)
set_property(TARGET toldya PROPERTY CXX_STANDARD 11)
set_property(TARGET toldya PROPERTY CXX_STANDARD_REQUIRED ON)
target_link_libraries(toldya ${yaml-cpp_LIBRARIES} twitter++)
