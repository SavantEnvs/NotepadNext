# Functional-test build for mayhem/test.sh (mayhem layer, not upstream). Built with the project's
# normal flags (no sanitizers, no SanCov) by mayhem/build.sh into build-tests/.
#
# NotepadNext has no test suite of its own; these are the upstream test suites of the libraries it
# vendors and runs on every document:
#  - TestLexers (thirdparty/lexilla/test): lexes and folds every example under test/examples with
#    its lexer and diffs the result against the committed .styled/.folded golden files.
#  - unitTest (thirdparty/scintilla/test/unit): Catch unit tests of Scintilla's document core
#    (CellBuffer, Document, UniConversion, RESearch, ...).
cmake_minimum_required(VERSION 3.21)
project(NotepadNextTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty)

file(GLOB LEXILLA_SRC ${TP}/lexilla/lexlib/*.cxx ${TP}/lexilla/lexers/*.cxx)
add_executable(TestLexers
    ${TP}/lexilla/test/TestLexers.cxx
    ${TP}/lexilla/test/TestDocument.cxx
    ${TP}/lexilla/access/LexillaAccess.cxx
    ${TP}/lexilla/src/Lexilla.cxx
    ${LEXILLA_SRC})
target_compile_definitions(TestLexers PRIVATE LEXILLA_STATIC)
target_include_directories(TestLexers PRIVATE
    ${TP}/scintilla/include ${TP}/lexilla/include ${TP}/lexilla/access ${TP}/lexilla/lexlib)

file(GLOB SCI_UNIT_SRC ${TP}/scintilla/test/unit/test*.cxx)
list(APPEND SCI_UNIT_SRC ${TP}/scintilla/test/unit/unitTest.cxx)
set(SCI_TESTED CaseConvert CaseFolder CellBuffer ChangeHistory CharacterCategoryMap CharClassify
    ContractionState Decoration Document Geometry PerLine RESearch RunStyles Selection UndoHistory
    UniConversion UniqueString)
list(TRANSFORM SCI_TESTED PREPEND ${TP}/scintilla/src/)
list(TRANSFORM SCI_TESTED APPEND .cxx)
add_executable(unitTest ${SCI_UNIT_SRC} ${SCI_TESTED})
target_include_directories(unitTest PRIVATE ${TP}/scintilla/include ${TP}/scintilla/src)
