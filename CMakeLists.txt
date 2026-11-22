cmake_minimum_required(VERSION 3.25)

project(F1DockTimers VERSION 1.1.1 LANGUAGES CXX
        DESCRIPTION "F1 schedule, countdown and standings for the Windows tray (C++/Qt port)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

option(F1_BUILD_TESTS "Build the unit tests" ON)
option(F1_DEPLOY "Run windeployqt after building the app (copies the Qt DLLs next to the exe)" ON)

# Qt location: CMAKE_PREFIX_PATH from the preset, or QT_ROOT_DIR / Qt6_DIR from the environment
# (the CI action sets those).
if(DEFINED ENV{QT_ROOT_DIR})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_ROOT_DIR}")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network Svg)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    # Smaller binaries: drop unused functions and fold identical ones in Release.
    add_link_options($<$<CONFIG:Release,MinSizeRel>:/OPT:REF> $<$<CONFIG:Release,MinSizeRel>:/OPT:ICF>)
endif()
add_compile_definitions(QT_NO_KEYWORDS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)

add_subdirectory(src)

if(F1_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
