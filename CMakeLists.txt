# PC port of Animal Crossing: City Folk on aurora + borealis.
#
# One build tree per game version: configure with -DCF_VERSION=<version> (default RUUE01_00).
# The MWCC decomp build (configure.py + ninja) is separate and unaffected by this file.
cmake_minimum_required(VERSION 3.25)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING
            "Build type options: Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif ()

include(extern/borealis/cmake/DetectVersion.cmake)
borealis_detect_version()
project(suitcase LANGUAGES C CXX VERSION ${BOREALIS_APP_VERSION})

if (APPLE)
    enable_language(OBJC OBJCXX)
endif ()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "_cmake")

option(CF_BUILD_GAME "Compile and link the decomp sources (OFF: only the aurora shell, for bring-up)" ON)
# The Linux release presets turn this on: prebuilt dependencies bring DWARF even without -g
option(CF_STRIP_DEBUG_INFO "Link the executable without debug info, keeping its symbols (GNU-style linkers)" OFF)

include(cmake/CfVersion.cmake)

if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(DAWN_USE_WAYLAND ON CACHE BOOL "Enable support for Wayland surface" FORCE)
endif ()
set(AURORA_ENABLE_DVD ON CACHE BOOL "Enable DVD API support" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "Enable CARD API support" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "Enable THP decoder" FORCE)
# The Wii's bus clock, so OS time runs at the Wii's 60.75 MHz: saves hold times in those ticks
set(AURORA_OS_BUS_CLOCK 243000000 CACHE STRING "Wii bus clock" FORCE)
add_subdirectory(extern/aurora EXCLUDE_FROM_ALL)
add_subdirectory(extern/borealis EXCLUDE_FROM_ALL)

# A function that falls off the end without a value is UB that optimized builds turn into crashes
# (../pc_port_ub.md UB-10): an error, not a warning. So is returning a reference to a local or a
# temporary, which the Wii build happens to survive and optimized PC builds do not (UB-12, UB-14).
# This directory's targets only.
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    # Multi-character constants ('ABCD') encode identically on CW, GCC, Clang and MSVC
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wno-multichar>
            $<$<COMPILE_LANGUAGE:C,CXX>:-Werror=return-type>)
    # MSVC's C4172 (a reference to a local or temporary returned) under each compiler's name
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Werror=return-stack-address>)
    else ()
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Werror=return-local-addr>)
    endif ()
    # The game's data tables put values like 0xFFF1 in s16 fields, which wrap as on the Wii (MSVC
    # accepts them)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # The decomp's PS functions keep CW's `register` parameters, an error in C++17 on Clang
        # only (GCC warns and MSVC accepts them)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-c++11-narrowing>
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>)
    else ()
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-narrowing>)
    endif ()
    # As the Wii's compiler (and MSVC), which the game's code was written for: type punning through
    # pointer casts and signed overflow that wraps
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-fno-strict-aliasing>
            $<$<COMPILE_LANGUAGE:C,CXX>:-fwrapv>)
    # Bit-fields laid out as MSVC does, which the PC layouts of the game's file and save structs were
    # written against (a field of a different type starts a new unit; GCC and Clang pack across)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-mms-bitfields>)
    endif ()
    # extern "C" functions can throw (see /EHs below): C code gets unwind tables
    add_compile_options($<$<COMPILE_LANGUAGE:C>:-fexceptions>)
    if (NOT APPLE)
        # The decomp's static libraries and aurora's reference each other in cycles (the game calls the
        # SDK layer, which calls back into the game). MSVC's and Apple's linkers search the libraries
        # until nothing changes; GNU ld and lld take each once, in order, unless grouped.
        set(CMAKE_CXX_LINK_EXECUTABLE
                "<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> -Wl,--start-group <LINK_LIBRARIES> -Wl,--end-group")
    endif ()
