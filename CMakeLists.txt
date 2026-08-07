cmake_minimum_required(VERSION 3.20)

project(OpenSesbianLex VERSION 1.0.0 LANGUAGES CXX)

include(CTest)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(
    OPEN_SLEX_ENABLE_OPENCL_RUNTIME_TESTS
    "Build tests that execute original and obfuscated kernels via OpenCL"
    OFF
)
option(
    OPEN_SLEX_ENABLE_SANITIZERS
    "Enable AddressSanitizer and UndefinedBehaviorSanitizer for host code"
    OFF
)
option(
    OPEN_SLEX_BUILD_FUZZER
    "Build the Clang libFuzzer target for the lexer and parser"
    OFF
)
set(
    OPEN_SLEX_OPENCL_RUNTIME_LAUNCHER
    ""
    CACHE STRING
    "Optional command used to launch OpenCL runtime tests (for example oclgrind)"
)
set(
    OPEN_SLEX_FRONTEND
    "AUTO"
    CACHE STRING
    "Semantic frontend: AUTO, CLANG, or LEGACY"
)
set_property(
    CACHE OPEN_SLEX_FRONTEND
    PROPERTY STRINGS AUTO CLANG LEGACY
)
string(TOUPPER "${OPEN_SLEX_FRONTEND}" OPEN_SLEX_FRONTEND)
if(NOT OPEN_SLEX_FRONTEND MATCHES "^(AUTO|CLANG|LEGACY)$")
    message(FATAL_ERROR
        "OPEN_SLEX_FRONTEND must be AUTO, CLANG, or LEGACY"
    )
endif()

set(OPEN_SLEX_HAS_CLANG_FRONTEND OFF)
if(NOT OPEN_SLEX_FRONTEND STREQUAL "LEGACY")
    find_package(LibClang QUIET)
    if(LibClang_FOUND)
        set(OPEN_SLEX_HAS_CLANG_FRONTEND ON)
        message(STATUS "OpenSLex semantic frontend: Clang/libclang")
    elseif(OPEN_SLEX_FRONTEND STREQUAL "CLANG")
        message(FATAL_ERROR
            "OPEN_SLEX_FRONTEND=CLANG requires libclang and clang-c/Index.h"
        )
    else()
        message(STATUS
            "OpenSLex semantic frontend: legacy (libclang was not found)"
        )
    endif()
else()
    message(STATUS "OpenSLex semantic frontend: legacy (explicitly selected)")
endif()

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
    src/AtomicFileWriter.cpp
    src/AtomicFileWriter.hpp
    src/ClangFrontend.cpp
    src/ClangFrontend.hpp
    src/IdentifierResolver.cpp
    src/IdentifierResolver.hpp
    src/TextHandler.cpp
    src/TextHandler.hpp
    ${BISON_OpenSLexParser_OUTPUTS}
    ${FLEX_OpenSLexLexer_OUTPUTS}
)

target_compile_features(OpenSLex PRIVATE cxx_std_14)
target_include_directories(
    OpenSLex
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${OPEN_SLEX_GENERATED_DIR}"
)

if(OPEN_SLEX_HAS_CLANG_FRONTEND)
    target_compile_definitions(
        OpenSLex
        PRIVATE
            OPEN_SLEX_HAS_CLANG_FRONTEND=1
            "OPEN_SLEX_CLANG_RESOURCE_DIR=\"${LibClang_RESOURCE_DIR}\""
    )
    target_link_libraries(OpenSLex PRIVATE LibClang::LibClang)
    if(WIN32 AND LibClang_RUNTIME_LIBRARY)
        add_custom_command(
            TARGET OpenSLex
            POST_BUILD
            COMMAND
                "${CMAKE_COMMAND}" -E copy_if_different
                "${LibClang_RUNTIME_LIBRARY}"
                "$<TARGET_FILE_DIR:OpenSLex>"
            COMMENT "Copying libclang runtime next to OpenSLex"
            VERBATIM
        )
    endif()
endif()

