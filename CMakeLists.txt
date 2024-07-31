cmake_minimum_required(VERSION 3.28)
project(pallas_tui)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PALLAS REQUIRED pallas)

include(FindCurses)

link_libraries(ncurses)

link_libraries(${PALLAS_LIBRARIES})
link_directories(${PALLAS_LIBRARY_DIRS})
include_directories(${PALLAS_INCLUDE_DIRS})

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_BUILD_TYPE Debug)

add_executable(pallas_tui src/main.cpp src/tui.cpp src/helpers.cpp)

install(
  TARGETS pallas_tui
)
