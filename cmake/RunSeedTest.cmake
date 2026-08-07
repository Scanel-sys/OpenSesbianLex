foreach(required_variable PARSER INPUT WORK_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")

set(seeds 1 305419896 4294967295)
set(output_hashes)

foreach(seed IN LISTS seeds)
    set(output "${WORK_DIR}/seed-${seed}.cl")
    set(reparsed "${WORK_DIR}/seed-${seed}-reparsed.cl")
    file(REMOVE "${output}" "${reparsed}")

    execute_process(
        COMMAND "${PARSER}" --seed "${seed}" "${INPUT}" "${output}"
        RESULT_VARIABLE parser_result
        OUTPUT_VARIABLE parser_stdout
        ERROR_VARIABLE parser_stderr
    )
    if(NOT parser_result EQUAL 0)
        message(
            FATAL_ERROR
            "Seed ${seed} failed with ${parser_result}.\n"
            "stdout:\n${parser_stdout}\n"
            "stderr:\n${parser_stderr}"
        )
    endif()

    execute_process(
        COMMAND "${PARSER}" "${output}" "${reparsed}"
        RESULT_VARIABLE reparse_result
        OUTPUT_VARIABLE reparse_stdout
        ERROR_VARIABLE reparse_stderr
    )
    if(NOT reparse_result EQUAL 0)
        message(
            FATAL_ERROR
            "Output produced with seed ${seed} is not parseable.\n"
            "stdout:\n${reparse_stdout}\n"
            "stderr:\n${reparse_stderr}"
        )
    endif()

    file(SHA256 "${output}" output_hash)
    list(APPEND output_hashes "${output_hash}")
endforeach()

list(REMOVE_DUPLICATES output_hashes)
list(LENGTH output_hashes distinct_output_count)
list(LENGTH seeds seed_count)
if(NOT distinct_output_count EQUAL seed_count)
    message(FATAL_ERROR "Different seeds did not produce distinct outputs")
endif()

set(repeat_output "${WORK_DIR}/seed-1-repeat.cl")
execute_process(
    COMMAND "${PARSER}" --seed 1 "${INPUT}" "${repeat_output}"
    RESULT_VARIABLE repeat_result
    OUTPUT_VARIABLE repeat_stdout
    ERROR_VARIABLE repeat_stderr
)
if(NOT repeat_result EQUAL 0)
    message(FATAL_ERROR "Repeated seed 1 run failed: ${repeat_stderr}")
endif()

file(SHA256 "${WORK_DIR}/seed-1.cl" first_hash)
file(SHA256 "${repeat_output}" repeat_hash)
if(NOT first_hash STREQUAL repeat_hash)
    message(FATAL_ERROR "The same seed did not reproduce the same output")
endif()

foreach(invalid_seed "not-a-number" "4294967296" "-1")
    execute_process(
        COMMAND "${PARSER}" --seed "${invalid_seed}" "${INPUT}"
        RESULT_VARIABLE invalid_result
        OUTPUT_VARIABLE invalid_stdout
        ERROR_VARIABLE invalid_stderr
    )
    if(NOT invalid_result EQUAL 2)
        message(
            FATAL_ERROR
            "Invalid seed '${invalid_seed}' returned ${invalid_result}, "
            "expected usage error 2"
        )
    endif()
    if(NOT invalid_stderr MATCHES "unsigned 32-bit value")
        message(
            FATAL_ERROR
            "Invalid seed '${invalid_seed}' did not produce a clear error"
        )
    endif()
endforeach()
