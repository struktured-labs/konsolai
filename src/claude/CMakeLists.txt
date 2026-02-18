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
    ClaudeConversationPicker.cpp
    SessionManagerPanel.cpp
    KonsolaiSettings.cpp
    BudgetController.cpp
    SessionObserver.cpp
    PromptQualityGate.cpp
    PromptTemplateManager.cpp
    OneShotController.cpp
    ${dbus_xml_srcs}
)

add_library(konsolai_claude OBJECT ${claude_SRCS})
set_target_properties(konsolai_claude PROPERTIES POSITION_INDEPENDENT_CODE ON)

# konsolai_claude objects are linked directly into konsoleprivate (shared lib).
# This define ensures #include <Session.h> resolves KONSOLEPRIVATE_EXPORT to
# dllexport (building) instead of dllimport (using) on Windows.
target_compile_definitions(konsolai_claude PRIVATE konsoleprivate_EXPORTS)

target_link_libraries(konsolai_claude
    PUBLIC
    Qt::Core
    Qt::Network
    Qt::Widgets
    Qt::Multimedia
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::Notifications
    KF6::I18n
)

if(HAVE_KSTATUSNOTIFIERITEM)
    target_link_libraries(konsolai_claude PUBLIC KF6::StatusNotifierItem)
endif()

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
