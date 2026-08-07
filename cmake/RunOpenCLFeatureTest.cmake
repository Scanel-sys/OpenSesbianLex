foreach(required_variable PARSER INPUT OUTPUT)
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
        "Could not obfuscate the OpenCL language feature fixture.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

file(READ "${OUTPUT}" obfuscated_source)

foreach(required_text
    "semantic_kernel"
    "ENABLE_FEATURE_PATH"
    "SCALE_VALUE"
    "CLK_LOCAL_MEM_FENCE"
    ".xy"
    ".s0"
    "->"
)
    string(FIND "${obfuscated_source}" "${required_text}" text_position)
    if(text_position EQUAL -1)
        message(FATAL_ERROR "Obfuscated OpenCL lost '${required_text}'")
    endif()
endforeach()

foreach(local_identifier
    "pair_pointer"
    "outer_value"
    "local_index"
    "transformed"
    "iteration"
)
    string(FIND "${obfuscated_source}" "${local_identifier}" name_position)
    if(NOT name_position EQUAL -1)
        message(
            FATAL_ERROR
            "Local identifier '${local_identifier}' was not obfuscated"
        )
    endif()
endforeach()
