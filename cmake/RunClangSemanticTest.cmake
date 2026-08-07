foreach(required_variable PARSER INPUT OUTPUT)
    if(NOT DEFINED ${required_variable})
        message(FATAL_ERROR "${required_variable} is required")
    endif()
endforeach()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
file(REMOVE "${OUTPUT}")

execute_process(
    COMMAND
        "${PARSER}"
        --frontend clang
        --seed 246813579
        "${INPUT}"
        "${OUTPUT}"
    RESULT_VARIABLE parser_result
    OUTPUT_VARIABLE parser_stdout
    ERROR_VARIABLE parser_stderr
)

if(NOT parser_result EQUAL 0)
    message(
        FATAL_ERROR
        "Clang semantic obfuscation failed.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

file(READ "${OUTPUT}" obfuscated_source)

foreach(required_text
    "clang_semantic_frontend"
    "macro_qualified_kernel"
    "CALL_MACRO_TARGET(argument) macro_target(argument)"
    "macro_target"
    "semantic_external_helper"
    ".x"
    ".xy"
    ".s0"
)
    string(FIND "${obfuscated_source}" "${required_text}" text_position)
    if(text_position EQUAL -1)
        message(FATAL_ERROR "Clang output lost '${required_text}'")
    endif()
endforeach()

foreach(renamed_identifier
    "SemanticItem"
    "semantic_helper"
    "item"
    "value"
)
    if(obfuscated_source MATCHES
       "(^|[^A-Za-z0-9_])${renamed_identifier}([^A-Za-z0-9_]|$)")
        message(
            FATAL_ERROR
            "Semantic identifier '${renamed_identifier}' was not renamed"
        )
    endif()
endforeach()

string(REGEX MATCH
    "int[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*;[ \t\r\n]*float4"
    field_declaration
    "${obfuscated_source}"
)
set(field_name "${CMAKE_MATCH_1}")
string(REGEX MATCH
    "int[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*11"
    local_declaration
    "${obfuscated_source}"
)
set(local_name "${CMAKE_MATCH_1}")
if(field_name STREQUAL "" OR local_name STREQUAL "")
    message(FATAL_ERROR "Could not locate the independently renamed symbols")
endif()
if(field_name STREQUAL local_name)
    message(FATAL_ERROR "A structure field and local variable shared one identity")
endif()

# Re-run the semantic frontend over its own output. Besides validating the
# generated OpenCL, this proves that the result is not limited by Bison.
get_filename_component(input_directory "${INPUT}" DIRECTORY)
execute_process(
    COMMAND
        "${PARSER}"
        --frontend clang
        "--clang-arg=-I${input_directory}"
        "${OUTPUT}"
        "${OUTPUT}.second.cl"
    RESULT_VARIABLE reparse_result
    OUTPUT_VARIABLE reparse_stdout
    ERROR_VARIABLE reparse_stderr
)
if(NOT reparse_result EQUAL 0)
    message(
        FATAL_ERROR
        "Clang rejected the obfuscated result.\n"
        "stdout:\n${reparse_stdout}\n"
        "stderr:\n${reparse_stderr}"
    )
endif()
