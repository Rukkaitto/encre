cmake_minimum_required(VERSION 3.20)
project(ereader LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings for first-party code only. Vendored headers (third_party/, doctest)
# are included as SYSTEM below so their own diagnostics stay quiet. No -Werror:
# a newer compiler must not be able to break the build.
if(MSVC)
  set(READER_WARNINGS /W4)
else()
  set(READER_WARNINGS -Wall -Wextra)
endif()

file(GLOB READER_CORE_SOURCES ${CMAKE_SOURCE_DIR}/core/src/*.cpp)
add_library(reader_core STATIC ${READER_CORE_SOURCES})
target_include_directories(reader_core PUBLIC ${CMAKE_SOURCE_DIR}/core/include)
# stb (desktop PNG I/O only; png.cpp is excluded from the firmware build).
target_include_directories(reader_core SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_compile_definitions(reader_core PRIVATE READER_DESKTOP=1)
target_compile_options(reader_core PRIVATE ${READER_WARNINGS})
# A library needs at least one TU before core sources exist:
if(NOT READER_CORE_SOURCES)
  file(WRITE ${CMAKE_BINARY_DIR}/_empty.cpp "namespace { int _unused; }\n")
  target_sources(reader_core PRIVATE ${CMAKE_BINARY_DIR}/_empty.cpp)
endif()

enable_testing()
file(GLOB TEST_SOURCES ${CMAKE_SOURCE_DIR}/test/unit/*.cpp)
add_executable(unit_tests ${TEST_SOURCES})
target_include_directories(unit_tests SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/test/vendor
  ${CMAKE_SOURCE_DIR}/third_party)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/test/unit)
target_link_libraries(unit_tests PRIVATE reader_core)
target_compile_options(unit_tests PRIVATE ${READER_WARNINGS})
target_compile_definitions(unit_tests PRIVATE
  ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  GOLDEN_DIR="${CMAKE_SOURCE_DIR}/test/golden"
  BUILD_DIR="${CMAKE_BINARY_DIR}")
add_test(NAME unit COMMAND unit_tests)

# The NAMES probe (3E): does a capitalisation-and-frequency heuristic yield a list
# worth reading on a real book? Not a test -- it needs a real novel, and the repo's
# generated EPUBs are 1,400-word stubs where every threshold reports zero. The
# findings and the five rules that produced them are in the file's own header.
add_executable(name_probe ${CMAKE_SOURCE_DIR}/tools/name_probe.cpp)
target_link_libraries(name_probe PRIVATE reader_core)
target_compile_options(name_probe PRIVATE ${READER_WARNINGS})
target_compile_definitions(name_probe PRIVATE READER_DESKTOP=1)

# The CORPUS probe: every book in tools/corpus.manifest through the real open path,
# as JSONL. Not a test -- it needs real books, same as name_probe, and the repo's
# generated EPUBs are 1,400-word stubs where nothing interesting fails.
add_executable(corpus_probe ${CMAKE_SOURCE_DIR}/tools/corpus_probe.cpp)
target_link_libraries(corpus_probe PRIVATE reader_core)
target_compile_options(corpus_probe PRIVATE ${READER_WARNINGS})
target_compile_definitions(corpus_probe PRIVATE READER_DESKTOP=1)

add_executable(reader_sim ${CMAKE_SOURCE_DIR}/sim/main.cpp)
target_link_libraries(reader_sim PRIVATE reader_core)
target_compile_options(reader_sim PRIVATE ${READER_WARNINGS})
target_compile_definitions(reader_sim PRIVATE ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")
add_test(NAME sim_home COMMAND reader_sim home ${CMAKE_BINARY_DIR}/sim_home.png)
# The scripted-input path, as a smoke test: Home -> Settings -> Input Monitor,
# with a held Confirm logged there. Catches a broken factory or key parser
# without needing a golden.
add_test(NAME sim_app COMMAND reader_sim app ${CMAKE_BINARY_DIR}/sim_app.png
         --keys DOWN,DOWN,CONFIRM,CONFIRM,CONFIRM+)
# The Library over the board's sample content, reached through the App: the
# subcommand tools/compare-design.py drives, and the one screen whose row count
# depends on the panel it is rendered at.
add_test(NAME sim_library COMMAND reader_sim library ${CMAKE_BINARY_DIR}/sim_library.png
         --canvas 528x792)
# ...and the actions overlay over it, which is the one render that needs
# App::render rather than the top screen alone.
add_test(NAME sim_library_actions COMMAND reader_sim library_actions
         ${CMAKE_BINARY_DIR}/sim_library_actions.png --canvas 528x792)
# ...and the delete confirmation over that, which is four screens deep and the
# one place PopTo is exercised outside the unit tests.
add_test(NAME sim_delete_confirm COMMAND reader_sim delete_confirm
         ${CMAKE_BINARY_DIR}/sim_delete_confirm.png --canvas 480x800)
# Book details, which the actions overlay pushes and which is NOT an overlay: the
# render is this screen alone, over neither of the two beneath it.
add_test(NAME sim_book_details COMMAND reader_sim book_details
         ${CMAKE_BINARY_DIR}/sim_book_details.png --canvas 480x800)
# The SD-missing screen renders with NO --root, which is the state it exists for.
add_test(NAME sim_sd_missing COMMAND reader_sim sd_missing ${CMAKE_BINARY_DIR}/sim_sd_missing.png)
# ...and the storage path runs on the desktop: a HostFileSystem mounted on the
# build directory, which has no /.reader/settings.json, so this is the
# "defaults, and say so" case the device takes on a fresh card.
add_test(NAME sim_storage COMMAND reader_sim sd_missing ${CMAKE_BINARY_DIR}/sim_storage.png
         --root ${CMAKE_BINARY_DIR})
