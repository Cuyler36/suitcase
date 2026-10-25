# Selects the game version this build tree is for and exports its units from configure.py.
#
# One build tree per version: CF_VERSION is fixed when the tree is first configured. The unit
# list, per-unit defines and include paths come from `configure.py pc -v ${CF_VERSION}`, so
# version-specific sources and library revisions always follow the decomp's configuration.
include_guard(GLOBAL)

# Same order as VERSIONS in configure.py and include/game_version.h
set(CF_VERSIONS RUUE01_00 RUUE01_01 RUUP01_00 RUUP01_01 RUUJ01_01 RUUJ01_02 RUUK01_01)
set(CF_VERSION "RUUE01_00" CACHE STRING "Game version this build tree is for")
set_property(CACHE CF_VERSION PROPERTY STRINGS ${CF_VERSIONS})

string(TOUPPER "${CF_VERSION}" _cf_version)
if (NOT _cf_version STREQUAL CF_VERSION)
    set(CF_VERSION "${_cf_version}" CACHE STRING "Game version this build tree is for" FORCE)
endif ()
list(FIND CF_VERSIONS "${CF_VERSION}" CF_VERSION_NUM)
if (CF_VERSION_NUM EQUAL -1)
    message(FATAL_ERROR "Unknown CF_VERSION '${CF_VERSION}'; expected one of: ${CF_VERSIONS}")
endif ()

# Switching versions in place would mix objects from two versions; use a separate build tree.
if (DEFINED CACHE{_CF_CONFIGURED_VERSION} AND NOT _CF_CONFIGURED_VERSION STREQUAL CF_VERSION)
    message(FATAL_ERROR
            "This build tree is for ${_CF_CONFIGURED_VERSION}, not ${CF_VERSION}. "
            "Configure ${CF_VERSION} in its own build directory (e.g. -B build/pc/${CF_VERSION}).")
endif ()
set(_CF_CONFIGURED_VERSION "${CF_VERSION}" CACHE INTERNAL "")

if (CF_VERSION STREQUAL "RUUK01_01")
    message(WARNING "RUUK01_01 is not targeted yet: its textinput revision and libraries are still being matched.")
endif ()

# RUUE01_00 -> game ID RUUE01, disc revision 0
string(SUBSTRING "${CF_VERSION}" 0 6 CF_GAME_ID)
string(SUBSTRING "${CF_VERSION}" 7 2 _cf_revision)
math(EXPR CF_DISC_REVISION "${_cf_revision}")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CF_DECOMP_EXPORT_DIR "${CMAKE_BINARY_DIR}/decomp")
set(CF_UNITS_FILE "${CF_DECOMP_EXPORT_DIR}/${CF_VERSION}/pc/units.cmake")
execute_process(
        COMMAND "${Python3_EXECUTABLE}" configure.py pc -v "${CF_VERSION}" --build-dir "${CF_DECOMP_EXPORT_DIR}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE _cf_export_result
        OUTPUT_QUIET
)
if (NOT _cf_export_result EQUAL 0 OR NOT EXISTS "${CF_UNITS_FILE}")
    message(FATAL_ERROR "configure.py pc -v ${CF_VERSION} failed (${_cf_export_result})")
endif ()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/configure.py"
        "${PROJECT_SOURCE_DIR}/tools/project.py"
        "${PROJECT_SOURCE_DIR}/tools/pc_sources.py"
)

message(STATUS "suitcase: ${CF_VERSION} (game ID ${CF_GAME_ID}, revision ${CF_DISC_REVISION})")
