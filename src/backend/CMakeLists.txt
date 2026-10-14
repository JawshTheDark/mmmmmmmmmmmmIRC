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
