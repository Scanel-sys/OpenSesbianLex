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
        "Could not obfuscate the preprocessor fixture.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

file(READ "${OUTPUT}" obfuscated_source)

foreach(required_text
    "#pragma OPENCL FP_CONTRACT ON"
    "#define FEATURE_LEVEL 2"
    "left ## right"
    "#value"
    "operation(__VA_ARGS__)"
    "CALL_HELPER(value) macro_helper(value)"
    "#if defined(FEATURE_LEVEL) && (FEATURE_LEVEL >= 2)"
    "#if FEATURE_LEVEL > 0"
    "#elif FEATURE_LEVEL == 1"
    "#else"
    "#endif"
    "#undef TEMP_MACRO"
    "preprocessor_features"
    "macro_helper"
)
    string(FIND "${obfuscated_source}" "${required_text}" text_position)
    if(text_position EQUAL -1)
        message(FATAL_ERROR "Obfuscated source lost '${required_text}'")
    endif()
endforeach()

set(multiline_macro [=[#define MULTILINE_VALUE(value) \
    ((value) + \
     1)]=])
string(FIND "${obfuscated_source}" "${multiline_macro}" multiline_position)
if(multiline_position EQUAL -1)
    message(FATAL_ERROR "The multiline macro was not preserved exactly")
endif()

foreach(ordinary_identifier "define" "include" "ifdef" "ifndef" "endif")
    string(FIND
        "${obfuscated_source}"
        "int ${ordinary_identifier}"
        identifier_position
    )
    if(NOT identifier_position EQUAL -1)
        message(
            FATAL_ERROR
            "Preprocessor word '${ordinary_identifier}' was treated as a "
            "keyword outside a directive"
        )
    endif()
endforeach()

string(FIND "${obfuscated_source}" "%:define" rewritten_hash_position)
if(NOT rewritten_hash_position EQUAL -1)
    message(FATAL_ERROR "A directive # token was rewritten as a digraph")
endif()
