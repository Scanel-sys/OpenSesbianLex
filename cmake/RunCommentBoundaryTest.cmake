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
        "Could not obfuscate the comment-boundary fixture.\n"
        "stdout:\n${parser_stdout}\n"
        "stderr:\n${parser_stderr}"
    )
endif()

file(READ "${INPUT}" input_source)
file(READ "${OUTPUT}" obfuscated_source)

foreach(required_text
    "int VALUE"
    "int \nSECOND"
    "VALUE+ +SECOND"
)
    string(FIND "${obfuscated_source}" "${required_text}" text_position)
    if(text_position EQUAL -1)
        message(
            FATAL_ERROR
            "Comment removal did not preserve token boundary '${required_text}'.\n"
            "output:\n${obfuscated_source}"
        )
    endif()
endforeach()

foreach(removed_text "/*" "first line" "second line")
    string(FIND "${obfuscated_source}" "${removed_text}" text_position)
    if(NOT text_position EQUAL -1)
        message(FATAL_ERROR "Comment text '${removed_text}' remains in output")
    endif()
endforeach()

string(REGEX REPLACE "[^\n]" "" input_newlines "${input_source}")
string(REGEX REPLACE "[^\n]" "" output_newlines "${obfuscated_source}")
string(LENGTH "${input_newlines}" input_newline_count)
string(LENGTH "${output_newlines}" output_newline_count)

if(NOT output_newline_count EQUAL input_newline_count)
    message(
        FATAL_ERROR
        "Comment removal changed the source line count: "
        "input=${input_newline_count}, output=${output_newline_count}"
    )
endif()
