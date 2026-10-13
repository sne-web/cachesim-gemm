# Generates trace files with tracegen, simulates them with cachesim, and checks
# the statistics match cachesim's built-in workload for the same parameters.
foreach(case "naive;" "tiled;--tile;4")
  list(GET case 0 algo)
  list(SUBLIST case 1 -1 tile_args)
  set(trace_file ${WORK_DIR}/pipeline_${algo}.trace)
  set(cache_args --size 2K --block 32 --assoc 2 --policy fifo)

  execute_process(
    COMMAND ${TRACEGEN} --algo ${algo} --n 20 ${tile_args} --out ${trace_file}
    RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "tracegen failed for ${algo}")
  endif()

  execute_process(
    COMMAND ${CACHESIM} ${cache_args} --trace ${trace_file} --csv
    OUTPUT_VARIABLE from_file RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "cachesim --trace failed for ${algo}")
  endif()

  execute_process(
    COMMAND ${CACHESIM} ${cache_args} --workload ${algo} --n 20 ${tile_args} --csv
    OUTPUT_VARIABLE built_in RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "cachesim --workload failed for ${algo}")
  endif()

  # Compare everything from the cache size onwards (the first three columns
  # describe the input source and legitimately differ).
  string(REGEX MATCH "2048,32,2,32,FIFO,[^\r\n]*" file_stats "${from_file}")
  string(REGEX MATCH "2048,32,2,32,FIFO,[^\r\n]*" built_in_stats "${built_in}")
  if(file_stats STREQUAL "" OR NOT file_stats STREQUAL built_in_stats)
    message(FATAL_ERROR "${algo}: trace file gave '${file_stats}' but built-in workload gave '${built_in_stats}'")
  endif()
  message(STATUS "${algo}: ${file_stats}")
  file(REMOVE ${trace_file})
endforeach()
