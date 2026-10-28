cmake_minimum_required(VERSION 3.25)
project(NPCPathingNG VERSION 3.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "NPC Pathing NG builds with MSVC only (CommonLibSSE-NG ABI).")
endif()

# Static CRT (/MT): the DLL carries its own C++ runtime, so it does not depend on which
# Visual C++ Redistributable a player has installed (a DLL built with a new toolset can fail to
# load against an old redist). The vcpkg triplet is x64-windows-static.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Game-independent math (arc solver). Unit-tested without the game.
# ---------------------------------------------------------------------------
add_library(NPNGCore INTERFACE)
target_include_directories(NPNGCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(NPNGCore INTERFACE cxx_std_23)

option(NPNG_BUILD_TESTS "Build the game-independent unit tests" ON)
if(NPNG_BUILD_TESTS)
    enable_testing()
    add_executable(NPNGTests tests/test_arc.cpp)
    target_link_libraries(NPNGTests PRIVATE NPNGCore)
    target_compile_options(NPNGTests PRIVATE /W4 /WX /permissive- /Zc:preprocessor /utf-8 /EHsc)
    add_test(NAME NPNGTests COMMAND NPNGTests)
endif()

# ---------------------------------------------------------------------------
# CommonLibSSE-NG (alandtse fork, pinned by git tag in BUILDING.md).
# One DLL for SE 1.5.97, AE 1.6.x / 1.7.x and VR (VR loads inactive).
# ---------------------------------------------------------------------------
set(NPNG_COMMONLIB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE" CACHE PATH
    "CommonLibSSE-NG source tree")
if(NOT EXISTS "${NPNG_COMMONLIB_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "CommonLibSSE-NG not found at ${NPNG_COMMONLIB_ROOT}. See BUILDING.md.")
endif()

# Source paths compiled into the DLL (assert/log source locations, here and in CommonLibSSE-NG)
# are made relative to this repository, so the shipped binary does not name the builder's
# folders. One option holds the whole path (it may contain spaces); no trailing backslash.
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" NPNG_ROOT_NATIVE)
string(REGEX REPLACE "\\+$" "" NPNG_ROOT_NATIVE "${NPNG_ROOT_NATIVE}")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/d1trimfile:${NPNG_ROOT_NATIVE}>")

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR ON CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
# Vtable swaps only: no branch patches, no trampoline, no xbyak.
set(SKSE_SUPPORT_XBYAK OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)
set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)
add_subdirectory("${NPNG_COMMONLIB_ROOT}" "${CMAKE_BINARY_DIR}/commonlib" EXCLUDE_FROM_ALL SYSTEM)

configure_file(src/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)

add_library(${PROJECT_NAME} SHARED
    src/PCH.h
    src/arc.h
    src/budget.h
    src/version.h
    src/main.cpp
    src/hooks.cpp
    src/hooks.h
    src/compat.cpp
    src/compat.h
    src/settings.cpp
    src/settings.h
    src/raycast.cpp
    src/raycast.h
    src/pathing.cpp
    src/pathing.h
    src/npc_parkour.cpp
    src/npc_parkour.h
    src/evg_traversal.cpp
    src/evg_traversal.h
    src/leap.cpp
    src/leap.h
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc")

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_link_libraries(${PROJECT_NAME} PRIVATE NPNGCore CommonLibSSE::CommonLibSSE)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    NPNG_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    NPNG_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    NPNG_VERSION_PATCH=${PROJECT_VERSION_PATCH})
# /Brepro: deterministic PE time stamp. SKSE 2.3+ rejects an Address Library plugin
# without the V5 flag (set in main.cpp) unless the stamp is recent; /Brepro hashes it.
target_compile_options(${PROJECT_NAME} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/W4;/permissive-;/Zc:preprocessor;/Zc:__cplusplus;/utf-8;/EHsc;/Gy;/Zi;/external:anglebrackets;/external:W0;/Brepro>")
target_link_options(${PROJECT_NAME} PRIVATE /INCREMENTAL:NO /OPT:REF /OPT:ICF /DEBUG:FULL /Brepro /PDBALTPATH:${PROJECT_NAME}.pdb)

set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/package/Data/SKSE/Plugins")
set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "NPCPathingNG"
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/debug"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")

# Companion build of StepUpOnto SKSE (SkyParkour-aware). Shipped by the FOMOD only when
# StepUpOntoSKSE.dll is detected. Shares the CommonLibSSE target above.
option(NPNG_BUILD_STEPUP_PATCH "Build the StepUpOnto SKSE compatibility patch" ON)
if(NPNG_BUILD_STEPUP_PATCH)
    add_subdirectory(patches/StepUpOntoSKSE-V2)
endif()
