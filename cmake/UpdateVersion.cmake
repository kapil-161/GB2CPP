# Runs at build time (not configure time) to keep version_generated.h current.
find_package(Git QUIET)

set(COUNT "0")
set(HASH  "unknown")

if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE COUNT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(NOT COUNT OR COUNT STREQUAL "")
    set(COUNT "0")
endif()
if(NOT HASH OR HASH STREQUAL "")
    set(HASH "unknown")
endif()

set(GIT_COMMIT_COUNT "${COUNT}")
set(GIT_SHORT_HASH   "${HASH}")
set(GB2_VERSION      "2.0.${COUNT}")
set(GB2_VERSION_FULL "2.0.${COUNT}-${HASH}")

configure_file(
    "${SOURCE_DIR}/include/version_generated.h.in"
    "${SOURCE_DIR}/include/version_generated.h"
    @ONLY
)

message(STATUS "GB2 version updated: ${GB2_VERSION_FULL}")
