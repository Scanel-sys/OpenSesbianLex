foreach(required_variable PARSER INPUT OUTPUT WORK_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(reparsed "${WORK_DIR}/token-boundaries-reparsed.cl")
set(in_place "${WORK_DIR}/token-boundaries-in-place.cl")
set(in_place_reparsed "${WORK_DIR}/token-boundaries-in-place-reparsed.cl")
file(REMOVE "${OUTPUT}" "${reparsed}" "${in_place}" "${in_place_reparsed}")

execute_process(
    COMMAND "${PARSER}" "${INPUT}" "${OUTPUT}"
    RESULT_VARIABLE obfuscation_result
    OUTPUT_VARIABLE obfuscation_stdout
    ERROR_VARIABLE obfuscation_stderr
)
if(NOT obfuscation_result EQUAL 0)
    message(
        FATAL_ERROR
        "Token-boundary obfuscation failed with ${obfuscation_result}.\n"
        "stdout:\n${obfuscation_stdout}\n"
        "stderr:\n${obfuscation_stderr}"
    )
endif()

file(READ "${OUTPUT}" obfuscated_source)
foreach(required_pattern "[+][ \t]+[+][+]" "[-][ \t]+[-][-]" "[/][ \t]+[*]")
    if(NOT obfuscated_source MATCHES "${required_pattern}")
        message(
            FATAL_ERROR
            "The obfuscated source lost a required token boundary matching "
            "'${required_pattern}'.\n${obfuscated_source}"
        )
    endif()
endforeach()

execute_process(
    COMMAND "${PARSER}" "${OUTPUT}" "${reparsed}"
    RESULT_VARIABLE reparse_result
    OUTPUT_VARIABLE reparse_stdout
    ERROR_VARIABLE reparse_stderr
)
if(NOT reparse_result EQUAL 0)
    message(
        FATAL_ERROR
        "The token-boundary output is not valid OpenCL.\n"
        "stdout:\n${reparse_stdout}\n"
        "stderr:\n${reparse_stderr}"
    )
endif()

configure_file("${INPUT}" "${in_place}" COPYONLY)
file(READ "${in_place}" original_in_place_source)
execute_process(
    COMMAND "${PARSER}" "${in_place}" "${in_place}"
    RESULT_VARIABLE in_place_result
    OUTPUT_VARIABLE in_place_stdout
    ERROR_VARIABLE in_place_stderr
)
if(NOT in_place_result EQUAL 0)
    file(READ "${in_place}" preserved_in_place_source)
    if(NOT preserved_in_place_source STREQUAL original_in_place_source)
        message(FATAL_ERROR "Failed final validation modified the in-place input")
    endif()
    message(
        FATAL_ERROR
        "In-place token-boundary obfuscation failed with ${in_place_result}.\n"
        "stdout:\n${in_place_stdout}\n"
        "stderr:\n${in_place_stderr}"
    )
endif()

execute_process(
    COMMAND "${PARSER}" "${in_place}" "${in_place_reparsed}"
    RESULT_VARIABLE in_place_reparse_result
    OUTPUT_VARIABLE in_place_reparse_stdout
    ERROR_VARIABLE in_place_reparse_stderr
)
if(NOT in_place_reparse_result EQUAL 0)
    message(
        FATAL_ERROR
        "In-place output is not valid OpenCL.\n"
        "stdout:\n${in_place_reparse_stdout}\n"
        "stderr:\n${in_place_reparse_stderr}"
    )
endif()
