set(CMAKE_AUTOMOC ON)

qt_add_executable(lurker-desktop
    main.cpp
    MainWindow.cpp
    MainWindow.h
    ChatView.cpp
    ChatView.h
    ConnectDialog.cpp
    ConnectDialog.h
    FormattedText.cpp
    FormattedText.h
    GuiScriptHost.h
)
target_link_libraries(lurker-desktop PRIVATE
    lurker_core
    lurker_backend
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
)
target_compile_definitions(lurker-desktop PRIVATE LURKER_DESKTOP_VERSION="${PROJECT_VERSION}")
target_compile_options(lurker-desktop PRIVATE -Wall -Wextra -Wpedantic)
