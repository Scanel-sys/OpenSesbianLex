foreach(required_variable PARSER RUNNER INPUT OUTPUT)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(REMOVE "${OUTPUT}")

execute_process(
    COMMAND "${PARSER}" "${INPUT}" "${OUTPUT}"
    RESULT_VARIABLE parser_result
    OUTPUT_VARIABLE parser_stdout
    ERROR_VARIABLE parser_stderr
)

if(NOT parser_result EQUAL 0)
    message(
        FATAL_ERROR
        "Could not obfuscate ${INPUT}.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

set(runner_command)
if(DEFINED RUNTIME_LAUNCHER AND NOT RUNTIME_LAUNCHER STREQUAL "")
    list(APPEND runner_command "${RUNTIME_LAUNCHER}")
endif()
list(APPEND runner_command "${RUNNER}" --build)

foreach(source_file "${INPUT}" "${OUTPUT}")
    execute_process(
        COMMAND ${runner_command} "${source_file}"
        RESULT_VARIABLE runner_result
        OUTPUT_VARIABLE runner_stdout
        ERROR_VARIABLE runner_stderr
    )

    if(NOT runner_result EQUAL 0)
        message(
            FATAL_ERROR
            "OpenCL could not build ${source_file}.\n"
            "stdout:\n${runner_stdout}\n"
            "stderr:\n${runner_stderr}"
        )
    endif()
endforeach()
