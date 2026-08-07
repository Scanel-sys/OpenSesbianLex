foreach(required_variable PARSER EXPECTED_EXIT_CODE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT EXISTS "${PARSER}")
    message(FATAL_ERROR "Parser executable does not exist: ${PARSER}")
endif()

set(parser_command "${PARSER}")
set(test_subject "parser without arguments")

if(DEFINED INPUT)
    if(NOT DEFINED INPUT_MUST_EXIST)
        set(INPUT_MUST_EXIST ON)
    endif()

    if(INPUT_MUST_EXIST AND NOT EXISTS "${INPUT}")
        message(FATAL_ERROR "Test input does not exist: ${INPUT}")
    endif()

    list(APPEND parser_command "${INPUT}")
    set(test_subject "${INPUT}")
endif()

execute_process(
    COMMAND ${parser_command}
    RESULT_VARIABLE actual_exit_code
    OUTPUT_VARIABLE parser_stdout
    ERROR_VARIABLE parser_stderr
)

if(NOT "${actual_exit_code}" MATCHES "^-?[0-9]+$")
    message(
        FATAL_ERROR
        "Parser could not be executed (${actual_exit_code}).\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

if(NOT actual_exit_code EQUAL EXPECTED_EXIT_CODE)
    message(
        FATAL_ERROR
        "Unexpected parser exit code for ${test_subject}: "
        "expected ${EXPECTED_EXIT_CODE}, got ${actual_exit_code}.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

if(EXPECTED_EXIT_CODE EQUAL 0 AND NOT parser_stdout MATCHES "(^|[\r\n])PASS([\r\n]|$)")
    message(
        FATAL_ERROR
        "Parser returned success without the PASS marker for ${test_subject}.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

if(DEFINED EXPECTED_STDERR_PATTERN AND
   NOT parser_stderr MATCHES "${EXPECTED_STDERR_PATTERN}")
    message(
        FATAL_ERROR
        "Parser stderr did not match '${EXPECTED_STDERR_PATTERN}' for ${test_subject}.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

if(EXPECT_STDOUT_EMPTY AND NOT parser_stdout STREQUAL "")
    message(
        FATAL_ERROR
        "Parser unexpectedly wrote to stdout for ${test_subject}.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

message(STATUS "Parser returned the expected result for ${test_subject}")
