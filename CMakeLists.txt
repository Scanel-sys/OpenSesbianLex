cmake_minimum_required(VERSION 3.20)

project(OpenSesbianLex VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

# GNU tools are usually named bison/flex. WinFlexBison uses
# win_bison/win_flex, so accept both without hard-coded install paths.
find_program(BISON_EXECUTABLE NAMES bison win_bison DOC "Bison executable")
find_program(FLEX_EXECUTABLE NAMES flex win_flex DOC "Flex executable")

find_package(BISON 2.7 REQUIRED)
find_package(FLEX 2.6 REQUIRED)

set(OPEN_SLEX_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${OPEN_SLEX_GENERATED_DIR}")

BISON_TARGET(
    OpenSLexParser
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SLexParser.ypp"
    "${OPEN_SLEX_GENERATED_DIR}/SLexParser.tab.cpp"
    DEFINES_FILE "${OPEN_SLEX_GENERATED_DIR}/SLexParser.tab.hpp"
    COMPILE_FLAGS "-Wconflicts-sr -Wconflicts-rr"
)

get_filename_component(OPEN_SLEX_FLEX_NAME "${FLEX_EXECUTABLE}" NAME_WE)
string(TOLOWER "${OPEN_SLEX_FLEX_NAME}" OPEN_SLEX_FLEX_NAME)

if(OPEN_SLEX_FLEX_NAME STREQUAL "win_flex")
    FLEX_TARGET(
        OpenSLexLexer
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SLexer.l"
        "${OPEN_SLEX_GENERATED_DIR}/lex.yy.cpp"
        COMPILE_FLAGS "--wincompat"
    )
else()
    FLEX_TARGET(
        OpenSLexLexer
        "${CMAKE_CURRENT_SOURCE_DIR}/src/SLexer.l"
        "${OPEN_SLEX_GENERATED_DIR}/lex.yy.cpp"
    )
endif()

ADD_FLEX_BISON_DEPENDENCY(OpenSLexLexer OpenSLexParser)

add_executable(
    OpenSLex
    src/TextHandler.cpp
    src/TextHandler.hpp
    ${BISON_OpenSLexParser_OUTPUTS}
    ${FLEX_OpenSLexLexer_OUTPUTS}
)

target_compile_features(OpenSLex PRIVATE cxx_std_11)
target_include_directories(
    OpenSLex
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${OPEN_SLEX_GENERATED_DIR}"
)

if(MSVC)
    target_compile_options(OpenSLex PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(OpenSLex PRIVATE -Wall -Wextra)
endif()

if(UNIX)
    target_link_libraries(OpenSLex PRIVATE m)
endif()

install(TARGETS OpenSLex RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    function(add_parser_file_test test_name input_file expected_exit_code)
        set(extra_test_arguments)
        if(ARGC GREATER 3)
            list(APPEND extra_test_arguments
                "-DEXPECTED_STDERR_PATTERN=${ARGV3}"
            )
        endif()

        add_test(
            NAME "${test_name}"
            COMMAND
                "${CMAKE_COMMAND}"
                "-DPARSER=$<TARGET_FILE:OpenSLex>"
                "-DINPUT=${input_file}"
                "-DEXPECTED_EXIT_CODE=${expected_exit_code}"
                ${extra_test_arguments}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunParserTest.cmake"
        )
        set_tests_properties("${test_name}" PROPERTIES TIMEOUT 10)
    endfunction()

    function(add_parser_test test_name relative_input expected_exit_code)
        if(ARGC GREATER 3)
            add_parser_file_test(
                "${test_name}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${relative_input}"
                "${expected_exit_code}"
                "${ARGV3}"
            )
        else()
            add_parser_file_test(
                "${test_name}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${relative_input}"
                "${expected_exit_code}"
            )
        endif()
    endfunction()

    add_parser_test(c.valid.array docs/c_examples/TRUE_array_test.txt 0)
    add_parser_test(c.valid.directives docs/c_examples/TRUE_directives_test.txt 0)
    add_parser_test(c.valid.float docs/c_examples/TRUE_float_test.txt 0)
    add_parser_test(c.valid.ifdef docs/c_examples/TRUE_ifdef_test 0)
    add_parser_test(c.valid.macros docs/c_examples/TRUE_macros_test.txt 0)
    add_parser_test(c.valid.struct docs/c_examples/TRUE_struct_test 0)
    add_parser_test(c.valid.nested_struct docs/c_examples/TRUE_test_struct.txt 0)
    add_parser_test(c.valid.typedef docs/c_examples/TRUE_typedef_test 0)

    add_parser_test(c.invalid.if docs/c_examples/FALSE_if_test.txt 1)
    add_parser_test(c.invalid.float_dot docs/c_examples/FALSE_float_dot_test.txt 1)
    add_parser_test(c.invalid.float_exponent docs/c_examples/FALSE_float_exponent_test.txt 1)
    add_parser_test(c.invalid.hex_float_exponent docs/c_examples/FALSE_hex_float_exponent_test.txt 1)
    add_parser_test(c.invalid.program_1 docs/c_examples/FALSE_prog_1.txt 1)
    add_parser_test(c.invalid.program_2 docs/c_examples/FALSE_prog_2_test.txt 1)
    add_parser_test(c.invalid.program_3 docs/c_examples/FALSE_prog_3.txt 1)
    add_parser_test(c.invalid.pointer docs/c_examples/FALSE_test_ptr.txt 1)

    add_parser_test(opencl.valid.add_numbers docs/cl_examples/add_numbers.cl 0)
    add_parser_test(opencl.valid.image_filtering docs/cl_examples/image_filtering.cl 0)
    add_parser_test(opencl.valid.mandelbrot docs/cl_examples/mandelbrot.cl 0)
    add_parser_test(opencl.valid.matrix_multiplication docs/cl_examples/matrix_multiplication.cl 0)
    add_parser_test(opencl.valid.strings docs/cl_examples/string_literals.cl 0)
    add_parser_test(
        opencl.invalid.string_escape
        docs/cl_examples/FALSE_string_escape.cl
        1
        "invalid string literal"
    )
    add_parser_test(
        opencl.invalid.string_newline
        docs/cl_examples/FALSE_string_newline.cl
        1
        "invalid string literal"
    )

    set(OPEN_SLEX_GENERATED_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-inputs")
    file(MAKE_DIRECTORY "${OPEN_SLEX_GENERATED_TEST_DIR}")

    set(OPEN_SLEX_LINE_COMMENT_EOF
        "${OPEN_SLEX_GENERATED_TEST_DIR}/line-comment-at-eof.cl"
    )
    file(WRITE "${OPEN_SLEX_LINE_COMMENT_EOF}"
        "__kernel void comment_at_eof() {}\n// comment without final newline"
    )
    add_parser_file_test(
        opencl.valid.line_comment_at_eof
        "${OPEN_SLEX_LINE_COMMENT_EOF}"
        0
    )

    set(OPEN_SLEX_UNTERMINATED_STRING_EOF
        "${OPEN_SLEX_GENERATED_TEST_DIR}/unterminated-string-at-eof.cl"
    )
    file(WRITE "${OPEN_SLEX_UNTERMINATED_STRING_EOF}"
        "__kernel void string_at_eof() {\n    printf(\"unterminated"
    )
    add_parser_file_test(
        opencl.invalid.unterminated_string_at_eof
        "${OPEN_SLEX_UNTERMINATED_STRING_EOF}"
        1
        "invalid string literal"
    )

    set(OPEN_SLEX_UNTERMINATED_COMMENT_EOF
        "${OPEN_SLEX_GENERATED_TEST_DIR}/unterminated-comment-at-eof.cl"
    )
    file(WRITE "${OPEN_SLEX_UNTERMINATED_COMMENT_EOF}"
        "__kernel void comment_at_eof() {}\n/* unterminated"
    )
    add_parser_file_test(
        opencl.invalid.unterminated_comment_at_eof
        "${OPEN_SLEX_UNTERMINATED_COMMENT_EOF}"
        1
        "unterminated block comment"
    )

    add_test(
        NAME cli.missing_argument
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            -DEXPECTED_EXIT_CODE=2
            -DEXPECTED_STDERR_PATTERN=Usage:
            -DEXPECT_STDOUT_EMPTY=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunParserTest.cmake"
    )
    add_test(
        NAME cli.missing_file
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/__open_slex_missing_input__.c"
            -DINPUT_MUST_EXIST=OFF
            -DEXPECTED_EXIT_CODE=3
            "-DEXPECTED_STDERR_PATTERN=cannot open input file"
            -DEXPECT_STDOUT_EMPTY=ON
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunParserTest.cmake"
    )
    set_tests_properties(
        cli.missing_argument
        cli.missing_file
        PROPERTIES LABELS "cli;invalid" TIMEOUT 10
    )

    set_tests_properties(
        c.valid.array
        c.valid.directives
        c.valid.float
        c.valid.ifdef
        c.valid.macros
        c.valid.struct
        c.valid.nested_struct
        c.valid.typedef
        PROPERTIES LABELS "c;valid"
    )
    set_tests_properties(
        c.invalid.if
        c.invalid.float_dot
        c.invalid.float_exponent
        c.invalid.hex_float_exponent
        c.invalid.program_1
        c.invalid.program_2
        c.invalid.program_3
        c.invalid.pointer
        PROPERTIES LABELS "c;invalid"
    )
    set_tests_properties(
        opencl.valid.add_numbers
        opencl.valid.image_filtering
        opencl.valid.mandelbrot
        opencl.valid.matrix_multiplication
        opencl.valid.strings
        opencl.valid.line_comment_at_eof
        PROPERTIES LABELS "opencl;valid"
    )
    set_tests_properties(
        opencl.invalid.string_escape
        opencl.invalid.string_newline
        opencl.invalid.unterminated_string_at_eof
        opencl.invalid.unterminated_comment_at_eof
        PROPERTIES LABELS "opencl;invalid"
    )
endif()
