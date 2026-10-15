# Tiny self-contained test runner (tests/check.h); no framework dependency.
foreach(t irc_format command_parser irc_message irc_session completion msl)
    add_executable(test_${t} test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE lurker_core)
    target_compile_options(test_${t} PRIVATE ${LURKER_WARNINGS})
    add_test(NAME ${t} COMMAND test_${t})
endforeach()

# Oracle regression: our MSL engine must still match the captured mIRC 7.85
# behaviour for every committed case (tools/oracle/ explains how the fixture is
# produced). engine-probe is defined in the top-level CMakeLists.
add_test(NAME oracle_eval COMMAND ${CMAKE_COMMAND}
    -DPROBE=$<TARGET_FILE:engine-probe>
    -DCASES=${CMAKE_CURRENT_SOURCE_DIR}/oracle/eval/cases.txt
    -DEXPECT=${CMAKE_CURRENT_SOURCE_DIR}/oracle/eval/mirc.txt
    -DOUT=${CMAKE_CURRENT_BINARY_DIR}/oracle_eval_out.txt
    -P ${CMAKE_CURRENT_SOURCE_DIR}/oracle_diff.cmake)
