cmake_minimum_required(VERSION 3.20)

get_filename_component(
    OPEN_SLEX_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)

if(NOT DEFINED OPEN_SLEX_CLANG_FORMAT_MODE)
    set(OPEN_SLEX_CLANG_FORMAT_MODE check)
endif()
if(NOT OPEN_SLEX_CLANG_FORMAT_MODE MATCHES "^(check|fix)$")
    message(FATAL_ERROR
        "OPEN_SLEX_CLANG_FORMAT_MODE must be 'check' or 'fix'"
    )
endif()

find_program(
    OPEN_SLEX_CLANG_FORMAT_EXECUTABLE
    NAMES clang-format
    DOC "clang-format executable"
)
if(NOT OPEN_SLEX_CLANG_FORMAT_EXECUTABLE)
    message(FATAL_ERROR
        "clang-format was not found. Install clang-format 22.1.8 and add it "
        "to PATH."
    )
endif()

file(GLOB_RECURSE OPEN_SLEX_FORMAT_SOURCES
    LIST_DIRECTORIES false
    "${OPEN_SLEX_SOURCE_DIR}/fuzz/*.c"
    "${OPEN_SLEX_SOURCE_DIR}/fuzz/*.cc"
    "${OPEN_SLEX_SOURCE_DIR}/fuzz/*.cpp"
    "${OPEN_SLEX_SOURCE_DIR}/fuzz/*.cxx"
    "${OPEN_SLEX_SOURCE_DIR}/fuzz/*.h"
    "${OPEN_SLEX_SOURCE_DIR}/fuzz/*.hh"
    "${OPEN_SLEX_SOURCE_DIR}/fuzz/*.hpp"
    "${OPEN_SLEX_SOURCE_DIR}/fuzz/*.hxx"
    "${OPEN_SLEX_SOURCE_DIR}/src/*.c"
    "${OPEN_SLEX_SOURCE_DIR}/src/*.cc"
    "${OPEN_SLEX_SOURCE_DIR}/src/*.cpp"
    "${OPEN_SLEX_SOURCE_DIR}/src/*.cxx"
    "${OPEN_SLEX_SOURCE_DIR}/src/*.h"
    "${OPEN_SLEX_SOURCE_DIR}/src/*.hh"
    "${OPEN_SLEX_SOURCE_DIR}/src/*.hpp"
    "${OPEN_SLEX_SOURCE_DIR}/src/*.hxx"
    "${OPEN_SLEX_SOURCE_DIR}/tests/*.c"
    "${OPEN_SLEX_SOURCE_DIR}/tests/*.cc"
    "${OPEN_SLEX_SOURCE_DIR}/tests/*.cpp"
    "${OPEN_SLEX_SOURCE_DIR}/tests/*.cxx"
    "${OPEN_SLEX_SOURCE_DIR}/tests/*.h"
    "${OPEN_SLEX_SOURCE_DIR}/tests/*.hh"
    "${OPEN_SLEX_SOURCE_DIR}/tests/*.hpp"
    "${OPEN_SLEX_SOURCE_DIR}/tests/*.hxx"
)
list(SORT OPEN_SLEX_FORMAT_SOURCES)

if(NOT OPEN_SLEX_FORMAT_SOURCES)
    message(FATAL_ERROR "No C or C++ source files were found to format")
endif()

if(OPEN_SLEX_CLANG_FORMAT_MODE STREQUAL "fix")
    set(OPEN_SLEX_CLANG_FORMAT_ARGUMENTS -i --style=file)
else()
    set(OPEN_SLEX_CLANG_FORMAT_ARGUMENTS --dry-run --Werror --style=file)
endif()

execute_process(
    COMMAND
        "${OPEN_SLEX_CLANG_FORMAT_EXECUTABLE}"
        ${OPEN_SLEX_CLANG_FORMAT_ARGUMENTS}
        ${OPEN_SLEX_FORMAT_SOURCES}
    WORKING_DIRECTORY "${OPEN_SLEX_SOURCE_DIR}"
    RESULT_VARIABLE OPEN_SLEX_CLANG_FORMAT_RESULT
)
if(NOT OPEN_SLEX_CLANG_FORMAT_RESULT EQUAL 0)
    if(OPEN_SLEX_CLANG_FORMAT_MODE STREQUAL "check")
        message(FATAL_ERROR
            "Formatting check failed. Run the CMake 'format' target and "
            "commit the resulting changes."
        )
    endif()
    message(FATAL_ERROR "clang-format failed")
endif()

list(LENGTH OPEN_SLEX_FORMAT_SOURCES OPEN_SLEX_FORMAT_SOURCE_COUNT)
message(STATUS
    "clang-format ${OPEN_SLEX_CLANG_FORMAT_MODE} completed for "
    "${OPEN_SLEX_FORMAT_SOURCE_COUNT} files"
)
