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
