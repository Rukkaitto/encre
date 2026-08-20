cmake_minimum_required(VERSION 3.20)
project(ereader LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB READER_CORE_SOURCES ${CMAKE_SOURCE_DIR}/core/src/*.cpp)
add_library(reader_core STATIC ${READER_CORE_SOURCES})
target_include_directories(reader_core PUBLIC ${CMAKE_SOURCE_DIR}/core/include)
target_include_directories(reader_core PRIVATE ${CMAKE_SOURCE_DIR}/test/vendor)
target_compile_definitions(reader_core PRIVATE READER_DESKTOP=1)
# A library needs at least one TU before core sources exist:
if(NOT READER_CORE_SOURCES)
  file(WRITE ${CMAKE_BINARY_DIR}/_empty.cpp "namespace { int _unused; }\n")
  target_sources(reader_core PRIVATE ${CMAKE_BINARY_DIR}/_empty.cpp)
endif()

enable_testing()
file(GLOB TEST_SOURCES ${CMAKE_SOURCE_DIR}/test/unit/*.cpp)
add_executable(unit_tests ${TEST_SOURCES})
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/test/vendor)
target_link_libraries(unit_tests PRIVATE reader_core)
target_compile_definitions(unit_tests PRIVATE
  ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  GOLDEN_DIR="${CMAKE_SOURCE_DIR}/test/golden"
  BUILD_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME unit COMMAND unit_tests)

add_executable(reader_sim ${CMAKE_SOURCE_DIR}/sim/main.cpp)
target_link_libraries(reader_sim PRIVATE reader_core)
target_compile_definitions(reader_sim PRIVATE ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME sim_home COMMAND reader_sim home ${CMAKE_BINARY_DIR}/sim_home.png)
