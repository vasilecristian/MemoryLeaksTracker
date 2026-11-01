cmake_minimum_required(VERSION 3.15)

project(MemoryLeaksTracker LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
	set(MLT_IS_TOP_LEVEL ON)
else()
	set(MLT_IS_TOP_LEVEL OFF)
endif()

# The library is C++11. Building it as C++11 keeps that promise honest.
# (MSVC has no C++11 mode; it uses its oldest one, C++14.)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: static C runtime (/MT, /MTd), like the Visual Studio projects in prj/vs2019.
# Everything linked with this library must use the same runtime. Only set when this is the
# top-level project, so a parent project keeps its own choice.
if(MLT_IS_TOP_LEVEL AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Threads REQUIRED)

add_library(MemoryLeaksTracker STATIC
	include/MemoryLeaksTracker/MemoryLT.h
	source/MemoryLT.cpp
)
target_include_directories(MemoryLeaksTracker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The tracker is on in Debug builds only. PUBLIC, so the library and the code that uses it are
# always built with the same value.
target_compile_definitions(MemoryLeaksTracker PUBLIC MLT_ENABLED=$<IF:$<CONFIG:Debug>,1,0>)
target_compile_options(MemoryLeaksTracker PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/W4,-Wall;-Wextra>")
target_link_libraries(MemoryLeaksTracker PUBLIC Threads::Threads)

option(MLT_BUILD_TESTS "Build the MemoryLeaksTracker tests" ${MLT_IS_TOP_LEVEL})
if(MLT_BUILD_TESTS)
	enable_testing()
	add_subdirectory(test)
endif()
