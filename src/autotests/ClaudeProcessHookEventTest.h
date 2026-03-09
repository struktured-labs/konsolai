/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEPROCESSHOOKEVENTTEST_H
#define CLAUDEPROCESSHOOKEVENTTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeProcessHookEventTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // ── PreToolUse: Bash tool ──
    void testPreToolUseBashEmitsSignal();
    void testPreToolUseBashEmptyCommand();
    void testPreToolUseBashToolInputNotObject();

    // ── PreToolUse: AskUserQuestion tool ──
    void testPreToolUseAskUserQuestionEmitsSignal();
    void testPreToolUseAskUserQuestionEmptyQuestions();
    void testPreToolUseAskUserQuestionNoQuestionsField();

    // ── PreToolUse: Task tool ──
    void testPreToolUseTaskEmitsSignal();
    void testPreToolUseTaskEmptyDescription();

    // ── PreToolUse: Edit/Write/Read tools ──
    void testPreToolUseEditSetsWorking();
    void testPreToolUseWriteSetsWorking();
    void testPreToolUseReadSetsWorking();

    // ── PostToolUse: toolUseCompleted signal ──
    void testPostToolUseEmitsToolUseCompleted();
    void testPostToolUseResponseIsObject();
    void testPostToolUseResponseIsArray();
    void testPostToolUseResponseIsString();
    void testPostToolUseEmptyToolNameNoSignal();
    void testPostToolUseNoResponseField();

    // ── Stop event: taskFinished signal ──
    void testStopEmitsTaskFinished();
    void testStopFromWorkingEmitsTaskFinished();

    // ── Reset ──
    void testResetClearsStateAndTask();
    void testResetFromWorking();

    // ── PermissionRequest: tool_input variants ──
    void testPermissionRequestToolInputObject();
    void testPermissionRequestToolInputArray();
    void testPermissionRequestToolInputString();

    // ── Full tool lifecycle sequences ──
    void testPreToolUsePostToolUseStopLifecycle();
    void testMultipleToolUsesBeforeStop();
    void testBashToolLifecycleSignals();

    // ── Multi-agent scenarios ──
    void testMultipleSubagentStartStop();
    void testSubagentWithTeammateIdleAndTaskCompleted();
    void testInterleavedSubagentAndToolUse();

    // ── Permission flow sequences ──
    void testPermissionThenPreToolUseThenStop();
    void testYoloThenManualThenYoloSequence();

    // ── Rapid-fire edge cases ──
    void testRapidFireSubagentEvents();
    void testRapidFireToolUseEvents();
    void testMixedEventRapidFire();

    // ── shortModelName ──
    void testShortModelName();

    // ── Notification: permission_required variant ──
    void testNotificationPermissionRequired();

    // ── Notification: idle variant ──
    void testNotificationIdleVariant();
};

}

#endif // CLAUDEPROCESSHOOKEVENTTEST_H
