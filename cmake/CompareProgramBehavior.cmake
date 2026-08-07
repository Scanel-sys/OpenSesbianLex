foreach(required_variable ORIGINAL_PROGRAM OBFUSCATED_PROGRAM)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${ORIGINAL_PROGRAM}"
    RESULT_VARIABLE original_result
    OUTPUT_VARIABLE original_stdout
    ERROR_VARIABLE original_stderr
)

execute_process(
    COMMAND "${OBFUSCATED_PROGRAM}"
    RESULT_VARIABLE obfuscated_result
    OUTPUT_VARIABLE obfuscated_stdout
    ERROR_VARIABLE obfuscated_stderr
)

if(NOT original_result MATCHES "^-?[0-9]+$" OR
   NOT obfuscated_result MATCHES "^-?[0-9]+$")
    message(
        FATAL_ERROR
        "A behavior test program could not be executed.\n"
        "original: ${original_result}\n"
        "obfuscated: ${obfuscated_result}"
    )
endif()

if(NOT original_result EQUAL 32)
    message(FATAL_ERROR "The original program returned ${original_result}, expected 32")
endif()

if(NOT obfuscated_result EQUAL original_result)
    message(
        FATAL_ERROR
        "Behavior changed after obfuscation.\n"
        "original exit code: ${original_result}\n"
        "obfuscated exit code: ${obfuscated_result}\n"
        "original stdout: ${original_stdout}\n"
        "obfuscated stdout: ${obfuscated_stdout}\n"
        "original stderr: ${original_stderr}\n"
        "obfuscated stderr: ${obfuscated_stderr}"
    )
endif()
