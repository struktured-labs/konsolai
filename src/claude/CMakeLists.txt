# Claude integration module for Konsolai

set(claude_SRCS
    TmuxManager.cpp
    ClaudeProcess.cpp
    ClaudeSession.cpp
    ClaudeHookHandler.cpp
    NotificationManager.cpp
    ClaudeNotificationWidget.cpp
)

add_library(konsolai_claude STATIC ${claude_SRCS})

target_link_libraries(konsolai_claude
    PUBLIC
    Qt::Core
    Qt::Network
    Qt::Widgets
    Qt::Multimedia
    KF6::CoreAddons
    KF6::Notifications
    KF6::StatusNotifierItem
    KF6::I18n
)

target_include_directories(konsolai_claude
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

# Hook handler binary
# This is called by Claude hooks to send events to Konsolai
add_executable(konsolai-hook-handler
    tools/konsolai-hook-handler.cpp
)

target_link_libraries(konsolai-hook-handler
    Qt::Core
    Qt::Network
)

install(TARGETS konsolai-hook-handler ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