function(open_slex_enable_sanitizers target_name)
    if(NOT OPEN_SLEX_ENABLE_SANITIZERS)
        return()
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "OPEN_SLEX_ENABLE_SANITIZERS requires GCC or Clang"
        )
    endif()
    target_compile_options(
        "${target_name}"
        PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(
        "${target_name}"
        PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
    )
endfunction()

open_slex_enable_sanitizers(OpenSLex)

if(MSVC)
    target_compile_options(OpenSLex PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(OpenSLex PRIVATE -Wall -Wextra)
endif()

if(UNIX)
    target_link_libraries(OpenSLex PRIVATE m)
endif()

if(OPEN_SLEX_BUILD_FUZZER)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "OPEN_SLEX_BUILD_FUZZER requires Clang")
    endif()

    add_executable(
        OpenSLexFuzzer
        fuzz/FuzzOpenSLex.cpp
        src/IdentifierResolver.cpp
        src/IdentifierResolver.hpp
        src/TextHandler.cpp
        src/TextHandler.hpp
        ${BISON_OpenSLexParser_OUTPUTS}
        ${FLEX_OpenSLexLexer_OUTPUTS}
    )
    target_compile_features(OpenSLexFuzzer PRIVATE cxx_std_14)
    target_compile_definitions(OpenSLexFuzzer PRIVATE OPEN_SLEX_NO_MAIN=1)
    target_include_directories(
        OpenSLexFuzzer
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${OPEN_SLEX_GENERATED_DIR}"
    )
    target_compile_options(
        OpenSLexFuzzer
        PRIVATE
            -Wall
            -Wextra
            -fsanitize=fuzzer,address,undefined
            -fno-omit-frame-pointer
    )
    target_link_options(
        OpenSLexFuzzer
        PRIVATE
            -fsanitize=fuzzer,address,undefined
            -fno-omit-frame-pointer
    )
    if(UNIX)
        target_link_libraries(OpenSLexFuzzer PRIVATE m)
    endif()
endif()

install(TARGETS OpenSLex RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    set(OPEN_SLEX_BEHAVIOR_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/behavior_program.cpp"
    )
    set(OPEN_SLEX_BEHAVIOR_OBFUSCATED_SOURCE
        "${CMAKE_CURRENT_BINARY_DIR}/test-inputs/behavior_program_obfuscated.cpp"
    )
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-inputs")

    add_custom_command(
        OUTPUT "${OPEN_SLEX_BEHAVIOR_OBFUSCATED_SOURCE}"
        COMMAND
            "$<TARGET_FILE:OpenSLex>"
            "${OPEN_SLEX_BEHAVIOR_SOURCE}"
            "${OPEN_SLEX_BEHAVIOR_OBFUSCATED_SOURCE}"
        DEPENDS OpenSLex "${OPEN_SLEX_BEHAVIOR_SOURCE}"
        COMMENT "Obfuscating the behavior-equivalence test program"
        VERBATIM
    )

    add_executable(
        OpenSLexBehaviorOriginal
        "${OPEN_SLEX_BEHAVIOR_SOURCE}"
    )
    add_executable(
        OpenSLexBehaviorObfuscated
        "${OPEN_SLEX_BEHAVIOR_OBFUSCATED_SOURCE}"
    )
    target_compile_features(OpenSLexBehaviorOriginal PRIVATE cxx_std_14)
    target_compile_features(OpenSLexBehaviorObfuscated PRIVATE cxx_std_14)
    open_slex_enable_sanitizers(OpenSLexBehaviorOriginal)
    open_slex_enable_sanitizers(OpenSLexBehaviorObfuscated)

    if(MSVC)
        target_compile_options(OpenSLexBehaviorObfuscated PRIVATE /Zc:trigraphs)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(OpenSLexBehaviorObfuscated PRIVATE -trigraphs)
    endif()

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
    add_parser_test(opencl.valid.semantic_features tests/opencl_semantic_kernel.cl 0)
    add_parser_test(opencl.valid.comment_boundaries tests/opencl_comment_boundaries.cl 0)
    add_parser_test(opencl.valid.preprocessor tests/opencl_preprocessor.cl 0)
    add_parser_test(opencl.valid.language_coverage tests/opencl_language_coverage.cl 0)
    if(OPEN_SLEX_HAS_CLANG_FRONTEND)
        add_parser_test(
            opencl.valid.clang_semantics
            tests/opencl_clang_semantics.cl
            0
        )
        add_parser_test(
            opencl.invalid.clang_undeclared_identifier
            tests/FALSE_opencl_undeclared_identifier.cl
            1
            "undeclared identifier"
        )
    endif()
    add_parser_test(
        opencl.invalid.preprocessor_unmatched
        tests/FALSE_preprocessor_unmatched.cl
        1
        "(syntax error|#else without #if)"
    )
    add_parser_test(
        opencl.invalid.preprocessor_unterminated
        tests/FALSE_preprocessor_unterminated.cl
        1
        "(syntax error|unterminated conditional directive)"
    )
    add_parser_test(
        opencl.invalid.string_escape
        docs/cl_examples/FALSE_string_escape.cl
        1
        "(invalid string literal|unknown escape sequence)"
    )
    add_parser_test(
        opencl.invalid.string_newline
        docs/cl_examples/FALSE_string_newline.cl
        1
        "(invalid string literal|expected expression|missing terminating)"
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
        "(invalid string literal|expected expression|missing terminating)"
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
        "(unterminated block comment|unterminated .+ comment)"
    )

    set(OPEN_SLEX_OBFUSCATION_INPUT
        "${OPEN_SLEX_GENERATED_TEST_DIR}/obfuscation-smoke.cl"
    )
    set(OPEN_SLEX_OBFUSCATION_OUTPUT
        "${OPEN_SLEX_GENERATED_TEST_DIR}/obfuscation-smoke-output.cl"
    )
    file(WRITE "${OPEN_SLEX_OBFUSCATION_INPUT}"
        "__kernel void keep_kernel(__global int* data) {\n"
        "    int local_value = 1;\n"
        "    printf(\"local_value\");\n"
        "    if (local_value) { data[0] = local_value; }\n"
        "}\n"
    )
    add_test(
        NAME opencl.obfuscation.smoke
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${OPEN_SLEX_OBFUSCATION_INPUT}"
            "-DOUTPUT=${OPEN_SLEX_OBFUSCATION_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunObfuscationTest.cmake"
    )
    set_tests_properties(
        opencl.obfuscation.smoke
        PROPERTIES LABELS "opencl;obfuscation" TIMEOUT 10
    )

    set(OPEN_SLEX_OPENCL_FEATURE_OUTPUT
        "${OPEN_SLEX_GENERATED_TEST_DIR}/opencl-features-obfuscated.cl"
    )
    add_test(
        NAME opencl.obfuscation.language_features
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_semantic_kernel.cl"
            "-DOUTPUT=${OPEN_SLEX_OPENCL_FEATURE_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOpenCLFeatureTest.cmake"
    )
    set_tests_properties(
        opencl.obfuscation.language_features
        PROPERTIES LABELS "opencl;obfuscation;semantic" TIMEOUT 10
    )

    set(OPEN_SLEX_SYMBOL_SCOPE_OUTPUT
        "${OPEN_SLEX_GENERATED_TEST_DIR}/symbol-scopes-obfuscated.cl"
    )
    add_test(
        NAME opencl.obfuscation.symbol_scopes
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_symbol_scopes.cl"
            "-DOUTPUT=${OPEN_SLEX_SYMBOL_SCOPE_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunSymbolScopeTest.cmake"
    )
    set_tests_properties(
        opencl.obfuscation.symbol_scopes
        PROPERTIES LABELS "opencl;obfuscation;semantic" TIMEOUT 10
    )

    set(OPEN_SLEX_OPAQUE_PREDICATE_OUTPUT
        "${OPEN_SLEX_GENERATED_TEST_DIR}/opaque-predicate-obfuscated.cl"
    )
    add_test(
        NAME opencl.obfuscation.opaque_predicate
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_opaque_predicate.cl"
            "-DOUTPUT=${OPEN_SLEX_OPAQUE_PREDICATE_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOpaquePredicateTest.cmake"
    )
    set_tests_properties(
        opencl.obfuscation.opaque_predicate
        PROPERTIES LABELS "opencl;obfuscation;semantic" TIMEOUT 10
    )

    set(OPEN_SLEX_PREPROCESSOR_OUTPUT
        "${OPEN_SLEX_GENERATED_TEST_DIR}/preprocessor-obfuscated.cl"
    )
    add_test(
        NAME opencl.obfuscation.preprocessor
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_preprocessor.cl"
            "-DOUTPUT=${OPEN_SLEX_PREPROCESSOR_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPreprocessorTest.cmake"
    )
    set_tests_properties(
        opencl.obfuscation.preprocessor
        PROPERTIES LABELS "opencl;obfuscation;preprocessor" TIMEOUT 10
    )

    set(OPEN_SLEX_LANGUAGE_COVERAGE_OUTPUT
        "${OPEN_SLEX_GENERATED_TEST_DIR}/language-coverage-obfuscated.cl"
    )
    add_test(
        NAME opencl.obfuscation.language_coverage
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_language_coverage.cl"
            "-DOUTPUT=${OPEN_SLEX_LANGUAGE_COVERAGE_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunLanguageCoverageTest.cmake"
    )
    set_tests_properties(
        opencl.obfuscation.language_coverage
        PROPERTIES LABELS "opencl;obfuscation;language" TIMEOUT 10
    )

    add_test(
        NAME opencl.obfuscation.seeds
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_language_coverage.cl"
            "-DWORK_DIR=${OPEN_SLEX_GENERATED_TEST_DIR}/seeds"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunSeedTest.cmake"
    )
    set_tests_properties(
        opencl.obfuscation.seeds
        PROPERTIES LABELS "opencl;obfuscation;seed" TIMEOUT 20
    )

    set(OPEN_SLEX_COMMENT_BOUNDARY_OUTPUT
        "${OPEN_SLEX_GENERATED_TEST_DIR}/comment-boundaries-obfuscated.cl"
    )
    add_test(
        NAME opencl.obfuscation.comment_boundaries
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_comment_boundaries.cl"
            "-DOUTPUT=${OPEN_SLEX_COMMENT_BOUNDARY_OUTPUT}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCommentBoundaryTest.cmake"
    )
    set_tests_properties(
        opencl.obfuscation.comment_boundaries
        PROPERTIES LABELS "opencl;obfuscation;comments" TIMEOUT 10
    )

    if(OPEN_SLEX_HAS_CLANG_FRONTEND)
        set(OPEN_SLEX_CLANG_SEMANTIC_OUTPUT
            "${OPEN_SLEX_GENERATED_TEST_DIR}/clang-semantics-obfuscated.cl"
        )
        add_test(
            NAME opencl.obfuscation.clang_semantics
            COMMAND
                "${CMAKE_COMMAND}"
                "-DPARSER=$<TARGET_FILE:OpenSLex>"
                "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_clang_semantics.cl"
                "-DOUTPUT=${OPEN_SLEX_CLANG_SEMANTIC_OUTPUT}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunClangSemanticTest.cmake"
        )
        set_tests_properties(
            opencl.obfuscation.clang_semantics
            PROPERTIES LABELS "opencl;obfuscation;clang;semantic" TIMEOUT 20
        )
    endif()

    add_test(
        NAME obfuscation.behavior_equivalence
        COMMAND
            "${CMAKE_COMMAND}"
            "-DORIGINAL_PROGRAM=$<TARGET_FILE:OpenSLexBehaviorOriginal>"
            "-DOBFUSCATED_PROGRAM=$<TARGET_FILE:OpenSLexBehaviorObfuscated>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompareProgramBehavior.cmake"
    )
    set_tests_properties(
        obfuscation.behavior_equivalence
        PROPERTIES LABELS "obfuscation;semantic" TIMEOUT 10
    )

    if(OPEN_SLEX_ENABLE_OPENCL_RUNTIME_TESTS)
        find_package(OpenCL REQUIRED)

        add_executable(
            OpenSLexOpenCLSemanticRunner
            tests/OpenCLSemanticRunner.cpp
        )
        target_compile_features(OpenSLexOpenCLSemanticRunner PRIVATE cxx_std_11)
        target_link_libraries(OpenSLexOpenCLSemanticRunner PRIVATE OpenCL::OpenCL)
        open_slex_enable_sanitizers(OpenSLexOpenCLSemanticRunner)
        if(MSVC)
            target_compile_options(OpenSLexOpenCLSemanticRunner PRIVATE /W4)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(
                OpenSLexOpenCLSemanticRunner
                PRIVATE -Wall -Wextra
            )
        endif()

        set(OPEN_SLEX_OPENCL_SEMANTIC_OUTPUT
            "${OPEN_SLEX_GENERATED_TEST_DIR}/opencl-semantic-obfuscated.cl"
        )
        set(opencl_runtime_launcher_argument)
        if(NOT OPEN_SLEX_OPENCL_RUNTIME_LAUNCHER STREQUAL "")
            list(APPEND opencl_runtime_launcher_argument
                "-DRUNTIME_LAUNCHER=${OPEN_SLEX_OPENCL_RUNTIME_LAUNCHER}"
            )
        endif()
        add_test(
            NAME opencl.obfuscation.runtime_equivalence
            COMMAND
                "${CMAKE_COMMAND}"
                "-DPARSER=$<TARGET_FILE:OpenSLex>"
                "-DRUNNER=$<TARGET_FILE:OpenSLexOpenCLSemanticRunner>"
                "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_semantic_kernel.cl"
                "-DOUTPUT=${OPEN_SLEX_OPENCL_SEMANTIC_OUTPUT}"
                ${opencl_runtime_launcher_argument}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOpenCLSemanticTest.cmake"
        )
        set_tests_properties(
            opencl.obfuscation.runtime_equivalence
            PROPERTIES LABELS "opencl;obfuscation;semantic" TIMEOUT 60
        )

        file(GLOB OPEN_SLEX_OPENCL_EXAMPLES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/docs/cl_examples/*.cl"
        )
        list(APPEND OPEN_SLEX_OPENCL_EXAMPLES
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_comment_boundaries.cl"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_language_coverage.cl"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_opaque_predicate.cl"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_preprocessor.cl"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/opencl_symbol_scopes.cl"
        )
        foreach(opencl_example IN LISTS OPEN_SLEX_OPENCL_EXAMPLES)
            get_filename_component(opencl_example_name "${opencl_example}" NAME)
            if(opencl_example_name MATCHES "^FALSE_")
                continue()
            endif()

            get_filename_component(opencl_example_stem "${opencl_example}" NAME_WE)
            string(MAKE_C_IDENTIFIER
                "${opencl_example_stem}" opencl_example_test_suffix
            )
            set(opencl_example_output
                "${OPEN_SLEX_GENERATED_TEST_DIR}/${opencl_example_stem}-obfuscated.cl"
            )
            add_test(
                NAME "opencl.obfuscation.build.${opencl_example_test_suffix}"
                COMMAND
                    "${CMAKE_COMMAND}"
                    "-DPARSER=$<TARGET_FILE:OpenSLex>"
                    "-DRUNNER=$<TARGET_FILE:OpenSLexOpenCLSemanticRunner>"
                    "-DINPUT=${opencl_example}"
                    "-DOUTPUT=${opencl_example_output}"
                    ${opencl_runtime_launcher_argument}
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunOpenCLBuildTest.cmake"
            )
            set_tests_properties(
                "opencl.obfuscation.build.${opencl_example_test_suffix}"
                PROPERTIES
                    LABELS "opencl;obfuscation;build"
                    TIMEOUT 60
            )
        endforeach()
    endif()

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
    add_test(
        NAME cli.safe_output
        COMMAND
            "${CMAKE_COMMAND}"
            "-DPARSER=$<TARGET_FILE:OpenSLex>"
            "-DWORK_DIR=${OPEN_SLEX_GENERATED_TEST_DIR}/safe-output"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunSafeOutputTest.cmake"
    )
    set_tests_properties(
        cli.missing_argument
        cli.missing_file
        cli.safe_output
        PROPERTIES LABELS "cli;invalid" TIMEOUT 10
    )

    if(OPEN_SLEX_BUILD_FUZZER)
        add_test(
            NAME fuzz.frontend.smoke
            COMMAND
                "$<TARGET_FILE:OpenSLexFuzzer>"
                -runs=2000
                "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus"
        )
        set_tests_properties(
            fuzz.frontend.smoke
            PROPERTIES LABELS "fuzz;lexer;parser" TIMEOUT 60
        )
    endif()

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
        opencl.valid.semantic_features
        opencl.valid.comment_boundaries
        opencl.valid.preprocessor
        opencl.valid.language_coverage
        opencl.valid.line_comment_at_eof
        PROPERTIES LABELS "opencl;valid"
    )
    if(OPEN_SLEX_HAS_CLANG_FRONTEND)
        set_tests_properties(
            opencl.valid.clang_semantics
            PROPERTIES LABELS "opencl;valid;clang;semantic"
        )
    endif()
    set_tests_properties(
        opencl.invalid.string_escape
        opencl.invalid.string_newline
        opencl.invalid.unterminated_string_at_eof
        opencl.invalid.unterminated_comment_at_eof
        opencl.invalid.preprocessor_unmatched
        opencl.invalid.preprocessor_unterminated
        PROPERTIES LABELS "opencl;invalid"
    )
    if(OPEN_SLEX_HAS_CLANG_FRONTEND)
        set_tests_properties(
            opencl.invalid.clang_undeclared_identifier
            PROPERTIES LABELS "opencl;invalid;clang;semantic"
        )
    endif()
endif()
