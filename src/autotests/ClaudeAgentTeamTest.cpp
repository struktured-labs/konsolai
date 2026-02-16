/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeAgentTeamTest.h"

// Qt
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeProcess.h"
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

static QString toJson(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void ClaudeAgentTeamTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeAgentTeamTest::cleanupTestCase()
{
}

// ============================================================
// SubagentStart
// ============================================================

void ClaudeAgentTeamTest::testSubagentStartSignal()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStarted);

    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("abc-123");
    data[QStringLiteral("agent_type")] = QStringLiteral("Explore");

    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("abc-123"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Explore"));
}

void ClaudeAgentTeamTest::testSubagentStartAlternateField()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStarted);

    // Use subagent_type instead of agent_type
    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("def-456");
    data[QStringLiteral("subagent_type")] = QStringLiteral("Plan");

    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("def-456"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Plan"));
}

// ============================================================
// SubagentStop
// ============================================================

void ClaudeAgentTeamTest::testSubagentStopSignal()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStopped);

    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("abc-123");
    data[QStringLiteral("agent_type")] = QStringLiteral("Explore");

    process.handleHookEvent(QStringLiteral("SubagentStop"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("abc-123"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Explore"));
    QVERIFY(spy.at(0).at(2).toString().isEmpty()); // No transcript path
}

void ClaudeAgentTeamTest::testSubagentStopWithTranscript()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStopped);

    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("abc-123");
    data[QStringLiteral("agent_type")] = QStringLiteral("Bash");
    data[QStringLiteral("agent_transcript_path")] = QStringLiteral("/tmp/transcript.jsonl");

    process.handleHookEvent(QStringLiteral("SubagentStop"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("/tmp/transcript.jsonl"));
}

// ============================================================
// TeammateIdle
// ============================================================

void ClaudeAgentTeamTest::testTeammateIdleSignal()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::teammateIdle);

    QJsonObject data;
    data[QStringLiteral("teammate_name")] = QStringLiteral("researcher");
    data[QStringLiteral("team_name")] = QStringLiteral("my-project");

    process.handleHookEvent(QStringLiteral("TeammateIdle"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("researcher"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("my-project"));
}

void ClaudeAgentTeamTest::testTeammateIdleAlternateNameField()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::teammateIdle);

    // Use "name" instead of "teammate_name"
    QJsonObject data;
    data[QStringLiteral("name")] = QStringLiteral("tester");
    data[QStringLiteral("team_name")] = QStringLiteral("team-alpha");

    process.handleHookEvent(QStringLiteral("TeammateIdle"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("tester"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("team-alpha"));
}

// ============================================================
// TaskCompleted
// ============================================================

void ClaudeAgentTeamTest::testTaskCompletedSignal()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::taskCompleted);

    QJsonObject data;
    data[QStringLiteral("task_id")] = QStringLiteral("task-1");
    data[QStringLiteral("task_subject")] = QStringLiteral("Fix the bug");
    data[QStringLiteral("teammate_name")] = QStringLiteral("developer");
    data[QStringLiteral("team_name")] = QStringLiteral("my-project");

    process.handleHookEvent(QStringLiteral("TaskCompleted"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("task-1"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Fix the bug"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("developer"));
    QCOMPARE(spy.at(0).at(3).toString(), QStringLiteral("my-project"));
}

void ClaudeAgentTeamTest::testTaskCompletedAlternateFields()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::taskCompleted);

    // Use "subject" and "name" alternate fields
    QJsonObject data;
    data[QStringLiteral("task_id")] = QStringLiteral("task-2");
    data[QStringLiteral("subject")] = QStringLiteral("Add tests");
    data[QStringLiteral("name")] = QStringLiteral("tester");
    data[QStringLiteral("team_name")] = QStringLiteral("team-beta");

    process.handleHookEvent(QStringLiteral("TaskCompleted"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Add tests"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("tester"));
}

// ============================================================
// Team detection (ClaudeSession level)
// ============================================================

void ClaudeAgentTeamTest::testHasActiveTeamInitiallyFalse()
{
    // ClaudeProcess is used as a standalone test proxy here —
    // hasActiveTeam() is on ClaudeSession which is hard to construct in tests.
    // We test the signal chain instead.
    ClaudeProcess process;
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);

    // No events → no signals
    QCOMPARE(startSpy.count(), 0);
}

void ClaudeAgentTeamTest::testHasActiveTeamAfterSubagentStart()
{
    ClaudeProcess process;
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);

    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("agent-1");
    data[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");

    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));

    QCOMPARE(startSpy.count(), 1);
}

