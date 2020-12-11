cmake_minimum_required(VERSION 3.5.1)
project(xlaserpointer CXX C)

find_package(PkgConfig REQUIRED)
find_package(X11 REQUIRED) 
pkg_search_module(CAIRO REQUIRED IMPORTED_TARGET cairo)

set(SOURCES
    src/main.cpp
)


add_executable(xlaserpointer ${SOURCES})
target_link_libraries(xlaserpointer
    ${X11_X11_LIB}
    ${X11_Xcomposite_LIB}
    ${X11_Xfixes_LIB}
    ${X11_Xi_LIB}
    ${CAIRO_LIBRARIES}
)

set_property(TARGET xlaserpointer PROPERTY CXX_STANDARD 11)

