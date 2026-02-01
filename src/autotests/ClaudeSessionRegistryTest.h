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
};

}

#endif // CLAUDESESSIONREGISTRYTEST_H
