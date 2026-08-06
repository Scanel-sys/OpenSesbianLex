foreach(required_variable PARSER INPUT EXPECTED_EXIT_CODE)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

if(NOT EXISTS "${PARSER}")
    message(FATAL_ERROR "Parser executable does not exist: ${PARSER}")
endif()

if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Test input does not exist: ${INPUT}")
endif()

execute_process(
    COMMAND "${PARSER}" "${INPUT}"
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
        "Unexpected parser exit code for ${INPUT}: "
        "expected ${EXPECTED_EXIT_CODE}, got ${actual_exit_code}.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

if(EXPECTED_EXIT_CODE EQUAL 0 AND NOT parser_stdout MATCHES "(^|[\r\n])PASS([\r\n]|$)")
    message(
        FATAL_ERROR
        "Parser returned success without the PASS marker for ${INPUT}.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

message(STATUS "Parser returned the expected exit code for ${INPUT}")
