# Tiny self-contained test runner (tests/check.h); no framework dependency.
foreach(t irc_format command_parser irc_message irc_session)
    add_executable(test_${t} test_${t}.cpp)
    target_link_libraries(test_${t} PRIVATE lurker_core)
    target_compile_options(test_${t} PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${t} COMMAND test_${t})
endforeach()
