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
    RESULT_VARIABLE parser_exit_code
    OUTPUT_VARIABLE parser_stdout
    ERROR_VARIABLE parser_stderr
)

if(NOT parser_exit_code EQUAL 0)
    message(
        FATAL_ERROR
        "Obfuscator exited with ${parser_exit_code}.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

if(NOT parser_stdout MATCHES "(^|[\r\n])PASS([\r\n]|$)")
    message(FATAL_ERROR "Obfuscator did not print PASS: ${parser_stdout}")
endif()

if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "Obfuscator did not create ${OUTPUT}")
endif()

file(READ "${OUTPUT}" obfuscated_source)
if(obfuscated_source STREQUAL "")
    message(FATAL_ERROR "Obfuscator created an empty output file")
endif()

string(FIND "${obfuscated_source}" "keep_kernel" kernel_name_position)
if(kernel_name_position EQUAL -1)
    message(FATAL_ERROR "The externally visible kernel name was not preserved")
endif()

string(FIND "${obfuscated_source}" "\"local_value\"" literal_position)
if(literal_position EQUAL -1)
    message(FATAL_ERROR "Identifier replacement modified a string literal")
endif()

string(REPLACE "\"local_value\"" "" source_without_literal "${obfuscated_source}")
string(FIND "${source_without_literal}" "local_value" local_name_position)
if(NOT local_name_position EQUAL -1)
    message(FATAL_ERROR "The local variable name was not obfuscated")
endif()

string(REGEX REPLACE "[ \t\r\n]" "" compact_source "${obfuscated_source}")
if(NOT compact_source MATCHES
   "if[(][(][(]0x[0-9a-f]+u[*][(]0x[0-9a-f]+u[+]1u[)][)][&]1u[)]!=0u[)]")
    message(FATAL_ERROR "The safe opaque-false branch was not inserted")
endif()

string(FIND "${obfuscated_source}" "<:" digraph_position)
string(FIND "${obfuscated_source}" "??(" trigraph_position)
if(digraph_position EQUAL -1 AND trigraph_position EQUAL -1)
    message(FATAL_ERROR "Array brackets were not obfuscated")
endif()
