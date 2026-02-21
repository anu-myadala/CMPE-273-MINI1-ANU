add_executable(phase1 main.cpp)
target_compile_options(phase1 PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native>
    $<$<CONFIG:Debug>:-g -Wall -Wextra>)
