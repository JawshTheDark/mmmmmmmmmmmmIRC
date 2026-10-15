set(CMAKE_AUTOMOC ON)

qt_add_executable(lurker-desktop
    main.cpp
    AppIcon.h
    MainWindow.cpp
    MainWindow.h
    ChatView.cpp
    ChatView.h
    ChannelListWindow.cpp
    ChannelListWindow.h
    DccTransfersWindow.cpp
    DccTransfersWindow.h
    UrlCatcherWindow.cpp
    UrlCatcherWindow.h
    PreviewService.cpp
    PreviewService.h
    ConnectDialog.cpp
    ConnectDialog.h
    FormattedText.cpp
    FormattedText.h
    GuiScriptHost.h
    ScriptDialog.cpp
    ScriptDialog.h
    Theme.cpp
    Theme.h
    ToolbarIcons.h
    Updater.cpp
    Updater.h
    PictureWindow.cpp
    PictureWindow.h
    ScriptSockets.cpp
    ScriptSockets.h
    ScriptEditor.cpp
    ScriptEditor.h
    OptionsDialog.cpp
    OptionsDialog.h
)
target_link_libraries(lurker-desktop PRIVATE
    lurker_core
    lurker_backend
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
)
target_compile_definitions(lurker-desktop PRIVATE LURKER_DESKTOP_VERSION="${PROJECT_VERSION}")
target_compile_options(lurker-desktop PRIVATE ${LURKER_WARNINGS})

# Windows: build as a GUI app so launching the .exe doesn't pop a console window.
# (macOS bundle too, for completeness.)
set_target_properties(lurker-desktop PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE OFF)
