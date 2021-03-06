cmake_minimum_required (VERSION 2.6)
project (wayproblems)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(Osmium REQUIRED COMPONENTS < pbf io sparsehash gdal >)

if(NOT OSMIUM_FOUND)
	message(WARNING "Libosmium not found!\n")
endif()

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
        message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

find_package(Protozero)
find_package(Boost 1.55.0 REQUIRED COMPONENTS program_options )
include_directories(${Boost_INCLUDE_DIRS})

add_executable(wayproblems wayproblems.cpp)
add_executable(accesscombinations accesscombinations.cpp)
include_directories(${OSMIUM_INCLUDE_DIRS})
include_directories(${Boost_INCLUDE_DIRS})
include_directories(SYSTEM ${PROTOZERO_INCLUDE_DIR})
target_link_libraries(wayproblems ${OSMIUM_LIBRARIES} ${Boost_LIBRARIES})
target_link_libraries(accesscombinations ${OSMIUM_LIBRARIES} ${Boost_LIBRARIES})

