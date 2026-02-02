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
};

}

#endif // CLAUDESESSIONYOLOTEST_H
