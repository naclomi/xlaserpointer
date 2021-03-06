cmake_minimum_required(VERSION 3.5.1)
project(xlaserpointer CXX C)

find_package(PkgConfig REQUIRED)
find_package(X11 REQUIRED) 
pkg_search_module(CAIRO REQUIRED IMPORTED_TARGET cairo)

set(SOURCES
    external/argagg.hpp
    external/csscolorparser.cpp
    external/csscolorparser.hpp
    src/main.cpp
)

include_directories(external)

add_executable(xlaserpointer ${SOURCES})
set_property(TARGET xlaserpointer PROPERTY CXX_STANDARD 17)
target_link_libraries(xlaserpointer
    ${X11_X11_LIB}
    ${X11_Xfixes_LIB}
    ${X11_Xi_LIB}
    ${CAIRO_LIBRARIES}
)

install(TARGETS xlaserpointer DESTINATION bin)
