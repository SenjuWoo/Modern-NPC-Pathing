# StepUpOnto SKSE - NPC Pathing patch. Built from the repository root CMakeLists
# (add_subdirectory), so it shares the root's CommonLibSSE-NG build instead of compiling it twice.
if(NOT TARGET CommonLibSSE::CommonLibSSE)
    message(FATAL_ERROR "Build this patch from the repository root: cmake --preset windows-msvc")
endif()

add_library(StepUpOntoSKSE SHARED
    src/PCH.h
    src/main.cpp
    src/Settings.cpp
    src/Menu.cpp
    src/Hooks.cpp
    src/Raycast.cpp
    src/StepUpManager.cpp)

target_precompile_headers(StepUpOntoSKSE PRIVATE src/PCH.h)
target_link_libraries(StepUpOntoSKSE PRIVATE CommonLibSSE::CommonLibSSE)
target_include_directories(StepUpOntoSKSE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(StepUpOntoSKSE PRIVATE _UNICODE UNICODE)
target_compile_options(StepUpOntoSKSE PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:/permissive-;/Zc:preprocessor;/Zc:__cplusplus;/utf-8;/EHsc;/Gy;/Zi;/external:anglebrackets;/external:W0;/Brepro>")
target_link_options(StepUpOntoSKSE PRIVATE /INCREMENTAL:NO /OPT:REF /OPT:ICF /DEBUG:FULL /Brepro /PDBALTPATH:StepUpOntoSKSE.pdb)

# Staged beside the other build outputs, never into package/ (it ships only through the FOMOD).
set(STEPUP_OUT "${CMAKE_BINARY_DIR}/patch_stepuponto/SKSE/Plugins")
set_target_properties(StepUpOntoSKSE PROPERTIES
    OUTPUT_NAME "StepUpOntoSKSE"
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY "${STEPUP_OUT}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${STEPUP_OUT}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${STEPUP_OUT}"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")
