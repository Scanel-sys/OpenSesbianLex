include(FindPackageHandleStandardArgs)

find_package(Clang CONFIG QUIET)

set(_libclang_include_hints ${CLANG_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
set(_libclang_library_hints ${CLANG_LIBRARY_DIRS} ${LLVM_LIBRARY_DIRS})

file(GLOB _libclang_unix_prefixes "/usr/lib/llvm-*")
foreach(_prefix IN LISTS _libclang_unix_prefixes)
    list(APPEND _libclang_include_hints "${_prefix}/include")
    list(APPEND _libclang_library_hints "${_prefix}/lib")
endforeach()

if(WIN32)
    list(APPEND _libclang_include_hints
        "$ENV{ProgramFiles}/LLVM/include"
    )
    list(APPEND _libclang_library_hints
        "$ENV{ProgramFiles}/LLVM/lib"
        "$ENV{ProgramFiles}/LLVM/bin"
    )
endif()

find_program(
    LibClang_CLANG_EXECUTABLE
    NAMES clang clang-21 clang-20 clang-19 clang-18 clang-17 clang-16
    HINTS
        "$ENV{ProgramFiles}/LLVM/bin"
        ${_libclang_library_hints}
)
if(LibClang_CLANG_EXECUTABLE)
    execute_process(
        COMMAND "${LibClang_CLANG_EXECUTABLE}" -print-resource-dir
        RESULT_VARIABLE _libclang_resource_result
        OUTPUT_VARIABLE _libclang_resource_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_libclang_resource_result EQUAL 0 AND
       EXISTS "${_libclang_resource_output}/include/opencl-c-base.h")
        file(TO_CMAKE_PATH
            "${_libclang_resource_output}"
            LibClang_RESOURCE_DIR
        )
    endif()
endif()

if(NOT LibClang_RESOURCE_DIR)
    set(_libclang_resource_prefixes ${_libclang_unix_prefixes})
    if(WIN32)
        list(APPEND _libclang_resource_prefixes "$ENV{ProgramFiles}/LLVM")
    endif()
    foreach(_prefix IN LISTS _libclang_resource_prefixes)
        file(GLOB _libclang_builtin_headers
            "${_prefix}/lib/clang/*/include/opencl-c-base.h"
        )
        list(LENGTH _libclang_builtin_headers _libclang_header_count)
        if(_libclang_header_count GREATER 0)
            list(GET _libclang_builtin_headers 0 _libclang_builtin_header)
            get_filename_component(
                _libclang_builtin_include_dir
                "${_libclang_builtin_header}"
                DIRECTORY
            )
            get_filename_component(
                LibClang_RESOURCE_DIR
                "${_libclang_builtin_include_dir}"
                DIRECTORY
            )
            break()
        endif()
    endforeach()
endif()

find_path(
    LibClang_INCLUDE_DIR
    NAMES clang-c/Index.h
    HINTS ${_libclang_include_hints}
)
find_library(
    LibClang_LIBRARY
    NAMES
        libclang
        clang
        libclang-21 clang-21
        libclang-20 clang-20
        libclang-19 clang-19
        libclang-18 clang-18
        libclang-17 clang-17
        libclang-16 clang-16
    HINTS ${_libclang_library_hints}
)
if(WIN32)
    find_file(
        LibClang_RUNTIME_LIBRARY
        NAMES libclang.dll
        HINTS
            "$ENV{ProgramFiles}/LLVM/bin"
            ${_libclang_library_hints}
    )
endif()

set(_libclang_required_variables
    LibClang_INCLUDE_DIR
    LibClang_LIBRARY
    LibClang_RESOURCE_DIR
)
if(WIN32)
    list(APPEND _libclang_required_variables LibClang_RUNTIME_LIBRARY)
endif()
find_package_handle_standard_args(
    LibClang
    REQUIRED_VARS ${_libclang_required_variables}
)

if(LibClang_FOUND AND NOT TARGET LibClang::LibClang)
    add_library(LibClang::LibClang UNKNOWN IMPORTED)
    set_target_properties(
        LibClang::LibClang
        PROPERTIES
            IMPORTED_LOCATION "${LibClang_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LibClang_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(
    LibClang_INCLUDE_DIR
    LibClang_LIBRARY
    LibClang_RESOURCE_DIR
    LibClang_RUNTIME_LIBRARY
    LibClang_CLANG_EXECUTABLE
)
