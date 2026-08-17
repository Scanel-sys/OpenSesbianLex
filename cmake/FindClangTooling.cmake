include(FindPackageHandleStandardArgs)

set(_clang_tooling_roots)
foreach(root_variable ClangTooling_ROOT Clang_ROOT LLVM_ROOT)
    if(DEFINED ${root_variable} AND NOT "${${root_variable}}" STREQUAL "")
        list(APPEND _clang_tooling_roots "${${root_variable}}")
    endif()
    if(DEFINED ENV{${root_variable}} AND NOT "$ENV{${root_variable}}" STREQUAL "")
        list(APPEND _clang_tooling_roots "$ENV{${root_variable}}")
    endif()
endforeach()

if(WIN32 AND DEFINED ENV{ProgramFiles})
    list(APPEND _clang_tooling_roots
        "$ENV{ProgramFiles}/LLVM"
        "$ENV{ProgramFiles}/LLVM-Dev"
    )
endif()
file(GLOB _clang_tooling_unix_roots "/usr/lib/llvm-*")
list(APPEND _clang_tooling_roots ${_clang_tooling_unix_roots})
list(REMOVE_DUPLICATES _clang_tooling_roots)

if(NOT LLVM_DIR)
    find_path(
        _clang_tooling_llvm_config_directory
        LLVMConfig.cmake
        HINTS ${_clang_tooling_roots}
        PATH_SUFFIXES lib/cmake/llvm
    )
    if(_clang_tooling_llvm_config_directory)
        set(LLVM_DIR "${_clang_tooling_llvm_config_directory}")
    endif()
endif()
find_package(LLVM CONFIG QUIET)

if(NOT Clang_DIR)
    find_path(
        _clang_tooling_clang_config_directory
        ClangConfig.cmake
        HINTS ${_clang_tooling_roots}
        PATH_SUFFIXES lib/cmake/clang
    )
    if(_clang_tooling_clang_config_directory)
        set(Clang_DIR "${_clang_tooling_clang_config_directory}")
    endif()
endif()
find_package(Clang CONFIG QUIET)

# Official Windows LLVM archives can contain an absolute diaguids.lib path
# from the machine that produced the package. Replace that non-relocatable
# entry with the DIA SDK belonging to the Visual Studio instance selected by
# CMake. Users of Ninja can provide ClangTooling_DIAGUIDS_LIBRARY explicitly.
if(WIN32 AND TARGET LLVMDebugInfoPDB)
    get_target_property(
        _clang_tooling_pdb_links
        LLVMDebugInfoPDB
        INTERFACE_LINK_LIBRARIES
    )
    set(_clang_tooling_needs_diaguids_replacement OFF)
    foreach(_link_entry IN LISTS _clang_tooling_pdb_links)
        if(_link_entry MATCHES "[\\/]diaguids\\.lib$" AND
           NOT EXISTS "${_link_entry}")
            set(_clang_tooling_needs_diaguids_replacement ON)
        endif()
    endforeach()

    if(_clang_tooling_needs_diaguids_replacement)
        find_file(
            ClangTooling_DIAGUIDS_LIBRARY
            NAMES diaguids.lib
            HINTS
                "${CMAKE_GENERATOR_INSTANCE}/DIA SDK/lib/amd64"
                "$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64"
        )
        if(ClangTooling_DIAGUIDS_LIBRARY)
            set(_clang_tooling_repaired_pdb_links)
            foreach(_link_entry IN LISTS _clang_tooling_pdb_links)
                if(_link_entry MATCHES "[\\/]diaguids\\.lib$" AND
                   NOT EXISTS "${_link_entry}")
                    list(APPEND _clang_tooling_repaired_pdb_links
                        "${ClangTooling_DIAGUIDS_LIBRARY}"
                    )
                else()
                    list(APPEND _clang_tooling_repaired_pdb_links
                        "${_link_entry}"
                    )
                endif()
            endforeach()
            set_target_properties(
                LLVMDebugInfoPDB
                PROPERTIES INTERFACE_LINK_LIBRARIES
                    "${_clang_tooling_repaired_pdb_links}"
            )
        endif()
    endif()
endif()

set(ClangTooling_LINK_TARGETS)
if(TARGET clang-cpp)
    list(APPEND ClangTooling_LINK_TARGETS clang-cpp)
elseif(TARGET clangTooling)
    list(APPEND ClangTooling_LINK_TARGETS
        clangTooling
        clangToolingCore
        clangFrontend
        clangSerialization
        clangDriver
        clangParse
        clangSema
        clangAnalysis
        clangAST
        clangLex
        clangBasic
    )
endif()

find_program(
    ClangTooling_CLANG_EXECUTABLE
    NAMES clang clang-22 clang-21 clang-20 clang-19 clang-18
    HINTS ${_clang_tooling_roots} "${LLVM_TOOLS_BINARY_DIR}"
    PATH_SUFFIXES bin
)
if(ClangTooling_CLANG_EXECUTABLE)
    execute_process(
        COMMAND "${ClangTooling_CLANG_EXECUTABLE}" -print-resource-dir
        RESULT_VARIABLE _clang_tooling_resource_result
        OUTPUT_VARIABLE _clang_tooling_resource_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_clang_tooling_resource_result EQUAL 0 AND
       EXISTS "${_clang_tooling_resource_output}/include/opencl-c-base.h")
        set(
            ClangTooling_RESOURCE_DIR
            "${_clang_tooling_resource_output}"
            CACHE PATH "Clang builtin resource directory"
        )
    endif()
endif()

set(_clang_tooling_required_targets TRUE)
if(NOT ClangTooling_LINK_TARGETS)
    set(_clang_tooling_required_targets FALSE)
endif()

find_package_handle_standard_args(
    ClangTooling
    REQUIRED_VARS
        LLVM_FOUND
        Clang_FOUND
        _clang_tooling_required_targets
        ClangTooling_RESOURCE_DIR
)

if(ClangTooling_FOUND AND NOT TARGET ClangTooling::ClangTooling)
    add_library(ClangTooling::ClangTooling INTERFACE IMPORTED)
    set_target_properties(
        ClangTooling::ClangTooling
        PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES
                "${LLVM_INCLUDE_DIRS};${CLANG_INCLUDE_DIRS}"
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                "${LLVM_INCLUDE_DIRS};${CLANG_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES
                "${ClangTooling_LINK_TARGETS}"
    )
endif()

mark_as_advanced(
    ClangTooling_CLANG_EXECUTABLE
    ClangTooling_DIAGUIDS_LIBRARY
    ClangTooling_RESOURCE_DIR
    _clang_tooling_llvm_config_directory
    _clang_tooling_clang_config_directory
)
