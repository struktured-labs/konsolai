/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeProcessTest.h"

// Qt
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeProcess.h"

using namespace Konsolai;

void ClaudeProcessTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeProcessTest::cleanupTestCase()
{
}

void ClaudeProcessTest::testInitialState()
{
    ClaudeProcess process;

    QCOMPARE(process.state(), ClaudeProcess::State::NotRunning);
    QVERIFY(!process.isRunning());
    QVERIFY(process.currentTask().isEmpty());
}

void ClaudeProcessTest::testStateTransitions()
{
    ClaudeProcess process;

    // Initial state
    QCOMPARE(process.state(), ClaudeProcess::State::NotRunning);

    // After receiving a hook event indicating work started
    QJsonObject workData;
    workData[QStringLiteral("task")] = QStringLiteral("Test task");
    process.handleHookEvent(QStringLiteral("PreToolUse"),
                            QString::fromUtf8(QJsonDocument(workData).toJson()));

    QCOMPARE(process.state(), ClaudeProcess::State::Working);
}

void ClaudeProcessTest::testIsRunning()
{
    ClaudeProcess process;

    // NotRunning -> not running
    QVERIFY(!process.isRunning());

    // Simulate transitioning to working state
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QVERIFY(process.isRunning());
}

void ClaudeProcessTest::testModelName()
{
    QCOMPARE(ClaudeProcess::modelName(ClaudeProcess::Model::Default), QString());
    QCOMPARE(ClaudeProcess::modelName(ClaudeProcess::Model::Opus), QStringLiteral("claude-opus-4-5"));
    QCOMPARE(ClaudeProcess::modelName(ClaudeProcess::Model::Sonnet), QStringLiteral("claude-sonnet-4"));
    QCOMPARE(ClaudeProcess::modelName(ClaudeProcess::Model::Haiku), QStringLiteral("claude-haiku"));
}

void ClaudeProcessTest::testParseModel()
{
    QCOMPARE(ClaudeProcess::parseModel(QStringLiteral("claude-opus-4-5")), ClaudeProcess::Model::Opus);
    QCOMPARE(ClaudeProcess::parseModel(QStringLiteral("claude-sonnet-4")), ClaudeProcess::Model::Sonnet);
    QCOMPARE(ClaudeProcess::parseModel(QStringLiteral("claude-haiku")), ClaudeProcess::Model::Haiku);

    // Unknown models should return Default
    QCOMPARE(ClaudeProcess::parseModel(QStringLiteral("unknown-model")), ClaudeProcess::Model::Default);
    QCOMPARE(ClaudeProcess::parseModel(QString()), ClaudeProcess::Model::Default);
}

void ClaudeProcessTest::testModelNameRoundTrip()
{
    // Test that modelName and parseModel are consistent
    auto testRoundTrip = [](ClaudeProcess::Model model) {
        if (model == ClaudeProcess::Model::Default) {
            return true; // Default has empty string, skip
        }
        QString name = ClaudeProcess::modelName(model);
        ClaudeProcess::Model parsed = ClaudeProcess::parseModel(name);
        return parsed == model;
    };

    QVERIFY(testRoundTrip(ClaudeProcess::Model::Opus));
    QVERIFY(testRoundTrip(ClaudeProcess::Model::Sonnet));
    QVERIFY(testRoundTrip(ClaudeProcess::Model::Haiku));
}

void ClaudeProcessTest::testBuildCommand()
{
    QString cmd = ClaudeProcess::buildCommand();

    QVERIFY(cmd.contains(QStringLiteral("claude")));
}

void ClaudeProcessTest::testBuildCommandWithModel()
{
    QString cmd = ClaudeProcess::buildCommand(ClaudeProcess::Model::Opus);

    QVERIFY(cmd.contains(QStringLiteral("claude")));
    QVERIFY(cmd.contains(QStringLiteral("opus")) || cmd.contains(QStringLiteral("--model")));
}

void ClaudeProcessTest::testBuildCommandWithWorkingDir()
{
    QString cmd = ClaudeProcess::buildCommand(
        ClaudeProcess::Model::Default,
        QStringLiteral("/home/user/project")
    );

    QVERIFY(cmd.contains(QStringLiteral("claude")));
    // Working directory handling depends on implementation
}

