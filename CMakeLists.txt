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

# THE SHELL HARNESS'S FAKE HARDWARE SURFACE (#179, phase 1 of the #178 epic).
#
# NAMED SOURCES, NOT file(GLOB). This is the one target where the glob's silent
# ignore is unacceptable: a scenario file that fell out of the build looks exactly
# like a scenario that passes, which is the defect shape this whole epic exists to
# close.
#
# PR1 compiles NONE of shell/src/main.cpp. That is the next step, whose deliverable
# is a link rather than a test -- it is where every SDK dependency the 47-method
# census missed gets discovered, and it cannot be split because a half-faked
# surface does not link.
#
# BEFORE PRIVATE on the include path, so <Arduino.h> and the rest resolve to the
# fakes for THIS TARGET ONLY. `make firmware` compiles the same sources against the
# real headers on every PR, which is what checks the fakes' signatures. Nothing
# checks their semantics; that is the honest limit and it is written at the top of
# every one of them.
add_executable(shell_fake_surface
  ${CMAKE_SOURCE_DIR}/test/shell/fake_surface_smoke.cpp
  ${CMAKE_SOURCE_DIR}/test/shell/fake_arduino/harness_state.cpp)
target_include_directories(shell_fake_surface BEFORE PRIVATE
  ${CMAKE_SOURCE_DIR}/test/shell/fake_arduino)
target_include_directories(shell_fake_surface SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/test/vendor)
target_compile_options(shell_fake_surface PRIVATE ${READER_WARNINGS})
add_test(NAME shell_fake_surface COMMAND shell_fake_surface)

# THE SHELL UNDER TEST (#179, phase 1's second half). This is what makes
# shell/src/main.cpp exist on the desktop at all -- 9,331 lines that until now no
# test could reach, and where five recorded defects have hidden.
#
# NAMED SOURCES, for the fake surface's reason: a scenario that fell out of a glob
# looks exactly like one that passes.
#
# THE FOUR TWINS replace shell files at seams that ALREADY EXIST rather than faking
# what sits behind them -- input_task.h's five functions, and three classes that
# each satisfy a reader:: interface. That is the rule that keeps the scaffolding
# from doubling, and it is why sd_fs.cpp, wifi_radio_arduino.cpp and
# http_transport_arduino.cpp are absent here rather than compiled against fakes.
#
# session.cpp, wifi_store_nvs.cpp, wallabag_store_nvs.cpp and card_file_sink.cpp
# ARE compiled: they are thin adapters over Preferences and SdFat, both of which
# the fake surface provides honestly.
add_library(shell_under_test STATIC
  ${CMAKE_SOURCE_DIR}/shell/src/main.cpp
  ${CMAKE_SOURCE_DIR}/shell/src/session.cpp
  ${CMAKE_SOURCE_DIR}/shell/src/wifi_store_nvs.cpp
  ${CMAKE_SOURCE_DIR}/shell/src/wallabag_store_nvs.cpp
  ${CMAKE_SOURCE_DIR}/shell/src/card_file_sink.cpp
  # WITHOUT -DENCRE_FS_SELFTEST ITS WHOLE BODY IS `return -1;`, which is the
  # honest answer on a desktop: the 27-clause contract it runs is meant for a REAL
  # card, and -1 rather than 0 is what stops "did not run" reading as "passed".
  # Compiled so main.cpp's one call to it resolves -- it was the ONLY undefined
  # symbol in the whole 9,331-line file, which is a smaller number than expected.
  ${CMAKE_SOURCE_DIR}/shell/src/sd_selftest.cpp
  ${CMAKE_SOURCE_DIR}/test/shell/input_task_host.cpp
  ${CMAKE_SOURCE_DIR}/test/shell/sd_fs_host.cpp
  ${CMAKE_SOURCE_DIR}/test/shell/wifi_radio_host.cpp
  ${CMAKE_SOURCE_DIR}/test/shell/http_transport_host.cpp
  ${CMAKE_SOURCE_DIR}/test/shell/fake_arduino/harness_state.cpp)
