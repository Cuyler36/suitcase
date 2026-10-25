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

option(CF_BUILD_GAME "Compile and link the decomp sources (M1; they do not build for the host yet)" OFF)

include(cmake/CfVersion.cmake)

if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    set(DAWN_USE_WAYLAND ON CACHE BOOL "Enable support for Wayland surface" FORCE)
endif ()
set(AURORA_ENABLE_DVD ON CACHE BOOL "Enable DVD API support" FORCE)
set(AURORA_ENABLE_CARD OFF CACHE BOOL "Enable CARD API support" FORCE)
set(AURORA_ENABLE_THP OFF CACHE BOOL "Enable THP decoder" FORCE)
add_subdirectory(extern/aurora EXCLUDE_FROM_ALL)
add_subdirectory(extern/borealis EXCLUDE_FROM_ALL)

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    # Multi-character constants ('ABCD') encode identically on CW, GCC, Clang and MSVC
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wno-multichar>)
elseif (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    add_compile_options(
            $<$<COMPILE_LANGUAGE:C,CXX>:/bigobj>
            $<$<COMPILE_LANGUAGE:C,CXX>:/MP>
            $<$<COMPILE_LANGUAGE:C,CXX>:/FS>
            $<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>
            /Zc:__cplusplus
    )
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

# Stand-ins for SDK libraries the PC build does not compile. They implement the decomp's SDK
# headers, so like the decomp targets they are only built with CF_BUILD_GAME.
add_library(cf_sdk_stubs STATIC EXCLUDE_FROM_ALL src/cf/sdk/hbm_stub.cpp)
target_include_directories(cf_sdk_stubs PRIVATE include include/lib lib/common/include/global)
target_compile_definitions(cf_sdk_stubs PRIVATE TARGET_PC=1 AVOID_UB=1 REVOLUTION VERSION=${CF_VERSION_NUM})
target_link_libraries(cf_sdk_stubs PRIVATE cf_cw_compat)

configure_file(src/cf/version_config.h.in "${CMAKE_BINARY_DIR}/generated/cf/version_config.h" @ONLY)

add_executable(suitcase src/cf/main.cpp)
set_target_properties(suitcase PROPERTIES OUTPUT_NAME "suitcase-${CF_VERSION}")
target_include_directories(suitcase PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(suitcase PRIVATE
        aurora::core aurora::gx aurora::main aurora::vi
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
    target_link_libraries(suitcase PRIVATE cf_sdk_stubs)
endif ()
