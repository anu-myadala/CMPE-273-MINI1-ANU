add_executable(phase3 main.cpp)
target_compile_options(phase3 PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -ftree-vectorize -fopt-info-vec>
    $<$<CONFIG:Debug>:-g -Wall -Wextra>)

if(OpenMP_CXX_FOUND)
    target_link_libraries(phase3 PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(phase3 PRIVATE _OPENMP_AVAILABLE)
else()
    message(WARNING "OpenMP not found — Phase 3 will run single-threaded")
endif()
