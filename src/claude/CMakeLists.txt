# Claude integration module for Konsolai

set(claude_SRCS
    TmuxManager.cpp
    ClaudeProcess.cpp
    ClaudeSession.cpp
)

add_library(konsolai_claude STATIC ${claude_SRCS})

target_link_libraries(konsolai_claude
    PUBLIC
    Qt::Core
    Qt::Network
    KF6::CoreAddons
)

target_include_directories(konsolai_claude
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
