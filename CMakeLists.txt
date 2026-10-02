cmake_minimum_required(VERSION 3.20)
project(Critterlink VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

function(critterlink_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

add_library(critterlink_core STATIC src/machine.cpp)
target_include_directories(critterlink_core PUBLIC include)
critterlink_target(critterlink_core)

add_executable(critterlink src/main.cpp)
target_link_libraries(critterlink PRIVATE critterlink_core)
critterlink_target(critterlink)

if(BUILD_TESTING)
  add_executable(critterlink_tests tests/machine_test.cpp)
  target_link_libraries(critterlink_tests PRIVATE critterlink_core)
  critterlink_target(critterlink_tests)
  add_test(NAME core COMMAND critterlink_tests)
  add_test(NAME cli_contract COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:critterlink> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake)
endif()
