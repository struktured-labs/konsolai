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

QTEST_GUILESS_MAIN(ClaudeProcessTest)

#include "moc_ClaudeProcessTest.cpp"
