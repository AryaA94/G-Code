# Runs EXE with ARGS and checks the exit code and that the output contains
# EXPECT_OUT. Used by the cli_* tests in CMakeLists.txt.
# ARGS arrives as a CMake list (a;b;c), so it expands to separate arguments.
execute_process(COMMAND ${EXE} ${ARGS}
                RESULT_VARIABLE rc
                OUTPUT_VARIABLE out
                ERROR_VARIABLE err)
set(all "${out}${err}")
if(NOT rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "exit code ${rc}, expected ${EXPECT_RC}\n${all}")
endif()
if(NOT "${EXPECT_OUT}" STREQUAL "")
  string(FIND "${all}" "${EXPECT_OUT}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "output does not contain \"${EXPECT_OUT}\"\n${all}")
  endif()
endif()
