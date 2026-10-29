cmake_minimum_required (VERSION 3.18.4)
project (wayproblems)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++17" COMPILER_SUPPORTS_CXX17)
if(COMPILER_SUPPORTS_CXX17)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
else()
        message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++17 support. Please use a different C++ compiler.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(Osmium REQUIRED COMPONENTS io gdal)

find_package(Boost CONFIG 1.70.0 REQUIRED COMPONENTS program_options)
include_directories(${Boost_INCLUDE_DIRS})

find_package(yaml-cpp CONFIG QUIET)
if(TARGET yaml-cpp::yaml-cpp)
	set(YAML_CPP_LIB yaml-cpp::yaml-cpp)
else()
	find_package(yaml-cpp MODULE REQUIRED)
	set(YAML_CPP_LIB ${YAML_CPP_LIBRARIES})
	include_directories(${YAML_CPP_INCLUDE_DIRS})
endif()

find_path(JSON_INCLUDE_DIR
	NAMES "nlohmann/json.hpp"
	PATHS
	/usr/include/
	/usr/include/
	${CMAKE_SOURCE_DIR}/nlohmann-json/include
	${CMAKE_SOURCE_DIR}/../nlohmann-json/include
	${CMAKE_SYSROOT}/usr/local/include
	)
message("nlohmann directory ${JSON_INCLUDE_DIR}")

add_executable(wayproblems wayproblems.cpp yamlrules.cpp)
add_executable(accesscombinations accesscombinations.cpp)
include_directories(${OSMIUM_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS} ${JSON_INCLUDE_DIR})
include_directories(SYSTEM ${PROTOZERO_INCLUDE_DIR})
target_link_libraries(wayproblems ${OSMIUM_LIBRARIES} ${Boost_LIBRARIES} ${YAML_CPP_LIB})
target_link_libraries(accesscombinations ${OSMIUM_LIBRARIES} ${Boost_LIBRARIES})

