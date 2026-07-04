/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSIONREGISTRYTEST_H
#define CLAUDESESSIONREGISTRYTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeSessionRegistryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    // State persistence tests
    void testSaveAndLoadState();
    void testPromptPersistedInState();
    void testLastAutoContinuePromptByDirectory();
    void testLastAutoContinuePromptMostRecent();
    void testLastAutoContinuePromptNoMatch();
    void testUpdateSessionPrompt();

    // Conversation reader
    void testReadClaudeConversationsEmpty();
    void testReadClaudeConversationsParsing();

    // hashedProjectPath — must match Claude Code's on-disk normalization
    // exactly (/ AND _ AND . all collapse to -). Any regression here silently
    // breaks resume detection, token tracking, and conversation discovery.
    void testHashedProjectPath_HyphenatesSlashes();
    void testHashedProjectPath_HyphenatesUnderscores();
    void testHashedProjectPath_HyphenatesDots();
    void testHashedProjectPath_MatchesClaudeCodeDrMarioRlCase();
    void testHashedProjectPath_HandlesWorktreePath();

    // Async operations
    void testRefreshOrphanedSessionsAsyncCompletes();

    // Remote has-session pre-check (attach hardening)
    void testBuildRemoteHasSessionArgs_basic();
    void testBuildRemoteHasSessionArgs_customPort();
    void testBuildRemoteHasSessionArgs_quotesSessionName();
    void testRemoteHasSessionAsync_emptyTarget();
};

}

#endif // CLAUDESESSIONREGISTRYTEST_H