void ClaudeProcessTest::testBuildCommandWithArgs()
{
    QStringList args;
    args << QStringLiteral("--verbose") << QStringLiteral("--no-color");

    QString cmd = ClaudeProcess::buildCommand(
        ClaudeProcess::Model::Default,
        QString(),
        args
    );

    QVERIFY(cmd.contains(QStringLiteral("claude")));
    QVERIFY(cmd.contains(QStringLiteral("--verbose")));
    QVERIFY(cmd.contains(QStringLiteral("--no-color")));
}

void ClaudeProcessTest::testSetCurrentTask()
{
    ClaudeProcess process;

    QVERIFY(process.currentTask().isEmpty());

    process.setCurrentTask(QStringLiteral("Writing test file"));
    QCOMPARE(process.currentTask(), QStringLiteral("Writing test file"));

    process.setCurrentTask(QStringLiteral("Another task"));
    QCOMPARE(process.currentTask(), QStringLiteral("Another task"));
}

void ClaudeProcessTest::testClearTask()
{
    ClaudeProcess process;

    process.setCurrentTask(QStringLiteral("Test task"));
    QVERIFY(!process.currentTask().isEmpty());

    process.clearTask();
    QVERIFY(process.currentTask().isEmpty());
}

void ClaudeProcessTest::testHandleHookEventStop()
{
    ClaudeProcess process;

    // First set to working state
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    // Then receive stop event
    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Idle);
}

void ClaudeProcessTest::testHandleHookEventNotification()
{
    ClaudeProcess process;

    QSignalSpy notificationSpy(&process, &ClaudeProcess::notificationReceived);

    QJsonObject notifData;
    notifData[QStringLiteral("type")] = QStringLiteral("permission");
    notifData[QStringLiteral("message")] = QStringLiteral("Allow file read?");

    process.handleHookEvent(QStringLiteral("Notification"),
                            QString::fromUtf8(QJsonDocument(notifData).toJson()));

    // State should indicate waiting for input
    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
}

void ClaudeProcessTest::testHandleHookEventPreToolUse()
{
    ClaudeProcess process;

    QJsonObject toolData;
    toolData[QStringLiteral("tool")] = QStringLiteral("Read");
    toolData[QStringLiteral("file")] = QStringLiteral("/path/to/file");

    process.handleHookEvent(QStringLiteral("PreToolUse"),
                            QString::fromUtf8(QJsonDocument(toolData).toJson()));

    QCOMPARE(process.state(), ClaudeProcess::State::Working);
}

void ClaudeProcessTest::testHandleHookEventPostToolUse()
{
    ClaudeProcess process;

    // Set to working first
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    // PostToolUse should keep working state (may have more tools to use)
    process.handleHookEvent(QStringLiteral("PostToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
}

void ClaudeProcessTest::testStateChangedSignal()
{
    ClaudeProcess process;

    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);

    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));

    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.at(0).at(0).value<ClaudeProcess::State>(), ClaudeProcess::State::Working);
}

void ClaudeProcessTest::testTaskSignals()
{
    ClaudeProcess process;

    QSignalSpy taskStartedSpy(&process, &ClaudeProcess::taskStarted);
    QSignalSpy taskFinishedSpy(&process, &ClaudeProcess::taskFinished);

    // Start a task
    process.setCurrentTask(QStringLiteral("Test task"));

    QCOMPARE(taskStartedSpy.count(), 1);
    QCOMPARE(taskStartedSpy.at(0).at(0).toString(), QStringLiteral("Test task"));

    // Clear the task
    process.clearTask();

    QCOMPARE(taskFinishedSpy.count(), 1);
}

// ============================================================
// State machine edge cases
// ============================================================

void ClaudeProcessTest::testSameStateNoSignal()
{
    ClaudeProcess process;

    // Go to Working
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);

    // Another PreToolUse should stay Working — no signal emitted (same state)
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QCOMPARE(stateSpy.count(), 0);
}

void ClaudeProcessTest::testRapidStateTransitions()
{
    ClaudeProcess process;
    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);

    // Rapid Working → Idle → Working → Idle
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));

    QCOMPARE(process.state(), ClaudeProcess::State::Idle);
    QCOMPARE(stateSpy.count(), 4); // Each transition emits exactly once
}

