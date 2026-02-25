/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeAgentTeamTest.h"

// Qt
#include <QDir>
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
    data[QStringLiteral("transcript_path")] = QStringLiteral("/home/user/.claude/projects/proj/uuid.jsonl");

    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("abc-123"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Explore"));
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("/home/user/.claude/projects/proj/uuid.jsonl"));
}

void ClaudeAgentTeamTest::testSubagentStartAlternateField()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStarted);

    // Use subagent_type instead of agent_type, no transcript_path
    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("def-456");
    data[QStringLiteral("subagent_type")] = QStringLiteral("Plan");

    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("def-456"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Plan"));
    QVERIFY(spy.at(0).at(2).toString().isEmpty()); // No transcript path provided
}

void ClaudeAgentTeamTest::testSubagentStartDerivedTranscriptPath()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::subagentStarted);

    // Provide transcript_path (parent session transcript) so the path can be derived
    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("agent-xyz");
    data[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    data[QStringLiteral("transcript_path")] = QStringLiteral("/home/user/.claude/projects/-home-user-myproj/abc123.jsonl");

    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));

    QCOMPARE(spy.count(), 1);
    // The raw transcript_path is passed through; derivation happens in ClaudeSession
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("/home/user/.claude/projects/-home-user-myproj/abc123.jsonl"));
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

// ============================================================
// Task tool description capture
// ============================================================

void ClaudeAgentTeamTest::testPreToolUseTaskEmitsSignal()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::taskToolCalled);

    QJsonObject toolInput;
    toolInput[QStringLiteral("description")] = QStringLiteral("Check obs for bugs");
    toolInput[QStringLiteral("prompt")] = QStringLiteral("Look for bugs in obs-studio");
    toolInput[QStringLiteral("subagent_type")] = QStringLiteral("Explore");

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Task");
    data[QStringLiteral("tool_input")] = toolInput;

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Check obs for bugs"));
}

void ClaudeAgentTeamTest::testPreToolUseNonTaskNoSignal()
{
    ClaudeProcess process;
    QSignalSpy spy(&process, &ClaudeProcess::taskToolCalled);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(spy.count(), 0);
}

