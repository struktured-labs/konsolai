/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEPROCESSTEST_H
#define CLAUDEPROCESSTEST_H

#include <QObject>

namespace Konsolai
{

class ClaudeProcessTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // State tests
    void testInitialState();
    void testStateTransitions();
    void testIsRunning();

    // Model tests
    void testModelName();
    void testParseModel();
    void testModelNameRoundTrip();

    // Command building tests
    void testBuildCommand();
    void testBuildCommandWithModel();
    void testBuildCommandWithWorkingDir();
    void testBuildCommandWithArgs();

    // Task management tests
    void testSetCurrentTask();
    void testClearTask();

    // Hook event handling tests
    void testHandleHookEventStop();
    void testHandleHookEventNotification();
    void testHandleHookEventPreToolUse();
    void testHandleHookEventPostToolUse();

    // Signal tests
    void testStateChangedSignal();
    void testTaskSignals();

    // State machine edge cases
    void testSameStateNoSignal();
    void testRapidStateTransitions();
    void testUnknownEventType();
    void testMalformedJsonEvent();
    void testEmptyEventData();
    void testIsRunningAllStates();

    // Notification event variants
    void testNotificationPermissionRequest();
    void testNotificationIdlePrompt();
    void testNotificationGeneric();

    // PermissionRequest event
    void testPermissionRequestSignals();
    void testPermissionRequestEmptyToolName();

    // PreToolUse task description
    void testPreToolUseSetsTask();
};

}

#endif // CLAUDEPROCESSTEST_H
