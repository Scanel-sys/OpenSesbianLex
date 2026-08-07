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
        "Could not obfuscate the OpenCL language fixture.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

file(READ "${OUTPUT}" obfuscated_source)

foreach(required_text
    "language_coverage"
    "image_language_coverage"
    "__attribute__"
    "reqd_work_group_size"
    "vec_type_hint"
    "image2d_t"
    "sampler_t"
    "read_only"
    "write_only"
    "ROTATE_AND_SELECT"
    "#ifdef cl_khr_fp16"
)
    string(FIND "${obfuscated_source}" "${required_text}" text_position)
    if(text_position EQUAL -1)
        message(FATAL_ERROR "Obfuscated source lost '${required_text}'")
    endif()
endforeach()

foreach(local_name "x" "y" "z" "M" "N")
    string(REGEX MATCH
        "int[ \t\r\n]+${local_name}[ \t\r\n=;,]"
        unchanged_declaration
        "${obfuscated_source}"
    )
    if(NOT unchanged_declaration STREQUAL "")
        message(FATAL_ERROR "Local 'int ${local_name}' was not obfuscated")
    endif()
endforeach()

string(FIND "${obfuscated_source}" ".xy" swizzle_position)
if(swizzle_position EQUAL -1)
    message(FATAL_ERROR "The vector .xy selector was not preserved")
endif()

string(FIND "${obfuscated_source}" ".s0" s0_position)
if(s0_position EQUAL -1)
    message(FATAL_ERROR "The vector .s0 selector was not preserved")
endif()

string(REGEX MATCH
    "int[ \t\r\n]+([A-Za-z_][A-Za-z0-9_]*)[ \t\r\n]*;"
    field_declaration
    "${obfuscated_source}"
)
set(field_name "${CMAKE_MATCH_1}")
string(REGEX MATCH
    "int[ \t\r\n]+([A-Za-z_][A-Za-z0-9_]*)[ \t\r\n]*=[ \t\r\n]*6;"
    local_declaration
    "${obfuscated_source}"
)
set(local_name "${CMAKE_MATCH_1}")
if(field_name STREQUAL "" OR local_name STREQUAL "")
    message(FATAL_ERROR "Could not find the field/local same-name declarations")
endif()
if(field_name STREQUAL local_name)
    message(FATAL_ERROR "A structure field and local variable share one symbol")
endif()
