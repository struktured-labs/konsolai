/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEAGENTTEAMTEST_H
#define CLAUDEAGENTTEAMTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeAgentTeamTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // Hook event parsing — SubagentStart
    void testSubagentStartSignal();
    void testSubagentStartAlternateField();
    void testSubagentStartDerivedTranscriptPath();

    // Hook event parsing — SubagentStop
    void testSubagentStopSignal();
    void testSubagentStopWithTranscript();

    // Hook event parsing — TeammateIdle
    void testTeammateIdleSignal();
    void testTeammateIdleAlternateNameField();

    // Hook event parsing — TaskCompleted
    void testTaskCompletedSignal();
    void testTaskCompletedAlternateFields();

    // Team detection
    void testHasActiveTeamInitiallyFalse();
    void testHasActiveTeamAfterSubagentStart();
    void testHasActiveTeamAfterAllSubagentsStop();
    void testSubagentsMapTracking();

    // Multiple subagents
    void testMultipleSubagents();

    // Unknown events still ignored
    void testUnknownEventNoSubagentSignal();
};

}

#endif // CLAUDEAGENTTEAMTEST_H
