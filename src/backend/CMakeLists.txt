set(CMAKE_AUTOMOC ON)

qt_add_library(lurker_backend STATIC
    BouncerBackend.cpp
    BouncerBackend.h
    LurkerClient.cpp
    LurkerClient.h
    ChatBackend.h
    LurkerChatBackend.cpp
    LurkerChatBackend.h
    BouncerChatBackend.cpp
    BouncerChatBackend.h
    MultiplexBackend.cpp
    MultiplexBackend.h
)
target_include_directories(lurker_backend PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lurker_backend PUBLIC lurker_core Qt6::Network Qt6::WebSockets)
target_compile_options(lurker_backend PRIVATE ${LURKER_WARNINGS})

# Headless dev probes.
qt_add_executable(lurker-bouncer-probe probe_main.cpp)
target_link_libraries(lurker-bouncer-probe PRIVATE lurker_backend)
target_compile_options(lurker-bouncer-probe PRIVATE ${LURKER_WARNINGS})

qt_add_executable(lurker-ws-probe ws_probe_main.cpp)
target_link_libraries(lurker-ws-probe PRIVATE lurker_backend)
target_compile_options(lurker-ws-probe PRIVATE ${LURKER_WARNINGS})

# MultiplexBackend unit test (net-id namespacing + routing); Qt but no network.
qt_add_executable(test_multiplex test_multiplex.cpp)
target_link_libraries(test_multiplex PRIVATE lurker_backend)
target_include_directories(test_multiplex PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_options(test_multiplex PRIVATE ${LURKER_WARNINGS})
add_test(NAME multiplex COMMAND test_multiplex)