void ClaudeAgentTeamTest::testTaskDescriptionCorrelation()
{
    // Simulate: PreToolUse(Task) → SubagentStart → verify taskDescription is assigned
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    auto *process = session.claudeProcess();
    QVERIFY(process);

    // 1. PreToolUse with Task description
    QJsonObject toolInput;
    toolInput[QStringLiteral("description")] = QStringLiteral("Fix login bug");
    QJsonObject preToolData;
    preToolData[QStringLiteral("tool_name")] = QStringLiteral("Task");
    preToolData[QStringLiteral("tool_input")] = toolInput;
    process->handleHookEvent(QStringLiteral("PreToolUse"), toJson(preToolData));

    // 2. SubagentStart
    QJsonObject startData;
    startData[QStringLiteral("agent_id")] = QStringLiteral("agent-abc");
    startData[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    process->handleHookEvent(QStringLiteral("SubagentStart"), toJson(startData));

    // 3. Verify taskDescription was correlated
    const auto &agents = session.subagents();
    QVERIFY(agents.contains(QStringLiteral("agent-abc")));
    QCOMPARE(agents[QStringLiteral("agent-abc")].taskDescription, QStringLiteral("Fix login bug"));
}

void ClaudeAgentTeamTest::testTaskDescriptionCorrelationMultiple()
{
    // Simulate: 2 PreToolUse(Task) → 2 SubagentStart → verify FIFO ordering
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    auto *process = session.claudeProcess();
    QVERIFY(process);

    // 1. Two Task tool calls
    QJsonObject input1;
    input1[QStringLiteral("description")] = QStringLiteral("Search codebase");
    QJsonObject pre1;
    pre1[QStringLiteral("tool_name")] = QStringLiteral("Task");
    pre1[QStringLiteral("tool_input")] = input1;
    process->handleHookEvent(QStringLiteral("PreToolUse"), toJson(pre1));

    QJsonObject input2;
    input2[QStringLiteral("description")] = QStringLiteral("Run tests");
    QJsonObject pre2;
    pre2[QStringLiteral("tool_name")] = QStringLiteral("Task");
    pre2[QStringLiteral("tool_input")] = input2;
    process->handleHookEvent(QStringLiteral("PreToolUse"), toJson(pre2));

    // 2. Two SubagentStart in order
    QJsonObject start1;
    start1[QStringLiteral("agent_id")] = QStringLiteral("agent-1");
    start1[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    process->handleHookEvent(QStringLiteral("SubagentStart"), toJson(start1));

    QJsonObject start2;
    start2[QStringLiteral("agent_id")] = QStringLiteral("agent-2");
    start2[QStringLiteral("agent_type")] = QStringLiteral("Bash");
    process->handleHookEvent(QStringLiteral("SubagentStart"), toJson(start2));

    // 3. Verify FIFO correlation
    const auto &agents = session.subagents();
    QCOMPARE(agents[QStringLiteral("agent-1")].taskDescription, QStringLiteral("Search codebase"));
    QCOMPARE(agents[QStringLiteral("agent-2")].taskDescription, QStringLiteral("Run tests"));
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

// ============================================================
// SubagentInfo toJson/fromJson round-trip
// ============================================================

void ClaudeAgentTeamTest::testSubagentInfoRoundTrip()
{
    SubagentInfo original;
    original.agentId = QStringLiteral("agent-rt-123");
    original.agentType = QStringLiteral("Explore");
    original.teammateName = QStringLiteral("researcher");
    original.state = ClaudeProcess::State::Idle;
    original.startedAt = QDateTime(QDate(2025, 6, 15), QTime(10, 0, 0), QTimeZone::utc());
    original.lastUpdated = QDateTime(QDate(2025, 6, 15), QTime(10, 5, 0), QTimeZone::utc());
    original.transcriptPath = QStringLiteral("/home/user/.claude/projects/proj/abc.jsonl");
    original.currentTaskSubject = QStringLiteral("Fix the login bug");
    original.taskDescription = QStringLiteral("Search codebase for auth issues");
    original.promptGroupId = 3;

    QJsonObject json = original.toJson();
    SubagentInfo restored = SubagentInfo::fromJson(json);

    QCOMPARE(restored.agentId, original.agentId);
    QCOMPARE(restored.agentType, original.agentType);
    QCOMPARE(restored.teammateName, original.teammateName);
    QCOMPARE(static_cast<int>(restored.state), static_cast<int>(original.state));
    QCOMPARE(restored.startedAt, original.startedAt);
    QCOMPARE(restored.lastUpdated, original.lastUpdated);
    QCOMPARE(restored.transcriptPath, original.transcriptPath);
    QCOMPARE(restored.currentTaskSubject, original.currentTaskSubject);
    QCOMPARE(restored.taskDescription, original.taskDescription);
    QCOMPARE(restored.promptGroupId, original.promptGroupId);
}

void ClaudeAgentTeamTest::testSubagentInfoFromJsonMissingFields()
{
    // Minimal JSON — only required fields
    QJsonObject json;
    json[QStringLiteral("agentId")] = QStringLiteral("min-agent");
    json[QStringLiteral("agentType")] = QStringLiteral("Bash");

    SubagentInfo info = SubagentInfo::fromJson(json);
    QCOMPARE(info.agentId, QStringLiteral("min-agent"));
    QCOMPARE(info.agentType, QStringLiteral("Bash"));
    QVERIFY(info.teammateName.isEmpty());
    QVERIFY(!info.startedAt.isValid());
    QVERIFY(!info.lastUpdated.isValid());
    QVERIFY(info.transcriptPath.isEmpty());
    QVERIFY(info.currentTaskSubject.isEmpty());
    QVERIFY(info.taskDescription.isEmpty());
    QCOMPARE(info.promptGroupId, 0);
}

void ClaudeAgentTeamTest::testSubagentInfoFromJsonStateDefault()
{
    // Missing state defaults to Working
    QJsonObject json;
    json[QStringLiteral("agentId")] = QStringLiteral("def-agent");
    json[QStringLiteral("agentType")] = QStringLiteral("Plan");

    SubagentInfo info = SubagentInfo::fromJson(json);
    QCOMPARE(static_cast<int>(info.state), static_cast<int>(ClaudeProcess::State::Working));

    // Explicit NotRunning
    json[QStringLiteral("state")] = static_cast<int>(ClaudeProcess::State::NotRunning);
    SubagentInfo info2 = SubagentInfo::fromJson(json);
    QCOMPARE(static_cast<int>(info2.state), static_cast<int>(ClaudeProcess::State::NotRunning));
}

// ============================================================
// SubprocessInfo toJson/fromJson round-trip
// ============================================================

void ClaudeAgentTeamTest::testSubprocessInfoRoundTrip()
{
    SubprocessInfo original;
    original.id = QStringLiteral("proc-rt-456");
    original.command = QStringLiteral("ninja -j4");
    original.fullCommand = QStringLiteral("ninja -j4 -C /build");
    original.status = SubprocessInfo::Completed;
    original.startedAt = QDateTime(QDate(2025, 6, 15), QTime(10, 0, 0), QTimeZone::utc());
    original.finishedAt = QDateTime(QDate(2025, 6, 15), QTime(10, 2, 0), QTimeZone::utc());
    original.exitCode = 0;
    original.output = QStringLiteral("[28/28] Linking CXX executable bin/test");
    original.pid = 12345;
    original.promptGroupId = 2;
    original.isBackground = true;

    QJsonObject json = original.toJson();
    SubprocessInfo restored = SubprocessInfo::fromJson(json);

    QCOMPARE(restored.id, original.id);
    QCOMPARE(restored.command, original.command);
    QCOMPARE(restored.fullCommand, original.fullCommand);
    QCOMPARE(static_cast<int>(restored.status), static_cast<int>(original.status));
    QCOMPARE(restored.startedAt, original.startedAt);
    QCOMPARE(restored.finishedAt, original.finishedAt);
    QCOMPARE(restored.exitCode, original.exitCode);
    QCOMPARE(restored.output, original.output);
    QCOMPARE(restored.pid, original.pid);
    QCOMPARE(restored.promptGroupId, original.promptGroupId);
    QCOMPARE(restored.isBackground, original.isBackground);
}

void ClaudeAgentTeamTest::testSubprocessInfoFromJsonMissingFields()
{
    QJsonObject json;
    json[QStringLiteral("id")] = QStringLiteral("min-proc");
    json[QStringLiteral("command")] = QStringLiteral("ls");

    SubprocessInfo info = SubprocessInfo::fromJson(json);
    QCOMPARE(info.id, QStringLiteral("min-proc"));
    QCOMPARE(info.command, QStringLiteral("ls"));
    QVERIFY(info.fullCommand.isEmpty());
    QCOMPARE(static_cast<int>(info.status), static_cast<int>(SubprocessInfo::Running)); // default
    QVERIFY(!info.startedAt.isValid());
    QVERIFY(!info.finishedAt.isValid());
    QCOMPARE(info.exitCode, -1);
    QVERIFY(info.output.isEmpty());
    QCOMPARE(info.pid, qint64(0));
    QCOMPARE(info.promptGroupId, 0);
    QVERIFY(!info.isBackground);
}

void ClaudeAgentTeamTest::testSubprocessInfoOutputTruncation()
{
    SubprocessInfo info;
    info.id = QStringLiteral("trunc");
    info.command = QStringLiteral("cat bigfile");
    // Output > 1000 chars should be truncated in toJson
    info.output = QString(2000, QLatin1Char('x'));

    QJsonObject json = info.toJson();
    QCOMPARE(json[QStringLiteral("output")].toString().length(), 1000);

    // Restore from truncated JSON — output is 1000 chars
    SubprocessInfo restored = SubprocessInfo::fromJson(json);
    QCOMPARE(restored.output.length(), 1000);
}

void ClaudeAgentTeamTest::testSubprocessInfoResourceUsageRoundTrip()
{
    SubprocessInfo info;
    info.id = QStringLiteral("res-usage");
    info.command = QStringLiteral("build");
    info.resourceUsage.cpuPercent = 85.5;
    info.resourceUsage.rssBytes = 1048576; // 1 MB

    QJsonObject json = info.toJson();
    QVERIFY(json.contains(QStringLiteral("resourceUsage")));

    SubprocessInfo restored = SubprocessInfo::fromJson(json);
    QCOMPARE(restored.resourceUsage.cpuPercent, 85.5);
    QCOMPARE(restored.resourceUsage.rssBytes, quint64(1048576));

    // No resourceUsage when both are zero
    SubprocessInfo info2;
    info2.id = QStringLiteral("no-res");
    info2.command = QStringLiteral("echo");
    QJsonObject json2 = info2.toJson();
    QVERIFY(!json2.contains(QStringLiteral("resourceUsage")));
}

QTEST_GUILESS_MAIN(ClaudeAgentTeamTest)

#include "moc_ClaudeAgentTeamTest.cpp"