target_include_directories(shell_under_test BEFORE PRIVATE
  ${CMAKE_SOURCE_DIR}/test/shell/fake_arduino
  ${CMAKE_SOURCE_DIR}/shell/src)
target_include_directories(shell_under_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(shell_under_test PRIVATE reader_core)
target_compile_options(shell_under_test PRIVATE ${READER_WARNINGS})

# THE LINK IS THE DELIVERABLE OF THIS STEP, so it gets a test that does nothing but
# prove it. main.cpp defines no main() -- setup() at :6009 and loop() at :8071 are
# its only entry points, and 86 of its 89 top-level definitions are file-static --
# so the harness declares those two and owns main() itself. That static wall IS the
# interface, and it is forced rather than chosen.
add_executable(shell_link_smoke ${CMAKE_SOURCE_DIR}/test/shell/link_smoke.cpp)

add_executable(shell_harness ${CMAKE_SOURCE_DIR}/test/shell/harness_main.cpp)
target_include_directories(shell_harness BEFORE PRIVATE
  ${CMAKE_SOURCE_DIR}/test/shell/fake_arduino)
target_link_libraries(shell_harness PRIVATE shell_under_test reader_core)
target_include_directories(shell_harness BEFORE PRIVATE ${CMAKE_SOURCE_DIR}/shell/src)
target_compile_options(shell_harness PRIVATE ${READER_WARNINGS})
target_compile_definitions(shell_harness PRIVATE
  TRANSCRIPT_DIR="${CMAKE_SOURCE_DIR}/test/shell/transcript"
  BUILD_DIR="${CMAKE_BINARY_DIR}")

# ONE add_test PER SCENARIO, because one scenario per process is the rule -- see
# harness_main.cpp. This is reader_sim's own idiom, which has nine of them.
add_test(NAME shell_boot_cold COMMAND shell_harness boot_cold_card_present)
add_test(NAME shell_boot_no_card COMMAND shell_harness boot_no_card)
add_test(NAME shell_press_down COMMAND shell_harness press_down_on_home)
target_include_directories(shell_link_smoke BEFORE PRIVATE
  ${CMAKE_SOURCE_DIR}/test/shell/fake_arduino)
target_include_directories(shell_link_smoke SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/test/vendor)
target_link_libraries(shell_link_smoke PRIVATE shell_under_test reader_core)
target_compile_options(shell_link_smoke PRIVATE ${READER_WARNINGS})
add_test(NAME shell_link_smoke COMMAND shell_link_smoke)

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

# THE SLEEP CARD'S CHAPTER RUN, against every real chapter name in the corpus.
# Not a test, for corpus_probe's reason: it needs real books, and the repo's
# generated EPUBs have one-word chapter labels that fit everything.
add_executable(sleep_chapter_probe ${CMAKE_SOURCE_DIR}/tools/sleep_chapter_probe.cpp)
target_link_libraries(sleep_chapter_probe PRIVATE reader_core)
target_compile_options(sleep_chapter_probe PRIVATE ${READER_WARNINGS})
target_compile_definitions(sleep_chapter_probe PRIVATE READER_DESKTOP=1)

add_executable(progress_probe ${CMAKE_SOURCE_DIR}/tools/progress_probe.cpp)
target_link_libraries(progress_probe PRIVATE reader_core)
target_compile_options(progress_probe PRIVATE ${READER_WARNINGS})
target_compile_definitions(progress_probe PRIVATE READER_DESKTOP=1
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")

add_executable(paging_probe ${CMAKE_SOURCE_DIR}/tools/paging_probe.cpp)
target_link_libraries(paging_probe PRIVATE reader_core)
target_compile_options(paging_probe PRIVATE ${READER_WARNINGS})
target_compile_definitions(paging_probe PRIVATE READER_DESKTOP=1
  ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")

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
