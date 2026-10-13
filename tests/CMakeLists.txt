add_executable(cachesim_tests
  test_main.cpp
  test_cache.cpp
  test_matmul.cpp
  test_trace.cpp
)
target_link_libraries(cachesim_tests PRIVATE cachesim_core)

add_test(NAME unit_tests COMMAND cachesim_tests)

# End-to-end checks of the command-line programs against a hand-worked trace
# (the expected numbers are derived line by line in data/conflict.trace).
set(conflict_trace ${CMAKE_CURRENT_SOURCE_DIR}/data/conflict.trace)

add_test(NAME cli_direct_mapped
  COMMAND cachesim --size 64 --block 16 --assoc 1 --trace ${conflict_trace} --csv)
set_tests_properties(cli_direct_mapped PROPERTIES
  PASS_REGULAR_EXPRESSION "trace,0,0,64,16,1,4,LRU,6,5,1,1,5,0.83333333,4,1,3,1,0")

add_test(NAME cli_two_way
  COMMAND cachesim --size 64 --block 16 --assoc 2 --trace ${conflict_trace} --csv)
set_tests_properties(cli_two_way PROPERTIES
  PASS_REGULAR_EXPRESSION "trace,0,0,64,16,2,2,LRU,6,5,1,2,4,0.66666667,3,1,0,0,1")

# N=8 in a 1 MiB cache: only the 24 first-touch misses (see test_matmul.cpp).
add_test(NAME cli_workload_naive
  COMMAND cachesim --size 1M --workload naive --n 8 --csv)
set_tests_properties(cli_workload_naive PROPERTIES
  PASS_REGULAR_EXPRESSION "naive,8,0,1048576,64,8,2048,LRU,1088,1024,64,1064,24,")

add_test(NAME cli_rejects_bad_geometry
  COMMAND cachesim --size 100 --block 16 --workload naive --n 4)
set_tests_properties(cli_rejects_bad_geometry PROPERTIES WILL_FAIL TRUE)

# tracegen piped into cachesim must agree with the built-in workload.
add_test(NAME cli_tracegen_pipeline
  COMMAND ${CMAKE_COMMAND}
    -DTRACEGEN=$<TARGET_FILE:tracegen>
    -DCACHESIM=$<TARGET_FILE:cachesim>
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/pipeline_test.cmake)
