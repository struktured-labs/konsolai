/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSIONYOLOTEST_H
#define CLAUDESESSIONYOLOTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeSessionYoloTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // detectPermissionPrompt tests
    void testDetectPermissionPrompt_YesBasic();
    void testDetectPermissionPrompt_YesVerbose();
    void testDetectPermissionPrompt_AllowOnce();
    void testDetectPermissionPrompt_AlwaysAllow();
    void testDetectPermissionPrompt_CaseInsensitive();
    void testDetectPermissionPrompt_WithAnsiCodes();
    void testDetectPermissionPrompt_NoMatch_IdlePrompt();
    void testDetectPermissionPrompt_NoMatch_EmptyOutput();
    void testDetectPermissionPrompt_NoMatch_NoSelector();
    void testDetectPermissionPrompt_NoMatch_SelectorWithoutKeyword();
    void testDetectPermissionPrompt_MultiLine();

    // detectIdlePrompt tests
    void testDetectIdlePrompt_BasicCaret();
    void testDetectIdlePrompt_CaretWithSpace();
    void testDetectIdlePrompt_ProjectPrefixed();
    void testDetectIdlePrompt_NoMatch_PermissionPrompt();
    void testDetectIdlePrompt_NoMatch_AllowDenyPresent();
    void testDetectIdlePrompt_NoMatch_EmptyOutput();
    void testDetectIdlePrompt_NoMatch_WorkingOutput();
    void testDetectIdlePrompt_TrailingBlankLines();

    // PermissionRequest hook event with yolo_approved
    void testPermissionRequest_YoloApproved();
    void testPermissionRequest_NotYoloApproved();
    void testPermissionRequest_MissingYoloField();

    // State transitions for yolo scenarios
    void testStateTransition_PreToolUseToStopBecomesIdle();
    void testStateTransition_PermissionRequestWaitsInput();

    // Detection edge cases
    void testDetectPermissionPrompt_NoMatch_CrossLine();
    void testDetectPermissionPrompt_NoMatch_SelectorOnDeny();
    void testDetectIdlePrompt_NoMatch_CaretInMiddle();

    // Approval logging
    void testLogApproval_Level1();
    void testLogApproval_Level2();
    void testLogApproval_Level3();
    void testLogApproval_AppendsToLog();
    void testLogApproval_EmitsSignals();
    void testTotalApprovalCount();
    void testRestoreApprovalState();

    // Yolo mode signals
    void testSetYoloMode_EmitsSignal();
    void testSetDoubleYoloMode_EmitsSignal();
    void testSetTripleYoloMode_EmitsSignal();

    // Yolo file management
    void testSetYoloMode_CreatesAndRemovesFile();
    void testSetTripleYoloMode_CreatesAndRemovesTeamFile();

    // hasActiveTeam and subagent tracking
    void testHasActiveTeam_NoSubagents();
    void testHasActiveTeam_WithWorkingSubagent();
    void testHasActiveTeam_SubagentStoppedNoTeam();
    void testHasActiveTeam_MultipleSubagentsOneStops();
    void testSubagentTracking_TeamName();
    void testSubagentTracking_TeammateIdle();
    void testSubagentTracking_TaskCompleted();
};

}

#endif // CLAUDESESSIONYOLOTEST_H
