cmake_minimum_required(VERSION 3.21)
project(mmmmmmmmmmmmirc VERSION 0.1.92 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Portable warning flags: GCC/Clang get the full set; MSVC (Windows) gets /W3,
# since -Wextra/-Wpedantic are not valid cl.exe options.
if(MSVC)
    set(LURKER_WARNINGS /W3)
else()
    set(LURKER_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# The protocol-independent logic (formatting codes, slash commands) is plain
# standard C++ with no Qt, so it builds and is tested on its own. The GUI client
# is only configured when Qt 6 is available.
option(LURKER_BUILD_GUI "Build the Qt GUI client" ON)

add_library(lurker_core STATIC
    src/core/IrcFormat.cpp
    src/core/CommandParser.cpp
    src/core/IrcMessage.cpp
    src/core/IrcSession.cpp
    src/core/Completion.cpp
    src/core/msl/Interpreter.cpp
    src/core/msl/Builtins.cpp
    src/core/msl/Dialog.cpp
    src/core/msl/Hash.cpp
)
target_include_directories(lurker_core PUBLIC src)
target_compile_options(lurker_core PRIVATE ${LURKER_WARNINGS})

# engine-probe: evaluate oracle cases with our MSL engine (diff vs real mIRC).
add_executable(engine-probe tools/oracle/engine_probe.cpp)
target_link_libraries(engine-probe PRIVATE lurker_core)
target_compile_options(engine-probe PRIVATE ${LURKER_WARNINGS})

enable_testing()
add_subdirectory(tests)

if(LURKER_BUILD_GUI)
    find_package(Qt6 6.4 QUIET COMPONENTS Widgets Network WebSockets)
    if(Qt6_FOUND)
        add_subdirectory(src/backend)
        add_subdirectory(src/gui)
    else()
        message(STATUS "Qt 6 (Widgets, Network, WebSockets) not found: building core and tests only")
    endif()
endif()
