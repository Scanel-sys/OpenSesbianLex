foreach(required_variable PARSER WORK_DIR)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${WORK_DIR}")

set(in_place_file "${WORK_DIR}/in-place.cl")
file(WRITE "${in_place_file}"
    "__kernel void in_place(__global int* data) {\n"
    "    int local_value = 1;\n"
    "    data[0] = local_value;\n"
    "}\n"
)

execute_process(
    COMMAND "${PARSER}" "${in_place_file}" "${in_place_file}"
    RESULT_VARIABLE in_place_result
    OUTPUT_VARIABLE in_place_stdout
    ERROR_VARIABLE in_place_stderr
)

if(NOT in_place_result EQUAL 0)
    message(
        FATAL_ERROR
        "In-place obfuscation failed with ${in_place_result}.\n"
        "stdout:\n${in_place_stdout}\n"
        "stderr:\n${in_place_stderr}"
    )
endif()

file(READ "${in_place_file}" in_place_source)
if(in_place_source STREQUAL "")
    message(FATAL_ERROR "In-place obfuscation left an empty source file")
endif()

string(FIND "${in_place_source}" "in_place" kernel_name_position)
if(kernel_name_position EQUAL -1)
    message(FATAL_ERROR "In-place obfuscation did not preserve the kernel name")
endif()

string(FIND "${in_place_source}" "local_value" local_name_position)
if(NOT local_name_position EQUAL -1)
    message(FATAL_ERROR "In-place obfuscation did not rename the local variable")
endif()

set(invalid_input "${WORK_DIR}/invalid-input.cl")
set(existing_output "${WORK_DIR}/existing-output.cl")
set(invalid_source "__kernel void broken( {\n")
set(output_sentinel "existing output must survive\n")
file(WRITE "${invalid_input}" "${invalid_source}")

execute_process(
    COMMAND "${PARSER}" "${invalid_input}" "${invalid_input}"
    RESULT_VARIABLE invalid_in_place_result
    OUTPUT_VARIABLE invalid_in_place_stdout
    ERROR_VARIABLE invalid_in_place_stderr
)

if(NOT invalid_in_place_result EQUAL 1)
    message(
        FATAL_ERROR
        "Invalid in-place input returned ${invalid_in_place_result}, expected 1.\n"
        "stdout:\n${invalid_in_place_stdout}\n"
        "stderr:\n${invalid_in_place_stderr}"
    )
endif()

file(READ "${invalid_input}" preserved_invalid_source)
if(NOT "${preserved_invalid_source}" STREQUAL "${invalid_source}")
    message(FATAL_ERROR "A syntax error modified the in-place input file")
endif()

file(WRITE "${existing_output}" "${output_sentinel}")

execute_process(
    COMMAND "${PARSER}" "${invalid_input}" "${existing_output}"
    RESULT_VARIABLE invalid_result
    OUTPUT_VARIABLE invalid_stdout
    ERROR_VARIABLE invalid_stderr
)

if(NOT invalid_result EQUAL 1)
    message(
        FATAL_ERROR
        "Invalid input returned ${invalid_result}, expected 1.\n"
        "stdout:\n${invalid_stdout}\n"
        "stderr:\n${invalid_stderr}"
    )
endif()

file(READ "${existing_output}" preserved_output)
if(NOT "${preserved_output}" STREQUAL "${output_sentinel}")
    message(FATAL_ERROR "A syntax error modified the existing output file")
endif()

file(GLOB leaked_temp_files "${WORK_DIR}/*.openslex.tmp.*")
if(leaked_temp_files)
    message(FATAL_ERROR "Temporary output files were not cleaned up")
endif()
