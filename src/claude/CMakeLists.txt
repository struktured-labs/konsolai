# Claude integration module for Konsolai

set(dbus_xml_srcs)
if(HAVE_DBUS)
    # Generate D-Bus adaptor for Claude interface
    qt_add_dbus_adaptor(
        claudeadaptors_SRCS
        org.kde.konsolai.Claude.xml
        ClaudeSession.h
        Konsolai::ClaudeSession
        ClaudeSessionAdaptor
        claudesessionadaptor
    )
    set(dbus_xml_srcs ${claudeadaptors_SRCS})
endif()

set(claude_SRCS
    TmuxManager.cpp
    ClaudeProcess.cpp
    ClaudeSession.cpp
    ClaudeHookHandler.cpp
    NotificationManager.cpp
    ClaudeNotificationWidget.cpp
    ClaudeSessionState.cpp
    ClaudeSessionRegistry.cpp
    ClaudeStatusWidget.cpp
    ClaudeMenu.cpp
    ClaudeTabIndicator.cpp
    ClaudeSessionWizard.cpp
    ${dbus_xml_srcs}
)

add_library(konsolai_claude STATIC ${claude_SRCS})
set_target_properties(konsolai_claude PROPERTIES POSITION_INDEPENDENT_CODE ON)

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

if(HAVE_DBUS)
    target_link_libraries(konsolai_claude
        PUBLIC
        Qt::DBus
        KF6::DBusAddons
    )
endif()

target_include_directories(konsolai_claude
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
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
