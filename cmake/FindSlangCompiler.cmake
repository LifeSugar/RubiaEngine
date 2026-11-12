# Find the host Slang command-line compiler and keep its runtime next to it.
# Inputs: SlangCompiler_ROOT (or environment variable), SlangCompiler_EXECUTABLE.
# Outputs: SlangCompiler_FOUND, SlangCompiler_VERSION, Slang::slangc.
# Vulkan SDK is a convenience fallback, not a dependency of this package.

if(SlangCompiler_ROOT OR DEFINED ENV{SlangCompiler_ROOT})
    # An explicit SDK selection must not silently fall back to another version.
    find_program(SlangCompiler_EXECUTABLE NAMES slangc
        HINTS "${SlangCompiler_ROOT}" ENV SlangCompiler_ROOT
        PATH_SUFFIXES bin Bin
        NO_DEFAULT_PATH
    )
else()
    find_program(SlangCompiler_EXECUTABLE NAMES slangc
        HINTS ENV VULKAN_SDK
        PATH_SUFFIXES bin Bin tools/shader-slang
    )
endif()

unset(SlangCompiler_VERSION)
set(_SlangCompiler_RUNS FALSE)
set(_SlangCompiler_REASON
    "Install a Slang binary distribution and set SlangCompiler_ROOT to its root, or set SlangCompiler_EXECUTABLE to slangc. Keep the matching runtime libraries beside the compiler."
)
if(SlangCompiler_EXECUTABLE)
    execute_process(
        COMMAND "${SlangCompiler_EXECUTABLE}" -version
        RESULT_VARIABLE _SlangCompiler_RESULT
        OUTPUT_VARIABLE _SlangCompiler_STDOUT
        ERROR_VARIABLE _SlangCompiler_STDERR
        TIMEOUT 15
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if("${_SlangCompiler_RESULT}" STREQUAL "0")
        # SDK builds may append a commit suffix, e.g. 2026.1-52-gc8ddf20bb.
        string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" SlangCompiler_VERSION
            "${_SlangCompiler_STDOUT}\n${_SlangCompiler_STDERR}")
        if(SlangCompiler_VERSION)
            set(_SlangCompiler_RUNS TRUE)
        endif()
    endif()
    if(NOT _SlangCompiler_RUNS)
        string(APPEND _SlangCompiler_REASON
            " Compiler version probe failed (${_SlangCompiler_RESULT}): ${_SlangCompiler_STDOUT} ${_SlangCompiler_STDERR}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SlangCompiler
    REQUIRED_VARS SlangCompiler_EXECUTABLE _SlangCompiler_RUNS
    VERSION_VAR SlangCompiler_VERSION
    REASON_FAILURE_MESSAGE "${_SlangCompiler_REASON}"
)

if(SlangCompiler_FOUND AND NOT TARGET Slang::slangc)
    add_executable(Slang::slangc IMPORTED GLOBAL)
    set_target_properties(Slang::slangc PROPERTIES
        IMPORTED_LOCATION "${SlangCompiler_EXECUTABLE}"
    )
endif()
mark_as_advanced(SlangCompiler_EXECUTABLE)
