set(CMAKE_AUTOMOC ON)

qt_add_library(lurker_backend STATIC
    BouncerBackend.cpp
    BouncerBackend.h
)
target_include_directories(lurker_backend PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lurker_backend PUBLIC lurker_core Qt6::Network)
target_compile_options(lurker_backend PRIVATE -Wall -Wextra -Wpedantic)

# Headless dev probe: connects, signs in, reports session state, disconnects.
qt_add_executable(lurker-bouncer-probe probe_main.cpp)
target_link_libraries(lurker-bouncer-probe PRIVATE lurker_backend)
target_compile_options(lurker-bouncer-probe PRIVATE -Wall -Wextra -Wpedantic)
