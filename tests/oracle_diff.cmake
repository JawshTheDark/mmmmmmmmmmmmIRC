# Runs engine-probe on a cases file and byte-compares its output against the
# committed mIRC fixture. Driven by the `oracle_eval` test (see CMakeLists.txt).
# Expects -DPROBE= -DCASES= -DEXPECT= -DOUT= on the command line.
execute_process(COMMAND "${PROBE}" "${CASES}" "${OUT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "engine-probe exited with ${rc}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${EXPECT}" "${OUT}"
                RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
    message(FATAL_ERROR
        "engine output differs from the mIRC oracle fixture.\n"
        "  expected: ${EXPECT}\n  actual:   ${OUT}\n"
        "Run: diff ${EXPECT} ${OUT}")
endif()
