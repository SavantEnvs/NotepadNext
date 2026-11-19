# Fuzz build for NotepadNext (mayhem layer). Configured by mayhem/build.sh; not part of upstream.
#
# Upstream's top-level CMakeLists builds one GUI executable and fetches four Qt libraries with CPM
# (ADS docking, QSimpleUpdater, SingleApplication, editorconfig-core-qt). None of those is on the
# path of the code the harnesses drive, so this file reuses upstream's own CMake for the vendored
# libraries (thirdparty/lua, thirdparty/scintilla, thirdparty/lexilla) and compiles the app's own
# editor sources (ScintillaNext, Finder, QRegexSearch, the HTML/RTF converters) into a static
# library. Every object gets $SANITIZER_FLAGS + SanCov (passed in CMAKE_C/CXX_FLAGS by build.sh).
cmake_minimum_required(VERSION 3.21)
project(NotepadNextFuzz LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(NN_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(FUZZ_ENGINE "-fsanitize=fuzzer" CACHE STRING "libFuzzer link flag")
set(STANDALONE_MAIN "" CACHE FILEPATH "LLVM StandaloneFuzzTargetMain.c")

# As upstream's root CMakeLists: the app replaces Scintilla's regex engine with QRegexSearch.cpp.
add_compile_definitions(SCI_OWNREGEX)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Core5Compat)
set(CMAKE_AUTOMOC ON)

add_subdirectory(${NN_ROOT}/thirdparty/lua lua EXCLUDE_FROM_ALL)
add_subdirectory(${NN_ROOT}/thirdparty/scintilla scintilla EXCLUDE_FROM_ALL)
add_subdirectory(${NN_ROOT}/thirdparty/lexilla lexilla EXCLUDE_FROM_ALL)
# Lexilla needs only Scintilla's public headers (ILexer.h, Scintilla.h); upstream gets them by
# linking the Qt scintilla target, which would drag Qt into the Qt-free Lexilla harness.
target_include_directories(lexilla PUBLIC ${NN_ROOT}/thirdparty/scintilla/include)

# SCI_OWNREGEX: Scintilla's Document calls CreateRegexSearch(), which the app defines in
# QRegexSearch.cpp; compile it into the scintilla library so the static link resolves in order.
target_sources(scintilla PRIVATE ${NN_ROOT}/src/QRegexSearch.cpp)
target_include_directories(scintilla PRIVATE ${NN_ROOT}/src)

# The app's editor core (a subset of src/ — the files the harnesses reach, plus what they link to).
add_library(nnext_core STATIC
    ${NN_ROOT}/src/ScintillaNext.cpp
    ${NN_ROOT}/src/Finder.cpp
    ${NN_ROOT}/src/ScintillaCommenter.cpp
    ${NN_ROOT}/src/SelectionTracker.cpp
    ${NN_ROOT}/src/RangeAllocator.cpp
    ${NN_ROOT}/src/UndoAction.cpp
    ${NN_ROOT}/src/ScintillaSorter.cpp
    ${NN_ROOT}/src/Sorter.cpp
    ${NN_ROOT}/src/Converter.cpp
    ${NN_ROOT}/src/HtmlConverter.cpp
    ${NN_ROOT}/src/RtfConverter.cpp
)
target_include_directories(nnext_core PUBLIC
    ${NN_ROOT}/src
    ${NN_ROOT}/thirdparty/uchardet/src
)
target_link_libraries(nnext_core PUBLIC scintilla lexilla Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Core5Compat)

set(LSAN_OFF_OBJ "" CACHE FILEPATH "mayhem/lsan_off.cc compiled by build.sh")
add_library(standalone_main OBJECT ${STANDALONE_MAIN})

# fuzz_target(<name> <source> <libs...>): the libFuzzer binary and its -standalone reproducer.
function(fuzz_target name src)
    add_executable(${name} ${src} ${LSAN_OFF_OBJ})
    target_link_libraries(${name} PRIVATE ${ARGN})
    target_link_options(${name} PRIVATE ${FUZZ_ENGINE})
    add_executable(${name}-standalone ${src} ${LSAN_OFF_OBJ} $<TARGET_OBJECTS:standalone_main>)
    target_link_libraries(${name}-standalone PRIVATE ${ARGN})
endfunction()

fuzz_target(fuzz_lexilla ${CMAKE_CURRENT_SOURCE_DIR}/harnesses/fuzz_lexilla.cpp lexilla_testdoc lexilla)
add_library(lexilla_testdoc STATIC ${NN_ROOT}/thirdparty/lexilla/test/TestDocument.cxx)
target_link_libraries(lexilla_testdoc PUBLIC lexilla)
target_include_directories(lexilla_testdoc PUBLIC ${NN_ROOT}/thirdparty/lexilla/test)

fuzz_target(fuzz_lua ${CMAKE_CURRENT_SOURCE_DIR}/harnesses/fuzz_lua.c lua m)
fuzz_target(fuzz_file_open ${CMAKE_CURRENT_SOURCE_DIR}/harnesses/fuzz_file_open.cpp nnext_core)
fuzz_target(fuzz_regex_search ${CMAKE_CURRENT_SOURCE_DIR}/harnesses/fuzz_regex_search.cpp nnext_core)
fuzz_target(fuzz_export ${CMAKE_CURRENT_SOURCE_DIR}/harnesses/fuzz_export.cpp nnext_core)
fuzz_target(fuzz_line_ops ${CMAKE_CURRENT_SOURCE_DIR}/harnesses/fuzz_line_ops.cpp nnext_core)
