cmake_minimum_required(VERSION 3.16)
project(CacheSim VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The experiments simulate billions of accesses, so default to an optimised build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Put the programs directly in the build directory on every generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${config} config_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR})
endforeach()

# MinGW: link the runtime statically so the programs run outside an MSYS2 shell.
if(MINGW)
  add_link_options(-static)
endif()

add_library(cachesim_core STATIC
  src/cache.cpp
  src/options.cpp
  src/trace.cpp
)
target_include_directories(cachesim_core PUBLIC include)

add_executable(cachesim src/cachesim_main.cpp)
target_link_libraries(cachesim PRIVATE cachesim_core)

add_executable(tracegen src/tracegen_main.cpp)
target_link_libraries(tracegen PRIVATE cachesim_core)

option(CACHESIM_BUILD_TESTS "Build the unit tests" ON)
if(CACHESIM_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
