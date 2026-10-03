file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/bad.elf" "not an ELF")
# Exercise paths with spaces on every platform.
configure_file("${FIXTURE}" "${WORK}/fixture with spaces.elf" COPYONLY)
# Make a valid ELF whose first instruction is SYNC with a nonzero reserved rd.
# Use an explicitly invalid encoding rather than an opcode awaiting implementation.
execute_process(COMMAND "${PYTHON}" -c
  "from pathlib import Path; import sys; image=bytearray(Path(sys.argv[1]).read_bytes()); image[0x100:0x104]=(0x0000080f).to_bytes(4,'little'); Path(sys.argv[2]).write_bytes(image)"
  "${FIXTURE}" "${WORK}/invalid-opcode.elf" RESULT_VARIABLE fixture_code)
if(NOT fixture_code EQUAL 0)
  message(FATAL_ERROR "Could not create invalid-opcode ELF")
endif()
execute_process(COMMAND "${CLI}" --elf "${WORK}/invalid-opcode.elf" --steps 1 --trace
  RESULT_VARIABLE fault_code OUTPUT_VARIABLE fault_output ERROR_VARIABLE fault_error TIMEOUT 20)
if(NOT fault_code EQUAL 1
   OR NOT fault_error MATCHES "pc=0x00100000 opcode=0x0000080f: unsupported"
   OR NOT fault_output MATCHES "retired=0 pc=0x00100000 stopped")
  message(FATAL_ERROR "CPU fault CLI contract failed: ${fault_code}: ${fault_output} / ${fault_error}")
endif()
function(run expected_code expected_text)
  execute_process(COMMAND "${CLI}" ${ARGN} RESULT_VARIABLE code
    OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
  if(NOT "${code}" STREQUAL "${expected_code}")
    message(FATAL_ERROR "${ARGN}: exit ${code}, expected ${expected_code}: ${output} / ${error}")
  endif()
  if(expected_code EQUAL 0)
    if(NOT error STREQUAL "" OR NOT output MATCHES "${expected_text}")
      message(FATAL_ERROR "${ARGN}: unexpected output: ${output} / ${error}")
    endif()
  elseif(NOT error MATCHES "${expected_text}")
    message(FATAL_ERROR "${ARGN}: missing error: ${error}")
  endif()
endfunction()

run(0 "ELF loaded: entry=0x00100000 segments=2 file-bytes=84 memory-bytes=116"
  --elf "${FIXTURE}" --steps 0)
run(0 "retired=100 pc=0x00100048 budget-exhausted"
  --elf "${WORK}/fixture with spaces.elf" --steps 100)
run(0 "inspect.0x00101010.=0x4b4e4c43" --elf "${FIXTURE}" --steps 34 --inspect 0x00101010)
run(0 "inspect.0x00101014.=0x0000000f" --elf "${FIXTURE}" --steps 100 --inspect 0x00101014)
run(0 "inspect.0x00101018.=0x00000005" --elf "${FIXTURE}" --inspect 0x00101018)
run(0 "inspect.0x0010101c.=0x12345678" --elf "${FIXTURE}" --inspect 0x0010101c)
run(0 "inspect.0x00101020.=0x00000001" --elf "${FIXTURE}" --inspect 0x00101020)
run(0 "inspect.0x00101010.=0x00000000" --elf "${FIXTURE}" --steps 0 --inspect 0x00101010)
run(0 "pc=0x0010001c opcode=0x24630001 delay-slot retired" --elf "${FIXTURE}" --steps 34 --trace)
run(1 "cannot open ELF file" --elf "${WORK}/missing.elf")
run(1 "ELF:" --elf "${WORK}/bad.elf")
run(1 "unsupported EE hardware register" --elf "${FIXTURE}" --inspect 0x9000f020)
run(2 "Usage:" --elf)
run(2 "Usage:" --elf "${FIXTURE}" --steps)
run(2 "Usage:" --elf "${FIXTURE}" --trace --trace)
run(2 "Usage:" --elf "${FIXTURE}" --steps 1 --steps 2)
run(2 "Usage:" --elf "${FIXTURE}" --unknown)
run(2 "Invalid step budget" --elf "${FIXTURE}" --steps -1)
run(2 "Invalid step budget" --elf "${FIXTURE}" --steps 100001)
run(2 "Invalid inspect address" --elf "${FIXTURE}" --inspect 0x100001)
run(2 "Invalid inspect address" --elf "${FIXTURE}" --inspect 0x100000000)
run(2 "Invalid inspect address" --elf "${FIXTURE}" --inspect 0x)

# Identical executions must produce byte-for-byte identical host output.
execute_process(COMMAND "${CLI}" --elf "${FIXTURE}" --steps 100 --trace --inspect 0x00101010
  RESULT_VARIABLE first_code OUTPUT_VARIABLE first ERROR_VARIABLE first_error TIMEOUT 20)
execute_process(COMMAND "${CLI}" --elf "${FIXTURE}" --steps 100 --trace --inspect 0x00101010
  RESULT_VARIABLE second_code OUTPUT_VARIABLE second ERROR_VARIABLE second_error TIMEOUT 20)
if(NOT first_code EQUAL 0 OR NOT second_code EQUAL 0 OR NOT first STREQUAL second
   OR NOT first_error STREQUAL "" OR NOT second_error STREQUAL "")
  message(FATAL_ERROR "ELF CLI replay was not deterministic")
endif()
