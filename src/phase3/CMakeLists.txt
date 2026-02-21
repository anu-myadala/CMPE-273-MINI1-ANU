add_executable(phase3 main.cpp)

# Vectorization flags: GCC uses -fopt-info-vec, Clang uses -Rpass=loop-vectorize
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(VEC_INFO_FLAG "-fopt-info-vec")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(VEC_INFO_FLAG "-Rpass=loop-vectorize")
else()
    set(VEC_INFO_FLAG "")
endif()

target_compile_options(phase3 PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -ftree-vectorize ${VEC_INFO_FLAG}>
    $<$<CONFIG:Debug>:-g -Wall -Wextra>)

if(OpenMP_CXX_FOUND)
    target_link_libraries(phase3 PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(phase3 PRIVATE _OPENMP_AVAILABLE)
else()
    message(WARNING "OpenMP not found — Phase 3 will run single-threaded")
endif()