void ClaudeProcessTest::testUnknownEventType()
{
    ClaudeProcess process;
    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);

    // Unknown event type should be silently ignored
    process.handleHookEvent(QStringLiteral("SomeNewEvent"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::NotRunning);
    QCOMPARE(stateSpy.count(), 0);
}

void ClaudeProcessTest::testMalformedJsonEvent()
{
    ClaudeProcess process;

    // Malformed JSON should not crash — QJsonDocument::fromJson returns null doc
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("this is not json"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{{{broken"));
    QCOMPARE(process.state(), ClaudeProcess::State::Idle);
}

void ClaudeProcessTest::testEmptyEventData()
{
    ClaudeProcess process;

    // Empty string should not crash
    process.handleHookEvent(QStringLiteral("PreToolUse"), QString());
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    process.handleHookEvent(QStringLiteral("Stop"), QString());
    QCOMPARE(process.state(), ClaudeProcess::State::Idle);
}

void ClaudeProcessTest::testIsRunningAllStates()
{
    ClaudeProcess process;

    // NotRunning → not running
    QVERIFY(!process.isRunning());

    // Working → running
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QVERIFY(process.isRunning());

    // Idle → running (process is alive but idle)
    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    QVERIFY(process.isRunning());

    // WaitingInput → running
    QJsonObject permData;
    permData[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    permData[QStringLiteral("yolo_approved")] = false;
    process.handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(permData).toJson()));
    QVERIFY(process.isRunning());
}

// ============================================================
// Notification event variants
// ============================================================

void ClaudeProcessTest::testNotificationPermissionRequest()
{
    ClaudeProcess process;
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);
    QSignalSpy notifSpy(&process, &ClaudeProcess::notificationReceived);

    QJsonObject data;
    data[QStringLiteral("type")] = QStringLiteral("permission_request");
    data[QStringLiteral("action")] = QStringLiteral("file_write");
    data[QStringLiteral("description")] = QStringLiteral("Write to /tmp/test");
    data[QStringLiteral("message")] = QStringLiteral("Permission needed");

    process.handleHookEvent(QStringLiteral("Notification"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
    QCOMPARE(permSpy.count(), 1);
    QCOMPARE(notifSpy.count(), 1);
}

void ClaudeProcessTest::testNotificationIdlePrompt()
{
    ClaudeProcess process;

    QJsonObject data;
    data[QStringLiteral("type")] = QStringLiteral("idle_prompt");
    data[QStringLiteral("message")] = QStringLiteral("Claude is idle");

    process.handleHookEvent(QStringLiteral("Notification"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
}

void ClaudeProcessTest::testNotificationGeneric()
{
    ClaudeProcess process;
    QSignalSpy notifSpy(&process, &ClaudeProcess::notificationReceived);

    QJsonObject data;
    data[QStringLiteral("type")] = QStringLiteral("info");
    data[QStringLiteral("message")] = QStringLiteral("Just an info message");

    process.handleHookEvent(QStringLiteral("Notification"), QString::fromUtf8(QJsonDocument(data).toJson()));

    // Generic notification should NOT change state to WaitingInput
    QCOMPARE(process.state(), ClaudeProcess::State::NotRunning);
    QCOMPARE(notifSpy.count(), 1);
    QCOMPARE(notifSpy.at(0).at(0).toString(), QStringLiteral("info"));
}

// ============================================================
// PermissionRequest event
// ============================================================

void ClaudeProcessTest::testPermissionRequestSignals()
{
    ClaudeProcess process;
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Write");
    data[QStringLiteral("tool_input")] = QStringLiteral("/path/to/file");
    data[QStringLiteral("yolo_approved")] = false;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(permSpy.count(), 1);
    QCOMPARE(permSpy.at(0).at(0).toString(), QStringLiteral("Write"));
    QCOMPARE(permSpy.at(0).at(1).toString(), QStringLiteral("/path/to/file"));
}

void ClaudeProcessTest::testPermissionRequestEmptyToolName()
{
    ClaudeProcess process;
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    // Missing tool_name field
    QJsonObject data;
    data[QStringLiteral("yolo_approved")] = false;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
    QCOMPARE(permSpy.count(), 1);
    QVERIFY(permSpy.at(0).at(0).toString().isEmpty()); // Empty tool name
}

// ============================================================
// PreToolUse task description
// ============================================================

void ClaudeProcessTest::testPreToolUseSetsTask()
{
    ClaudeProcess process;
    QSignalSpy taskSpy(&process, &ClaudeProcess::taskStarted);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");

    process.handleHookEvent(QStringLiteral("PreToolUse"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(process.currentTask(), QStringLiteral("Using tool: Bash"));
    QCOMPARE(taskSpy.count(), 1);
}

// ============================================================
// Subagent events
// ============================================================

void ClaudeProcessTest::testSubagentStartEvent()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStarted);

    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("agent-abc-123");
    data[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");
    data[QStringLiteral("transcript_path")] = QStringLiteral("/tmp/transcript.md");

    process.handleHookEvent(QStringLiteral("SubagentStart"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("agent-abc-123"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("general-purpose"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("/tmp/transcript.md"));
}

void ClaudeProcessTest::testSubagentStopEvent()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStopped);

    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("agent-xyz-789");
    data[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    data[QStringLiteral("agent_transcript_path")] = QStringLiteral("/tmp/done.md");

    process.handleHookEvent(QStringLiteral("SubagentStop"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("agent-xyz-789"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Explore"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("/tmp/done.md"));
}

void ClaudeProcessTest::testSubagentStartMissingFields()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStarted);

    // Use alternative field name "subagent_type" and omit transcript
    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("agent-fallback");
    data[QStringLiteral("subagent_type")] = QStringLiteral("Plan");

    process.handleHookEvent(QStringLiteral("SubagentStart"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("agent-fallback"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Plan"));
    QVERIFY(spy.at(0).at(2).toString().isEmpty()); // No transcript
}

// ============================================================
// Team events
// ============================================================

void ClaudeProcessTest::testTeammateIdleEvent()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::teammateIdle);

    QJsonObject data;
    data[QStringLiteral("teammate_name")] = QStringLiteral("researcher");
    data[QStringLiteral("team_name")] = QStringLiteral("my-team");

    process.handleHookEvent(QStringLiteral("TeammateIdle"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("researcher"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("my-team"));
}

void ClaudeProcessTest::testTaskCompletedEvent()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::taskCompleted);

    QJsonObject data;
    data[QStringLiteral("task_id")] = QStringLiteral("task-42");
    data[QStringLiteral("task_subject")] = QStringLiteral("Fix the bug");
    data[QStringLiteral("teammate_name")] = QStringLiteral("coder");
    data[QStringLiteral("team_name")] = QStringLiteral("dev-team");

    process.handleHookEvent(QStringLiteral("TaskCompleted"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("task-42"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Fix the bug"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("coder"));
    QCOMPARE(spy.at(0).at(3).toString(), QStringLiteral("dev-team"));
}

void ClaudeProcessTest::testTeamEventMissingFields()
{
    ClaudeProcess process;

    // TeammateIdle with alternative "name" field
    {
        QSignalSpy spy(&process, &ClaudeProcess::teammateIdle);

        QJsonObject data;
        data[QStringLiteral("name")] = QStringLiteral("tester");
        data[QStringLiteral("team_name")] = QStringLiteral("qa-team");

        process.handleHookEvent(QStringLiteral("TeammateIdle"), QString::fromUtf8(QJsonDocument(data).toJson()));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("tester"));
    }

    // TaskCompleted with alternative field names
    {
        QSignalSpy spy(&process, &ClaudeProcess::taskCompleted);

        QJsonObject data;
        data[QStringLiteral("task_id")] = QStringLiteral("task-99");
        data[QStringLiteral("subject")] = QStringLiteral("Write docs"); // alt for task_subject
        data[QStringLiteral("name")] = QStringLiteral("writer"); // alt for teammate_name

        process.handleHookEvent(QStringLiteral("TaskCompleted"), QString::fromUtf8(QJsonDocument(data).toJson()));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Write docs"));
        QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("writer"));
    }
}

QTEST_GUILESS_MAIN(ClaudeProcessTest)

#include "moc_ClaudeProcessTest.cpp"
