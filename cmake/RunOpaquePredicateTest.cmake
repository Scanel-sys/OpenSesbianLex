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
        "Could not obfuscate the opaque-predicate fixture.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

file(READ "${OUTPUT}" obfuscated_source)

string(REGEX MATCHALL "atomic_inc" atomic_calls "${obfuscated_source}")
list(LENGTH atomic_calls atomic_call_count)
if(NOT atomic_call_count EQUAL 1)
    message(
        FATAL_ERROR
        "The original condition must be evaluated once; found "
        "${atomic_call_count} atomic_inc calls.\n"
        "output:\n${obfuscated_source}"
    )
endif()

string(REGEX MATCH
    "int[ \t\r\n]+([A-Za-z_][A-Za-z0-9_]*)[ \t\r\n]*[(][ \t\r\n]*__global"
    helper_declaration
    "${obfuscated_source}"
)
set(helper_name "${CMAKE_MATCH_1}")
if(helper_name STREQUAL "")
    message(FATAL_ERROR "Could not find the side-effect helper definition")
endif()

string(REGEX MATCHALL
    "${helper_name}[ \t\r\n]*[(]"
    helper_occurrences
    "${obfuscated_source}"
)
list(LENGTH helper_occurrences helper_occurrence_count)
if(NOT helper_occurrence_count EQUAL 2)
    message(
        FATAL_ERROR
        "The side-effect helper must occur once as a definition and once as "
        "a call; found ${helper_occurrence_count} occurrences.\n"
        "output:\n${obfuscated_source}"
    )
endif()

string(REGEX MATCHALL "counter" original_counter_names "${obfuscated_source}")
list(LENGTH original_counter_names original_counter_count)
if(NOT original_counter_count EQUAL 0)
    message(FATAL_ERROR "The volatile counter parameter was not obfuscated")
endif()

string(REGEX REPLACE "[ \t\r\n]" "" compact_source "${obfuscated_source}")
if(NOT compact_source MATCHES
   "if[(][(][(]0x[0-9a-f]+u[*][(]0x[0-9a-f]+u[+]1u[)][)][&]1u[)]!=0u[)]")
    message(
        FATAL_ERROR
        "The side-effect-free opaque predicate was not inserted.\n"
        "output:\n${obfuscated_source}"
    )
endif()

if(compact_source MATCHES "if[ ]*[(]![ (]*atomic_inc")
    message(FATAL_ERROR "The original condition is still negated and repeated")
endif()
