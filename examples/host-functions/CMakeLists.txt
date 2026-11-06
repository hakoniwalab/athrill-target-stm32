cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(athrill_host_functions VERSION 1.0.0 LANGUAGES C)
  set(ATHRILL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../athrill" CACHE PATH
    "Athrill core checkout")
endif()

add_library(athrill_host_functions SHARED host_functions.c)
target_include_directories(athrill_host_functions PRIVATE "${ATHRILL_SOURCE_DIR}/src/inc")
set_target_properties(athrill_host_functions PROPERTIES
  C_STANDARD 99 C_STANDARD_REQUIRED YES
  C_VISIBILITY_PRESET hidden
  OUTPUT_NAME athrill_host_functions)
