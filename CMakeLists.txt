cmake_minimum_required(VERSION 3.20)
project(ereader LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warnings for first-party code only. Vendored headers (third_party/, doctest)
# are included as SYSTEM below so their own diagnostics stay quiet. No -Werror:
# a newer compiler must not be able to break the build.
if(MSVC)
  set(READER_WARNINGS /W4)
  set(READER_NO_WARNINGS /w)
else()
  set(READER_WARNINGS -Wall -Wextra)
  set(READER_NO_WARNINGS -w)
endif()

file(GLOB READER_CORE_SOURCES ${CMAKE_SOURCE_DIR}/core/src/*.cpp)
# TJpgDec is the one vendored SOURCE file in third_party rather than a vendored
# header, so it is named here instead of arriving through the glob above -- and
# it is C, which is why this project declares a C language at all. It cannot be
# compiled as C++: it assigns alloc_pool's void* straight to typed pointers
# throughout, which is legal C and an error in C++.
#
# It ships in BOTH builds, unlike png.cpp and host_fs.cpp, so it is a srcFilter
# exclusion nowhere. The firmware reaches it through a lib_deps entry on
# third_party itself -- see platformio.ini, which records why the obvious
# build_src_filter route does not work and fails silently.
set(TJPGD_SOURCE ${CMAKE_SOURCE_DIR}/third_party/tjpgd.c)
# Vendored, so it is not held to first-party warnings. A source-level option is
# appended after the target's, so this wins over READER_WARNINGS below without
# turning them off for anything else.
set_source_files_properties(${TJPGD_SOURCE} PROPERTIES COMPILE_OPTIONS "${READER_NO_WARNINGS}")
add_library(reader_core STATIC ${READER_CORE_SOURCES} ${TJPGD_SOURCE})
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
  BUILD_DIR="${CMAKE_BINARY_DIR}"
  TEST_FIXTURE_DIR="${CMAKE_SOURCE_DIR}/test/unit/fixtures")
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
# DESIGN_DIR, because two sleep subcommands render the board's OWN cover image
# (design/assets/sleep-cover-<W>x<H>.png) rather than a picture of their own -- see
# sim/main.cpp's BoardCover. It is the design tree and not the assets tree because
# that is where the boards that display it live, and generated-from-one-source is
# the rule those files exist to keep.
target_compile_definitions(reader_sim PRIVATE
  ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  DESIGN_DIR="${CMAKE_SOURCE_DIR}/design")
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
# The Typography panel, on the stack the device will have: Reader -> menu ->
# Typography. Rendered at the X3, which is the geometry with the tighter preview
# budget (246px of content area against the X4's 254), so it is the one where the
# four-line specimen decides. It is NOT yet a check on the navigation -- the menu's
# Typography row is inert until it is made live, and sim/main.cpp says so at the
# flag -- so what this smoke-tests today is that the panel builds and paints.
add_test(NAME sim_typography COMMAND reader_sim typography
         ${CMAKE_BINARY_DIR}/sim_typography.png --canvas 528x792)
# The SD-missing screen renders with NO --root, which is the state it exists for.
add_test(NAME sim_sd_missing COMMAND reader_sim sd_missing ${CMAKE_BINARY_DIR}/sim_sd_missing.png)
# ...and the storage path runs on the desktop: a HostFileSystem mounted on the
# build directory, which has no /.reader/settings.json, so this is the
# "defaults, and say so" case the device takes on a fresh card.
add_test(NAME sim_storage COMMAND reader_sim sd_missing ${CMAKE_BINARY_DIR}/sim_storage.png
         --root ${CMAKE_BINARY_DIR})