elseif (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(
            $<$<COMPILE_LANGUAGE:C,CXX>:/we4715>
            $<$<COMPILE_LANGUAGE:C,CXX>:/we4172>
            $<$<COMPILE_LANGUAGE:C,CXX>:/bigobj>
            $<$<COMPILE_LANGUAGE:C,CXX>:/MP>
            $<$<COMPILE_LANGUAGE:C,CXX>:/FS>
            $<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>
            /Zc:__cplusplus
    )
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # clang-cl: CW's `register` parameters, as for Clang above
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-register>)
    endif ()
    # extern "C" functions can throw: the SDK layer's waits unwind a game thread that is being ended
    # (src/cf/sdk/thread_system.h), and with /EHsc callers may skip their cleanup. This directory's
    # targets only (aurora and borealis are added above).
    string(REPLACE "/EHsc" "/EHs" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif ()

# Match the original game's (and x86's) signed char on ARM
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arch)
if (_arch MATCHES "^(arm|aarch64)" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    add_compile_options(-fsigned-char)
endif ()

# Host implementations of the CodeWarrior / PowerPC intrinsics (include/lib/cw_compat.h)
add_library(cf_cw_compat STATIC src/cf/cw_compat.c src/cf/extras.c)
target_include_directories(cf_cw_compat PUBLIC include/lib PRIVATE src/cf/include)
target_compile_definitions(cf_cw_compat PUBLIC TARGET_PC=1)
if (MSVC)
    target_compile_definitions(cf_cw_compat PRIVATE _CRT_SECURE_NO_WARNINGS)
endif ()

include(cmake/CfUnits.cmake)
cf_add_unit_targets()

# PC sources built against the decomp's headers (with the PC overrides first). Like the decomp
# targets they are only built with CF_BUILD_GAME.
function(cf_add_pc_decomp_library name)
    add_library(${name} STATIC EXCLUDE_FROM_ALL ${ARGN})
    target_include_directories(${name} PRIVATE src/cf/include extern/aurora/include include include/lib
            lib/common/include/global lib/RevoEX/include "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(${name} PRIVATE TARGET_PC=1 AVOID_UB=1 REVOLUTION VERSION=${CF_VERSION_NUM})
    target_link_libraries(${name} PRIVATE cf_cw_compat aurora::core aurora::gx borealis::log)
endfunction()

# Host input as the Wii Remotes: aurora's WPAD/KPAD, with suitcase's provider for channel 0
# (src/cf/include/suitcase/wpad_host.hpp)
file(GLOB CF_INPUT_SOURCES CONFIGURE_DEPENDS src/cf/input/*.cpp)
cf_add_pc_decomp_library(cf_input ${CF_INPUT_SOURCES})
target_link_libraries(cf_input PUBLIC aurora::wpad aurora::kpad PRIVATE ${AURORA_SDL3_TARGET})

# Stand-ins for SDK libraries the PC build does not compile
file(GLOB CF_SDK_SOURCES CONFIGURE_DEPENDS src/cf/sdk/*.cpp)
cf_add_pc_decomp_library(cf_sdk_stubs ${CF_SDK_SOURCES})
target_link_libraries(cf_sdk_stubs PRIVATE cf_input ${AURORA_SDL3_TARGET} # SDL3: audio output (ax_rvl.cpp)
        nlohmann_json::nlohmann_json # WiiConnect24 state (nwc24_revoex.cpp)
        borealis::socket borealis::http) # SO, NHTTP and the online layers (so_host_revoex.cpp, host_net.cpp)
# RevoEX's headers clash with the decomp's (tools/gen_pc_sdk_stubs.py): put them first for its stubs
file(GLOB CF_SDK_REVOEX_SOURCES CONFIGURE_DEPENDS src/cf/sdk/*_revoex.cpp)
set_source_files_properties(${CF_SDK_REVOEX_SOURCES} PROPERTIES
        INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/lib/RevoEX/include")

# Game definitions the decomp lacks on PC (data from the original binary, etc.)
file(GLOB CF_GAME_PC_SOURCES CONFIGURE_DEPENDS src/cf/game/*.cpp)

# Data the game takes straight from main.dol: sized from this version's symbols.txt, filled from the
# disc at startup (cf::LoadDolData), so none of it is stored in the repository or the build
# (the generator emits only the ones in this version's symbols.txt). Constants and tables without
# assets are defined in source instead (src/cf/game/dol_data.cpp).
set(CF_DOL_BLOBS
        g_cautionArc g_cautionFont
        g_cautionMsgUS g_cautionMsgQC g_cautionMsgMX                              # RUUE01
        g_cautionMsgEN g_cautionMsgFR g_cautionMsgDE g_cautionMsgIT g_cautionMsgES # RUUP01
        g_cautionMsgJP                                                            # RUUJ01
        g_cautionMsgKR                                                            # RUUK01
)
set(CF_SYMBOLS_FILE "${PROJECT_SOURCE_DIR}/config/${CF_VERSION}/symbols.txt")
set(CF_DOL_BLOBS_FILE "${CMAKE_BINARY_DIR}/generated/cf/dol_blobs.cpp")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/cf")
execute_process(
        COMMAND "${Python3_EXECUTABLE}" tools/gen_pc_dol_data.py "${CF_SYMBOLS_FILE}" "${CF_DOL_BLOBS_FILE}"
                ${CF_DOL_BLOBS}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE _cf_dol_result
        OUTPUT_VARIABLE _cf_dol_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT _cf_dol_result EQUAL 0)
    message(FATAL_ERROR "tools/gen_pc_dol_data.py failed (${_cf_dol_result})")
endif ()
message(STATUS "suitcase: ${_cf_dol_output}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CF_SYMBOLS_FILE}" "${PROJECT_SOURCE_DIR}/tools/gen_pc_dol_data.py")

# The save file's byte-order map (src/cf/game/save_codec.h), generated from the save structs'
# declarations at build time, so it follows the headers
set(CF_SAVE_MAP_FILE "${CMAKE_BINARY_DIR}/generated/cf/save_map.cpp")
file(GLOB_RECURSE CF_SAVE_MAP_HEADERS CONFIGURE_DEPENDS include/game/*.hpp include/game/*.h include/lib/*.h include/lib/*.hpp)
add_custom_command(
        OUTPUT "${CF_SAVE_MAP_FILE}"
        COMMAND "${Python3_EXECUTABLE}" tools/gen_save_swap.py "${PROJECT_SOURCE_DIR}" "${CF_SAVE_MAP_FILE}"
        DEPENDS tools/gen_save_swap.py ${CF_SAVE_MAP_HEADERS}
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Generating the save file map"
        VERBATIM
)

cf_add_pc_decomp_library(cf_game_pc ${CF_GAME_PC_SOURCES} "${CF_DOL_BLOBS_FILE}" "${CF_SAVE_MAP_FILE}")
target_include_directories(cf_game_pc PRIVATE src/cf/game)

# Settings (src/cf/include/cf/config.h): borealis::config vars in the user directory's config.json
file(GLOB CF_CONFIG_SOURCES CONFIGURE_DEPENDS src/cf/config/*.cpp)
cf_add_pc_decomp_library(cf_config ${CF_CONFIG_SOURCES})
target_link_libraries(cf_config PUBLIC borealis::config PRIVATE cf_input)

# The ImGui debug interface (src/cf/include/cf/debug.h), drawn by main.cpp each frame
file(GLOB CF_DEBUG_SOURCES CONFIGURE_DEPENDS src/cf/debug/*.cpp)
cf_add_pc_decomp_library(cf_debug ${CF_DEBUG_SOURCES})
target_link_libraries(cf_debug PRIVATE cf_config cf_input nlohmann_json::nlohmann_json)

configure_file(src/cf/version_config.h.in "${CMAKE_BINARY_DIR}/generated/cf/version_config.h" @ONLY)

# The icon (res/icon.png), embedded for the window on every platform (main.cpp); Windows also gets
# res/icon.ico in the executable's resources, for Explorer and the taskbar
file(READ res/icon.png _cf_icon_hex HEX)
string(REPEAT "[0-9a-f]" 32 _cf_icon_line) # 16 bytes a line
string(REGEX REPLACE "(${_cf_icon_line})" "\\1\n" _cf_icon_hex "${_cf_icon_hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," CF_ICON_PNG_BYTES "${_cf_icon_hex}")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/cf/icon_png.h" CONTENT
        "#pragma once\n\n// res/icon.png (generated by CMakeLists.txt)\nconstexpr unsigned char kIconPng[] = {\n@CF_ICON_PNG_BYTES@};\n"
        @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/res/icon.png")

add_executable(suitcase src/cf/main.cpp)
if (WIN32)
    target_sources(suitcase PRIVATE res/suitcase.rc)
    set_property(SOURCE res/suitcase.rc APPEND PROPERTY OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/res/icon.ico")
endif ()
set_target_properties(suitcase PROPERTIES OUTPUT_NAME "Suitcase-${CF_VERSION}")
if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # No identical code folding (on by default when linking without /DEBUG, as Release does): the game
    # tells states apart by their functions' addresses (dState's mode functions and the like), which
    # folding merges when two functions compile to the same code (../pc_port_ub.md PC-27)
    target_link_options(suitcase PRIVATE /OPT:NOICF)
elseif (CF_STRIP_DEBUG_INFO AND NOT APPLE)
    # --strip-debug rather than -s: borealis::crash's backtraces keep their function names
    target_link_options(suitcase PRIVATE -Wl,--strip-debug)
endif ()
target_include_directories(suitcase PRIVATE src/cf/include "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(suitcase PRIVATE
        aurora::core aurora::gx aurora::gd aurora::main aurora::vi aurora::os aurora::dvd aurora::mtx
        aurora::pad aurora::si
        borealis::log
        cf_cw_compat
)
include(extern/aurora/cmake/AuroraCopyRuntimeDLLs.cmake)
aurora_copy_runtime_dlls(suitcase)
if (CF_BUILD_GAME)
    foreach (target IN LISTS CF_UNIT_TARGETS)
        if (TARGET ${target})
            target_link_libraries(suitcase PRIVATE ${target})
        endif ()
    endforeach ()
    target_link_libraries(suitcase PRIVATE cf_game_pc cf_sdk_stubs cf_input cf_config cf_debug)
    target_compile_definitions(suitcase PRIVATE CF_BUILD_GAME=1)
endif ()
