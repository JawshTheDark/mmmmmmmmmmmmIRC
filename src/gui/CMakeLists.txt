set(CMAKE_AUTOMOC ON)

qt_add_executable(lurker-desktop
    main.cpp
    MainWindow.cpp
    MainWindow.h
    FormattedText.cpp
    FormattedText.h
)
target_link_libraries(lurker-desktop PRIVATE
    lurker_core
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
)
target_compile_definitions(lurker-desktop PRIVATE LURKER_DESKTOP_VERSION="${PROJECT_VERSION}")
target_compile_options(lurker-desktop PRIVATE -Wall -Wextra -Wpedantic)