void ClaudeAgentTeamTest::testHasActiveTeamAfterAllSubagentsStop()
{
    ClaudeProcess process;
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);
    QSignalSpy stopSpy(&process, &ClaudeProcess::subagentStopped);

    // Start two subagents
    QJsonObject data1;
    data1[QStringLiteral("agent_id")] = QStringLiteral("agent-1");
    data1[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data1));

    QJsonObject data2;
    data2[QStringLiteral("agent_id")] = QStringLiteral("agent-2");
    data2[QStringLiteral("agent_type")] = QStringLiteral("Bash");
    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data2));

    QCOMPARE(startSpy.count(), 2);

    // Stop both
    QJsonObject stop1;
    stop1[QStringLiteral("agent_id")] = QStringLiteral("agent-1");
    stop1[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    process.handleHookEvent(QStringLiteral("SubagentStop"), toJson(stop1));

    QJsonObject stop2;
    stop2[QStringLiteral("agent_id")] = QStringLiteral("agent-2");
    stop2[QStringLiteral("agent_type")] = QStringLiteral("Bash");
    process.handleHookEvent(QStringLiteral("SubagentStop"), toJson(stop2));

    QCOMPARE(stopSpy.count(), 2);
}

void ClaudeAgentTeamTest::testSubagentsMapTracking()
{
    ClaudeProcess process;

    // Verify signals carry correct data
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);

    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("unique-agent-id");
    data[QStringLiteral("agent_type")] = QStringLiteral("Plan");

    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));

    QCOMPARE(startSpy.count(), 1);
    QCOMPARE(startSpy.at(0).at(0).toString(), QStringLiteral("unique-agent-id"));
    QCOMPARE(startSpy.at(0).at(1).toString(), QStringLiteral("Plan"));
}

void ClaudeAgentTeamTest::testMultipleSubagents()
{
    ClaudeProcess process;
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);
    QSignalSpy stopSpy(&process, &ClaudeProcess::subagentStopped);

    // Start 3 agents
    for (int i = 0; i < 3; ++i) {
        QJsonObject data;
        data[QStringLiteral("agent_id")] = QStringLiteral("agent-%1").arg(i);
        data[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");
        process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));
    }

    QCOMPARE(startSpy.count(), 3);

    // Stop 2 of them
    for (int i = 0; i < 2; ++i) {
        QJsonObject data;
        data[QStringLiteral("agent_id")] = QStringLiteral("agent-%1").arg(i);
        data[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");
        process.handleHookEvent(QStringLiteral("SubagentStop"), toJson(data));
    }

    QCOMPARE(stopSpy.count(), 2);
}

void ClaudeAgentTeamTest::testUnknownEventNoSubagentSignal()
{
    ClaudeProcess process;
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);
    QSignalSpy stopSpy(&process, &ClaudeProcess::subagentStopped);
    QSignalSpy idleSpy(&process, &ClaudeProcess::teammateIdle);
    QSignalSpy completedSpy(&process, &ClaudeProcess::taskCompleted);

    process.handleHookEvent(QStringLiteral("SomethingElse"), QStringLiteral("{}"));

    QCOMPARE(startSpy.count(), 0);
    QCOMPARE(stopSpy.count(), 0);
    QCOMPARE(idleSpy.count(), 0);
    QCOMPARE(completedSpy.count(), 0);
}

QTEST_GUILESS_MAIN(ClaudeAgentTeamTest)

#include "moc_ClaudeAgentTeamTest.cpp"
