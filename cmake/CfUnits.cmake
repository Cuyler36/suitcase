# Routes the units exported by configure.py into the PC build's targets.
#
# units.cmake calls cf_unit() once per unit. Each unit keeps the defines and include paths
# configure.py gives it (minus MWCC/MSL-only ones), so libraries built with another
# revision's flags (e.g. nw4r's VERSION_RSPE01_01) stay separate from the game's VERSION.
# Units of libraries the port replaces (hardware SDK modules, RevoEX, DWC, MSL, runtime, TRK)
# are not routed anywhere. HBM is not built either: src/cf/sdk/hbm_stub.cpp stands in for it
# until the HOME button menu is recreated with RmlUi.
include_guard(GLOBAL)

# configure.py library name -> PC target
set(_cf_lib_targets
        "cLib=cf_game" "sLib=cf_game" "framework=cf_game" "mLib=cf_game" "game=cf_game"
        "sound=cf_game" "RegEad=cf_game" "keyboard=cf_game"
        "NW4R=cf_nw4r"
        "EGG=cf_egg"
        "RVLFaceLib (wii-ipl)=cf_rfl"
        "MWM=cf_mwm"
        "RVL_SDK (wii-ipl)=cf_rvl"
)
# RVL_SDK modules that are plain software and compile as-is; the rest are aurora or shims
set(CF_RVL_SOFTWARE_MODULES arc cx enc kpr mem tpl)
# REL loader glue; the PC build links the RELs statically
set(CF_EXCLUDE_SOURCE_REGEX "^src/runtime/|/rel_init\\.cpp$")
set(CF_DROP_DEFINE_REGEX "^(MUST_MATCH|NULL)(=|$)")
set(CF_DROP_INCLUDE_REGEX "MSL|PowerPC_EABI_Support")

set(CF_UNIT_TARGETS cf_game cf_nw4r cf_egg cf_rfl cf_mwm cf_rvl)

function(_cf_unit_target lib source out)
    set(target "")
    if (lib MATCHES "NP$")
        set(target cf_game)
    else ()
        foreach (entry IN LISTS _cf_lib_targets)
            string(FIND "${entry}" "=" eq REVERSE)
            string(SUBSTRING "${entry}" 0 ${eq} name)
            if (name STREQUAL lib)
                math(EXPR eq "${eq} + 1")
                string(SUBSTRING "${entry}" ${eq} -1 target)
                break ()
            endif ()
        endforeach ()
    endif ()
    if (target STREQUAL "cf_rvl")
        # lib/RVL_SDK/src/<module>/...
        string(REGEX MATCH "^lib/RVL_SDK/src/([^/]+)/" _ "${source}")
        if (NOT CMAKE_MATCH_1 IN_LIST CF_RVL_SOFTWARE_MODULES)
            set(target "")
        endif ()
    endif ()
    if (source MATCHES "${CF_EXCLUDE_SOURCE_REGEX}")
        set(target "")
    endif ()
    set(${out} "${target}" PARENT_SCOPE)
endfunction()

function(cf_unit)
    # Not PARSE_ARGV: the quoted lists from units.cmake must split into their elements
    cmake_parse_arguments(U "" "LIB;SOURCE;COMPLETED" "DEFINES;INCLUDES;PREFIX_HEADERS" ${ARGN})
    _cf_unit_target("${U_LIB}" "${U_SOURCE}" target)
    if (NOT target)
        return ()
    endif ()

    set(defines ${U_DEFINES})
    list(FILTER defines EXCLUDE REGEX "${CF_DROP_DEFINE_REGEX}")
    # PC overrides first (src/cf/include/README.md)
    set(includes "${PROJECT_SOURCE_DIR}/src/cf/include")
    foreach (dir IN LISTS U_INCLUDES)
        if (NOT dir MATCHES "${CF_DROP_INCLUDE_REGEX}")
            list(APPEND includes "${PROJECT_SOURCE_DIR}/${dir}")
        endif ()
    endforeach ()
    set(options "")
    foreach (header IN LISTS U_PREFIX_HEADERS)
        if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            list(APPEND options "/FI${header}")
        else ()
            list(APPEND options "SHELL:-include ${header}")
        endif ()
    endforeach ()

    set(source "${PROJECT_SOURCE_DIR}/${U_SOURCE}")
    set_source_files_properties("${source}" PROPERTIES
            COMPILE_DEFINITIONS "${defines}"
            INCLUDE_DIRECTORIES "${includes}"
            COMPILE_OPTIONS "${options}"
    )
    set_property(GLOBAL APPEND PROPERTY CF_SOURCES_${target} "${source}")
    if (NOT U_COMPLETED)
        set_property(GLOBAL APPEND PROPERTY CF_INCOMPLETE_${target} "${U_SOURCE}")
    endif ()
endfunction()

# Includes the exported units and creates one library per target. The libraries are only
# built when something links them (CF_BUILD_GAME), since the decomp sources do not compile
# for the host yet.
function(cf_add_unit_targets)
    include("${CF_UNITS_FILE}")
    foreach (target IN LISTS CF_UNIT_TARGETS)
        get_property(sources GLOBAL PROPERTY CF_SOURCES_${target})
        if (NOT sources)
            continue ()
        endif ()
        list(LENGTH sources count)
        if (target STREQUAL "cf_game")
            add_library(${target} OBJECT EXCLUDE_FROM_ALL ${sources})
        else ()
            add_library(${target} STATIC EXCLUDE_FROM_ALL ${sources})
        endif ()
        target_compile_definitions(${target} PRIVATE TARGET_PC=1 AVOID_UB=1)
        target_link_libraries(${target} PRIVATE cf_cw_compat)
        set_target_properties(${target} PROPERTIES FOLDER "decomp")
        get_property(incomplete GLOBAL PROPERTY CF_INCOMPLETE_${target})
        list(LENGTH incomplete incomplete_count)
        message(STATUS "suitcase: ${target}: ${count} units (${incomplete_count} not yet matching)")
    endforeach ()
endfunction()
