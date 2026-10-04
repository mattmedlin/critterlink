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

add_library(critterlink_core STATIC src/machine.cpp src/cache.cpp src/mmu.cpp src/memory.cpp src/cpu.cpp src/elf.cpp
  src/scheduler.cpp src/graphics.cpp src/peripherals.cpp src/hardware.cpp src/system.cpp src/hardware_demo.cpp
  src/interrupt_demo.cpp src/vector.cpp src/vector_demo.cpp src/iop.cpp src/sif.cpp src/iop_demo.cpp
  src/spu.cpp src/spu_demo.cpp src/sio2.cpp src/cdvd.cpp src/io_demo.cpp src/integrated_demo.cpp)
target_include_directories(critterlink_core PUBLIC include)
critterlink_target(critterlink_core)

add_executable(critterlink src/main.cpp)
target_link_libraries(critterlink PRIVATE critterlink_core)
critterlink_target(critterlink)

option(CRITTERLINK_BUILD_FIXTURE "Build the redistributable homebrew ELF (requires Python 3)" ON)
if(CRITTERLINK_BUILD_FIXTURE)
  find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
  set(HOMEBREW_ELF "${CMAKE_CURRENT_BINARY_DIR}/fixtures/sum.elf")
  add_custom_command(OUTPUT "${HOMEBREW_ELF}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sum/build.py"
      --output "${HOMEBREW_ELF}"
    DEPENDS fixtures/sum/build.py fixtures/sum/sum.S
    VERBATIM)
  add_custom_target(homebrew_fixture ALL DEPENDS "${HOMEBREW_ELF}")
endif()

if(BUILD_TESTING)
  foreach(subsystem IN ITEMS ucab cache mmu boot_rom cpu_fpu_sqrt cpu_fpu_divide cpu_fpu_addsub cpu_fpu_minmax cpu_fpu_convert cpu_fpu_compare cpu_fpu_branch cpu_fpu_register cpu_divide_zero cpu_mmi_audit cpu_packed_halfword cpu_packed_accumulate cpu_packed_hilo cpu_packed_permute cpu_packed_arithmetic cpu_packed cpu_sa cpu_plzcw cpu_sync gif_fifo cpu_merge cpu_quadword cpu_trap cpu_hilo cpu_branch cpu_scalar scheduler graphics peripherals hardware system cop0 interrupt vector vif_dma vector_system iop sif iop_system spu spu_system sio2 cdvd io_system integrated_system)
    add_executable(critterlink_${subsystem}_tests tests/${subsystem}_test.cpp)
    target_link_libraries(critterlink_${subsystem}_tests PRIVATE critterlink_core)
    critterlink_target(critterlink_${subsystem}_tests)
    add_test(NAME ${subsystem} COMMAND critterlink_${subsystem}_tests)
  endforeach()
  add_executable(critterlink_tests tests/machine_test.cpp)
  target_link_libraries(critterlink_tests PRIVATE critterlink_core)
  critterlink_target(critterlink_tests)
  add_test(NAME core COMMAND critterlink_tests)
  add_executable(critterlink_cpu_tests tests/cpu_test.cpp)
  target_link_libraries(critterlink_cpu_tests PRIVATE critterlink_core)
  critterlink_target(critterlink_cpu_tests)
  add_test(NAME cpu_memory COMMAND critterlink_cpu_tests)
  if(CRITTERLINK_BUILD_FIXTURE)
    add_executable(critterlink_elf_tests tests/elf_test.cpp)
    target_link_libraries(critterlink_elf_tests PRIVATE critterlink_core)
    critterlink_target(critterlink_elf_tests)
    add_dependencies(critterlink_elf_tests homebrew_fixture)
    add_test(NAME elf_loader COMMAND critterlink_elf_tests "${HOMEBREW_ELF}")
    add_test(NAME elf_cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:critterlink> -DFIXTURE=${HOMEBREW_ELF}
      -DPYTHON=${Python3_EXECUTABLE}
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/elf-cli-tests
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/elf_cli_test.cmake)
    add_test(NAME fixture_reproducible COMMAND ${CMAKE_COMMAND}
      -DPYTHON=${Python3_EXECUTABLE} -DBUILDER=${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sum/build.py
      -DFIXTURE=${HOMEBREW_ELF} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture_test.cmake)
    add_test(NAME boot_rom_cli COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:critterlink> -DPYTHON=${Python3_EXECUTABLE}
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/boot-rom-cli-tests
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/boot_rom_cli_test.cmake)
  endif()
  add_test(NAME cli_contract COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:critterlink> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.cmake)
endif()
