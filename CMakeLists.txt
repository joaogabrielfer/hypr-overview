cmake_minimum_required(VERSION 3.24)

project(hypr-overview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND REQUIRED hyprland)

add_library(hypr-overview SHARED src/main.cpp)

target_include_directories(hypr-overview SYSTEM PRIVATE ${HYPRLAND_INCLUDE_DIRS})
target_compile_options(hypr-overview PRIVATE ${HYPRLAND_CFLAGS_OTHER} -fno-gnu-unique -Wall -Wextra -Wpedantic -Wno-template-body)
target_link_directories(hypr-overview PRIVATE ${HYPRLAND_LIBRARY_DIRS})
target_link_libraries(hypr-overview PRIVATE ${HYPRLAND_LIBRARIES})
target_link_options(hypr-overview PRIVATE ${HYPRLAND_LDFLAGS_OTHER})

set_target_properties(hypr-overview PROPERTIES PREFIX "" OUTPUT_NAME "hypr-overview")
