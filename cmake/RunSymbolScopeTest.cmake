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
        "Could not obfuscate the symbol-scope fixture.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

file(READ "${OUTPUT}" obfuscated_source)

foreach(required_text
    "symbol_scopes"
    "KEEP_MACRO(value)"
    "vload4"
    "barrier"
    "CLK_LOCAL_MEM_FENCE"
    ".x"
)
    string(FIND "${obfuscated_source}" "${required_text}" text_position)
    if(text_position EQUAL -1)
        message(FATAL_ERROR "Obfuscated OpenCL lost '${required_text}'")
    endif()
endforeach()

foreach(renamed_identifier
    "Record_t"
    "helper_value"
    "UPPER_LOCAL"
    "value_t"
    "record"
    "constant_record"
)
    if(obfuscated_source MATCHES
       "(^|[^A-Za-z0-9_])${renamed_identifier}([^A-Za-z0-9_]|$)")
        message(
            FATAL_ERROR
            "Declared identifier '${renamed_identifier}' was not obfuscated"
        )
    endif()
endforeach()

string(REGEX MATCHALL "[.]x" vector_selectors "${obfuscated_source}")
list(LENGTH vector_selectors vector_selector_count)
if(NOT vector_selector_count EQUAL 1)
    message(
        FATAL_ERROR
        "Expected only the vector swizzle '.x' to remain, found "
        "${vector_selector_count} occurrences"
    )
endif()

string(REGEX MATCH
    "int[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*=1;"
    outer_declaration
    "${obfuscated_source}"
)
set(outer_name "${CMAKE_MATCH_1}")
string(REGEX MATCH
    "int[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*=7;"
    inner_declaration
    "${obfuscated_source}"
)
set(inner_name "${CMAKE_MATCH_1}")

if(outer_name STREQUAL "" OR inner_name STREQUAL "")
    message(FATAL_ERROR "Could not find the obfuscated shadowed declarations")
endif()
if(outer_name STREQUAL inner_name)
    message(FATAL_ERROR "Shadowed variables received the same symbol identity")
endif()
