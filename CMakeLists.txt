cmake_minimum_required(VERSION 3.28)
project(pallas_explorer)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PALLAS REQUIRED pallas)

include(FindCurses)

link_libraries(pallas)

set(CMAKE_CXX_STANDARD 20)

add_executable(pallas_explorer src/main.cpp)
