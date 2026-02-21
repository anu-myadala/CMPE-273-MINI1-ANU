add_executable(phase2 main.cpp)
target_compile_options(phase2 PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native>
    $<$<CONFIG:Debug>:-g -Wall -Wextra>)

if(OpenMP_CXX_FOUND)
    target_link_libraries(phase2 PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(phase2 PRIVATE _OPENMP_AVAILABLE)
else()
    message(WARNING "OpenMP not found — Phase 2 will run single-threaded")
endif()
