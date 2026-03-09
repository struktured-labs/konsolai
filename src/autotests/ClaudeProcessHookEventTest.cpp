/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeProcessHookEventTest.h"

// Qt
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeProcess.h"

using namespace Konsolai;

// Helper to build JSON string from a QJsonObject
static QString toJson(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson());
}

void ClaudeProcessHookEventTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeProcessHookEventTest::cleanupTestCase()
{
}

// ================================================================
// PreToolUse: Bash tool
// ================================================================

void ClaudeProcessHookEventTest::testPreToolUseBashEmitsSignal()
{
    ClaudeProcess process;
    QSignalSpy bashSpy(&process, &ClaudeProcess::bashToolStarted);

    QJsonObject inputObj;
    inputObj[QStringLiteral("command")] = QStringLiteral("ls -la /tmp");

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_input")] = inputObj;

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(bashSpy.count(), 1);
    QCOMPARE(bashSpy.at(0).at(0).toString(), QStringLiteral("ls -la /tmp"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QCOMPARE(process.currentTask(), QStringLiteral("Using tool: Bash"));
}

void ClaudeProcessHookEventTest::testPreToolUseBashEmptyCommand()
{
    ClaudeProcess process;
    QSignalSpy bashSpy(&process, &ClaudeProcess::bashToolStarted);

    QJsonObject inputObj;
    inputObj[QStringLiteral("command")] = QString(); // empty command

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_input")] = inputObj;

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    // Empty command should NOT emit bashToolStarted
    QCOMPARE(bashSpy.count(), 0);
    // But state should still transition
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
}

void ClaudeProcessHookEventTest::testPreToolUseBashToolInputNotObject()
{
    ClaudeProcess process;
    QSignalSpy bashSpy(&process, &ClaudeProcess::bashToolStarted);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_input")] = QStringLiteral("not an object");

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    // tool_input is a string, not an object -- should not emit
    QCOMPARE(bashSpy.count(), 0);
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
}

// ================================================================
// PreToolUse: AskUserQuestion tool
// ================================================================

void ClaudeProcessHookEventTest::testPreToolUseAskUserQuestionEmitsSignal()
{
    ClaudeProcess process;
    QSignalSpy questionSpy(&process, &ClaudeProcess::userQuestionDetected);

    QJsonObject questionObj;
    questionObj[QStringLiteral("question")] = QStringLiteral("Should I proceed?");
    QJsonArray questionsArr;
    questionsArr.append(questionObj);

    QJsonObject inputObj;
    inputObj[QStringLiteral("questions")] = questionsArr;

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("AskUserQuestion");
    data[QStringLiteral("tool_input")] = inputObj;

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(questionSpy.count(), 1);
    QCOMPARE(questionSpy.at(0).at(0).toString(), QStringLiteral("Should I proceed?"));
}

void ClaudeProcessHookEventTest::testPreToolUseAskUserQuestionEmptyQuestions()
{
    ClaudeProcess process;
    QSignalSpy questionSpy(&process, &ClaudeProcess::userQuestionDetected);

    QJsonObject inputObj;
    inputObj[QStringLiteral("questions")] = QJsonArray(); // empty array

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("AskUserQuestion");
    data[QStringLiteral("tool_input")] = inputObj;

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    // Empty questions array -- no signal
    QCOMPARE(questionSpy.count(), 0);
}

void ClaudeProcessHookEventTest::testPreToolUseAskUserQuestionNoQuestionsField()
{
    ClaudeProcess process;
    QSignalSpy questionSpy(&process, &ClaudeProcess::userQuestionDetected);

    QJsonObject inputObj;
    // No "questions" key at all

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("AskUserQuestion");
    data[QStringLiteral("tool_input")] = inputObj;

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(questionSpy.count(), 0);
}

// ================================================================
// PreToolUse: Task tool
// ================================================================

void ClaudeProcessHookEventTest::testPreToolUseTaskEmitsSignal()
{
    ClaudeProcess process;
    QSignalSpy taskToolSpy(&process, &ClaudeProcess::taskToolCalled);

    QJsonObject inputObj;
    inputObj[QStringLiteral("description")] = QStringLiteral("Analyze the codebase");

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Task");
    data[QStringLiteral("tool_input")] = inputObj;

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(taskToolSpy.count(), 1);
    QCOMPARE(taskToolSpy.at(0).at(0).toString(), QStringLiteral("Analyze the codebase"));
}

void ClaudeProcessHookEventTest::testPreToolUseTaskEmptyDescription()
{
    ClaudeProcess process;
    QSignalSpy taskToolSpy(&process, &ClaudeProcess::taskToolCalled);

    QJsonObject inputObj;
    inputObj[QStringLiteral("description")] = QString(); // empty

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Task");
    data[QStringLiteral("tool_input")] = inputObj;

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    // Empty description should NOT emit
    QCOMPARE(taskToolSpy.count(), 0);
}

// ================================================================
// PreToolUse: Edit/Write/Read tools
// ================================================================

void ClaudeProcessHookEventTest::testPreToolUseEditSetsWorking()
{
    ClaudeProcess process;
    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Edit");

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QCOMPARE(process.currentTask(), QStringLiteral("Using tool: Edit"));
}

void ClaudeProcessHookEventTest::testPreToolUseWriteSetsWorking()
{
    ClaudeProcess process;
    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Write");

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QCOMPARE(process.currentTask(), QStringLiteral("Using tool: Write"));
}

void ClaudeProcessHookEventTest::testPreToolUseReadSetsWorking()
{
    ClaudeProcess process;
    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Read");

    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(data));

    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QCOMPARE(process.currentTask(), QStringLiteral("Using tool: Read"));
}

// ================================================================
// PostToolUse: toolUseCompleted signal
// ================================================================

void ClaudeProcessHookEventTest::testPostToolUseEmitsToolUseCompleted()
{
    ClaudeProcess process;
    QSignalSpy toolSpy(&process, &ClaudeProcess::toolUseCompleted);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_response")] = QStringLiteral("exit code 0");

    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(data));

    QCOMPARE(toolSpy.count(), 1);
    QCOMPARE(toolSpy.at(0).at(0).toString(), QStringLiteral("Bash"));
    QCOMPARE(toolSpy.at(0).at(1).toString(), QStringLiteral("exit code 0"));
}

void ClaudeProcessHookEventTest::testPostToolUseResponseIsObject()
{
    ClaudeProcess process;
    QSignalSpy toolSpy(&process, &ClaudeProcess::toolUseCompleted);

    QJsonObject responseObj;
    responseObj[QStringLiteral("exit_code")] = 0;
    responseObj[QStringLiteral("output")] = QStringLiteral("hello");

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_response")] = responseObj;

    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(data));

    QCOMPARE(toolSpy.count(), 1);
    QCOMPARE(toolSpy.at(0).at(0).toString(), QStringLiteral("Bash"));
    // Response should be indented JSON of the object
    QString responseStr = toolSpy.at(0).at(1).toString();
    QVERIFY(responseStr.contains(QStringLiteral("exit_code")));
    QVERIFY(responseStr.contains(QStringLiteral("hello")));
}

void ClaudeProcessHookEventTest::testPostToolUseResponseIsArray()
{
    ClaudeProcess process;
    QSignalSpy toolSpy(&process, &ClaudeProcess::toolUseCompleted);

    QJsonArray responseArr;
    responseArr.append(QStringLiteral("item1"));
    responseArr.append(QStringLiteral("item2"));

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Read");
    data[QStringLiteral("tool_response")] = responseArr;

    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(data));

    QCOMPARE(toolSpy.count(), 1);
    QCOMPARE(toolSpy.at(0).at(0).toString(), QStringLiteral("Read"));
    QString responseStr = toolSpy.at(0).at(1).toString();
    QVERIFY(responseStr.contains(QStringLiteral("item1")));
    QVERIFY(responseStr.contains(QStringLiteral("item2")));
}

void ClaudeProcessHookEventTest::testPostToolUseResponseIsString()
{
    ClaudeProcess process;
    QSignalSpy toolSpy(&process, &ClaudeProcess::toolUseCompleted);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Write");
    data[QStringLiteral("tool_response")] = QStringLiteral("File written successfully");

    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(data));

    QCOMPARE(toolSpy.count(), 1);
    QCOMPARE(toolSpy.at(0).at(0).toString(), QStringLiteral("Write"));
    QCOMPARE(toolSpy.at(0).at(1).toString(), QStringLiteral("File written successfully"));
}

void ClaudeProcessHookEventTest::testPostToolUseEmptyToolNameNoSignal()
{
    ClaudeProcess process;
    QSignalSpy toolSpy(&process, &ClaudeProcess::toolUseCompleted);

    QJsonObject data;
    // No tool_name field
    data[QStringLiteral("tool_response")] = QStringLiteral("some response");

    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(data));

    // Empty tool_name -> no signal emitted
    QCOMPARE(toolSpy.count(), 0);
}

void ClaudeProcessHookEventTest::testPostToolUseNoResponseField()
{
    ClaudeProcess process;
    QSignalSpy toolSpy(&process, &ClaudeProcess::toolUseCompleted);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    // No tool_response field

    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(data));

    QCOMPARE(toolSpy.count(), 1);
    QCOMPARE(toolSpy.at(0).at(0).toString(), QStringLiteral("Bash"));
    // Response should be empty string when field is missing
    QVERIFY(toolSpy.at(0).at(1).toString().isEmpty());
}

// ================================================================
// Stop event: taskFinished signal
// ================================================================

void ClaudeProcessHookEventTest::testStopEmitsTaskFinished()
{
    ClaudeProcess process;
    QSignalSpy finishedSpy(&process, &ClaudeProcess::taskFinished);

    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(process.state(), ClaudeProcess::State::Idle);
}

void ClaudeProcessHookEventTest::testStopFromWorkingEmitsTaskFinished()
{
    ClaudeProcess process;

    // Go to Working first
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    QSignalSpy finishedSpy(&process, &ClaudeProcess::taskFinished);
    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);

    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));

    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.at(0).at(0).value<ClaudeProcess::State>(), ClaudeProcess::State::Idle);
}

// ================================================================
// Reset
// ================================================================

void ClaudeProcessHookEventTest::testResetClearsStateAndTask()
{
    ClaudeProcess process;

    // Set up some state
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    process.setCurrentTask(QStringLiteral("Some task"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QVERIFY(!process.currentTask().isEmpty());

    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);
    QSignalSpy finishedSpy(&process, &ClaudeProcess::taskFinished);

    process.reset();

    QCOMPARE(process.state(), ClaudeProcess::State::NotRunning);
    QVERIFY(process.currentTask().isEmpty());
    // reset() calls clearTask() which emits taskFinished, then setState(NotRunning)
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(stateSpy.count(), 1);
    QCOMPARE(stateSpy.at(0).at(0).value<ClaudeProcess::State>(), ClaudeProcess::State::NotRunning);
}

void ClaudeProcessHookEventTest::testResetFromWorking()
{
    ClaudeProcess process;

    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QVERIFY(process.isRunning());

    process.reset();

    QVERIFY(!process.isRunning());
    QCOMPARE(process.state(), ClaudeProcess::State::NotRunning);
}

// ================================================================
// PermissionRequest: tool_input variants
// ================================================================

void ClaudeProcessHookEventTest::testPermissionRequestToolInputObject()
{
    ClaudeProcess process;
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    QJsonObject inputObj;
    inputObj[QStringLiteral("file_path")] = QStringLiteral("/etc/passwd");

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Read");
    data[QStringLiteral("tool_input")] = inputObj;
    data[QStringLiteral("yolo_approved")] = false;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), toJson(data));

    QCOMPARE(permSpy.count(), 1);
    QCOMPARE(permSpy.at(0).at(0).toString(), QStringLiteral("Read"));
    // tool_input was an object, so it should be serialized to indented JSON
    QString toolInput = permSpy.at(0).at(1).toString();
    QVERIFY(toolInput.contains(QStringLiteral("file_path")));
    QVERIFY(toolInput.contains(QStringLiteral("/etc/passwd")));
}

void ClaudeProcessHookEventTest::testPermissionRequestToolInputArray()
{
    ClaudeProcess process;
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    QJsonArray inputArr;
    inputArr.append(QStringLiteral("arg1"));
    inputArr.append(QStringLiteral("arg2"));

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_input")] = inputArr;
    data[QStringLiteral("yolo_approved")] = false;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), toJson(data));

    QCOMPARE(permSpy.count(), 1);
    QString toolInput = permSpy.at(0).at(1).toString();
    QVERIFY(toolInput.contains(QStringLiteral("arg1")));
    QVERIFY(toolInput.contains(QStringLiteral("arg2")));
}

void ClaudeProcessHookEventTest::testPermissionRequestToolInputString()
{
    ClaudeProcess process;
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_input")] = QStringLiteral("rm -rf /tmp/test");
    data[QStringLiteral("yolo_approved")] = false;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), toJson(data));

    QCOMPARE(permSpy.count(), 1);
    QCOMPARE(permSpy.at(0).at(1).toString(), QStringLiteral("rm -rf /tmp/test"));
}

// ================================================================
// Full tool lifecycle sequences
// ================================================================

void ClaudeProcessHookEventTest::testPreToolUsePostToolUseStopLifecycle()
{
    ClaudeProcess process;
    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);
    QSignalSpy taskStartSpy(&process, &ClaudeProcess::taskStarted);
    QSignalSpy toolCompleteSpy(&process, &ClaudeProcess::toolUseCompleted);
    QSignalSpy taskFinishSpy(&process, &ClaudeProcess::taskFinished);

    // 1. PreToolUse -> Working
    QJsonObject preData;
    preData[QStringLiteral("tool_name")] = QStringLiteral("Edit");
    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(preData));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QCOMPARE(taskStartSpy.count(), 1);

    // 2. PostToolUse -> still Working (may have more tools)
    QJsonObject postData;
    postData[QStringLiteral("tool_name")] = QStringLiteral("Edit");
    postData[QStringLiteral("tool_response")] = QStringLiteral("File edited");
    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(postData));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QCOMPARE(toolCompleteSpy.count(), 1);

    // 3. Stop -> Idle
    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Idle);
    QCOMPARE(taskFinishSpy.count(), 1);

    // State transitions: NotRunning -> Working -> Idle (PostToolUse doesn't change state)
    QCOMPARE(stateSpy.count(), 2);
}

void ClaudeProcessHookEventTest::testMultipleToolUsesBeforeStop()
{
    ClaudeProcess process;
    QSignalSpy toolCompleteSpy(&process, &ClaudeProcess::toolUseCompleted);

    // Simulate: Read -> PostRead -> Write -> PostWrite -> Stop
    QJsonObject readPre;
    readPre[QStringLiteral("tool_name")] = QStringLiteral("Read");
    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(readPre));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    QJsonObject readPost;
    readPost[QStringLiteral("tool_name")] = QStringLiteral("Read");
    readPost[QStringLiteral("tool_response")] = QStringLiteral("file contents");
    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(readPost));

    QJsonObject writePre;
    writePre[QStringLiteral("tool_name")] = QStringLiteral("Write");
    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(writePre));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
    QCOMPARE(process.currentTask(), QStringLiteral("Using tool: Write"));

    QJsonObject writePost;
    writePost[QStringLiteral("tool_name")] = QStringLiteral("Write");
    writePost[QStringLiteral("tool_response")] = QStringLiteral("written");
    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(writePost));

    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Idle);

    // Two PostToolUse events -> two toolUseCompleted signals
    QCOMPARE(toolCompleteSpy.count(), 2);
    QCOMPARE(toolCompleteSpy.at(0).at(0).toString(), QStringLiteral("Read"));
    QCOMPARE(toolCompleteSpy.at(1).at(0).toString(), QStringLiteral("Write"));
}

void ClaudeProcessHookEventTest::testBashToolLifecycleSignals()
{
    ClaudeProcess process;
    QSignalSpy bashSpy(&process, &ClaudeProcess::bashToolStarted);
    QSignalSpy toolCompleteSpy(&process, &ClaudeProcess::toolUseCompleted);

    // PreToolUse(Bash) with command
    QJsonObject inputObj;
    inputObj[QStringLiteral("command")] = QStringLiteral("make -j4");

    QJsonObject preData;
    preData[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    preData[QStringLiteral("tool_input")] = inputObj;
    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(preData));

    QCOMPARE(bashSpy.count(), 1);
    QCOMPARE(bashSpy.at(0).at(0).toString(), QStringLiteral("make -j4"));

    // PostToolUse(Bash) with exit code response
    QJsonObject responseObj;
    responseObj[QStringLiteral("exit_code")] = 0;
    responseObj[QStringLiteral("stdout")] = QStringLiteral("Build successful");

    QJsonObject postData;
    postData[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    postData[QStringLiteral("tool_response")] = responseObj;
    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(postData));

    QCOMPARE(toolCompleteSpy.count(), 1);
    QCOMPARE(toolCompleteSpy.at(0).at(0).toString(), QStringLiteral("Bash"));
    QString response = toolCompleteSpy.at(0).at(1).toString();
    QVERIFY(response.contains(QStringLiteral("exit_code")));
    QVERIFY(response.contains(QStringLiteral("Build successful")));
}

// ================================================================
// Multi-agent scenarios
// ================================================================

void ClaudeProcessHookEventTest::testMultipleSubagentStartStop()
{
    ClaudeProcess process;
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);
    QSignalSpy stopSpy(&process, &ClaudeProcess::subagentStopped);

    // Start 3 subagents
    for (int i = 1; i <= 3; ++i) {
        QJsonObject data;
        data[QStringLiteral("agent_id")] = QStringLiteral("agent-%1").arg(i);
        data[QStringLiteral("agent_type")] = QStringLiteral("worker");
        data[QStringLiteral("transcript_path")] = QStringLiteral("/tmp/transcript-%1.md").arg(i);
        process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(data));
    }

    QCOMPARE(startSpy.count(), 3);

    // Stop them in reverse order
    for (int i = 3; i >= 1; --i) {
        QJsonObject data;
        data[QStringLiteral("agent_id")] = QStringLiteral("agent-%1").arg(i);
        data[QStringLiteral("agent_type")] = QStringLiteral("worker");
        data[QStringLiteral("agent_transcript_path")] = QStringLiteral("/tmp/transcript-%1.md").arg(i);
        process.handleHookEvent(QStringLiteral("SubagentStop"), toJson(data));
    }

    QCOMPARE(stopSpy.count(), 3);

    // Verify IDs are correct
    QCOMPARE(startSpy.at(0).at(0).toString(), QStringLiteral("agent-1"));
    QCOMPARE(startSpy.at(2).at(0).toString(), QStringLiteral("agent-3"));
    QCOMPARE(stopSpy.at(0).at(0).toString(), QStringLiteral("agent-3"));
    QCOMPARE(stopSpy.at(2).at(0).toString(), QStringLiteral("agent-1"));
}

void ClaudeProcessHookEventTest::testSubagentWithTeammateIdleAndTaskCompleted()
{
    ClaudeProcess process;
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);
    QSignalSpy idleSpy(&process, &ClaudeProcess::teammateIdle);
    QSignalSpy completedSpy(&process, &ClaudeProcess::taskCompleted);
    QSignalSpy stopSpy(&process, &ClaudeProcess::subagentStopped);

    // Full multi-agent lifecycle:
    // 1. SubagentStart
    QJsonObject startData;
    startData[QStringLiteral("agent_id")] = QStringLiteral("agent-worker-1");
    startData[QStringLiteral("agent_type")] = QStringLiteral("coder");
    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(startData));
    QCOMPARE(startSpy.count(), 1);

    // 2. TeammateIdle (subagent is waiting)
    QJsonObject idleData;
    idleData[QStringLiteral("teammate_name")] = QStringLiteral("coder-1");
    idleData[QStringLiteral("team_name")] = QStringLiteral("dev-team");
    process.handleHookEvent(QStringLiteral("TeammateIdle"), toJson(idleData));
    QCOMPARE(idleSpy.count(), 1);

    // 3. TaskCompleted
    QJsonObject completeData;
    completeData[QStringLiteral("task_id")] = QStringLiteral("task-impl");
    completeData[QStringLiteral("task_subject")] = QStringLiteral("Implement feature X");
    completeData[QStringLiteral("teammate_name")] = QStringLiteral("coder-1");
    completeData[QStringLiteral("team_name")] = QStringLiteral("dev-team");
    process.handleHookEvent(QStringLiteral("TaskCompleted"), toJson(completeData));
    QCOMPARE(completedSpy.count(), 1);

    // 4. SubagentStop
    QJsonObject stopData;
    stopData[QStringLiteral("agent_id")] = QStringLiteral("agent-worker-1");
    stopData[QStringLiteral("agent_type")] = QStringLiteral("coder");
    stopData[QStringLiteral("agent_transcript_path")] = QStringLiteral("/tmp/coder.md");
    process.handleHookEvent(QStringLiteral("SubagentStop"), toJson(stopData));
    QCOMPARE(stopSpy.count(), 1);
}

void ClaudeProcessHookEventTest::testInterleavedSubagentAndToolUse()
{
    ClaudeProcess process;
    QSignalSpy subagentStartSpy(&process, &ClaudeProcess::subagentStarted);
    QSignalSpy toolCompleteSpy(&process, &ClaudeProcess::toolUseCompleted);
    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);

    // Subagent starts while tools are being used
    QJsonObject preData;
    preData[QStringLiteral("tool_name")] = QStringLiteral("Read");
    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(preData));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    // Subagent starts mid-tool-use
    QJsonObject subData;
    subData[QStringLiteral("agent_id")] = QStringLiteral("agent-mid");
    subData[QStringLiteral("agent_type")] = QStringLiteral("analyzer");
    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(subData));
    QCOMPARE(subagentStartSpy.count(), 1);
    // State should still be Working (SubagentStart doesn't change state)
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    // PostToolUse
    QJsonObject postData;
    postData[QStringLiteral("tool_name")] = QStringLiteral("Read");
    postData[QStringLiteral("tool_response")] = QStringLiteral("contents");
    process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(postData));
    QCOMPARE(toolCompleteSpy.count(), 1);
}

// ================================================================
// Permission flow sequences
// ================================================================

void ClaudeProcessHookEventTest::testPermissionThenPreToolUseThenStop()
{
    ClaudeProcess process;

    // PreToolUse -> Working
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    // PermissionRequest (non-yolo) -> WaitingInput
    QJsonObject permData;
    permData[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    permData[QStringLiteral("tool_input")] = QStringLiteral("dangerous command");
    permData[QStringLiteral("yolo_approved")] = false;
    process.handleHookEvent(QStringLiteral("PermissionRequest"), toJson(permData));
    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);

    // User approves, Claude uses the tool -> PreToolUse -> Working
    QJsonObject toolData;
    toolData[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(toolData));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    // Stop -> Idle
    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Idle);
}

void ClaudeProcessHookEventTest::testYoloThenManualThenYoloSequence()
{
    ClaudeProcess process;
    QSignalSpy yoloSpy(&process, &ClaudeProcess::yoloApprovalOccurred);
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    // Yolo approval
    QJsonObject data1;
    data1[QStringLiteral("tool_name")] = QStringLiteral("Read");
    data1[QStringLiteral("yolo_approved")] = true;
    process.handleHookEvent(QStringLiteral("PermissionRequest"), toJson(data1));
    QCOMPARE(yoloSpy.count(), 1);
    QCOMPARE(permSpy.count(), 0);

    // Manual approval needed
    QJsonObject data2;
    data2[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data2[QStringLiteral("yolo_approved")] = false;
    process.handleHookEvent(QStringLiteral("PermissionRequest"), toJson(data2));
    QCOMPARE(yoloSpy.count(), 1);
    QCOMPARE(permSpy.count(), 1);
    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);

    // Another yolo approval after manual (e.g., user re-enabled yolo)
    QJsonObject data3;
    data3[QStringLiteral("tool_name")] = QStringLiteral("Write");
    data3[QStringLiteral("yolo_approved")] = true;
    process.handleHookEvent(QStringLiteral("PermissionRequest"), toJson(data3));
    QCOMPARE(yoloSpy.count(), 2);
    QCOMPARE(permSpy.count(), 1);
    // State should still be WaitingInput (yolo doesn't change state)
    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
}

// ================================================================
// Rapid-fire edge cases
// ================================================================

void ClaudeProcessHookEventTest::testRapidFireSubagentEvents()
{
    ClaudeProcess process;
    QSignalSpy startSpy(&process, &ClaudeProcess::subagentStarted);
    QSignalSpy stopSpy(&process, &ClaudeProcess::subagentStopped);

    // Rapid start/stop 20 subagents
    for (int i = 0; i < 20; ++i) {
        QJsonObject startData;
        startData[QStringLiteral("agent_id")] = QStringLiteral("rapid-%1").arg(i);
        startData[QStringLiteral("agent_type")] = QStringLiteral("worker");
        process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(startData));

        QJsonObject stopData;
        stopData[QStringLiteral("agent_id")] = QStringLiteral("rapid-%1").arg(i);
        stopData[QStringLiteral("agent_type")] = QStringLiteral("worker");
        process.handleHookEvent(QStringLiteral("SubagentStop"), toJson(stopData));
    }

    QCOMPARE(startSpy.count(), 20);
    QCOMPARE(stopSpy.count(), 20);
}

void ClaudeProcessHookEventTest::testRapidFireToolUseEvents()
{
    ClaudeProcess process;
    QSignalSpy toolCompleteSpy(&process, &ClaudeProcess::toolUseCompleted);
    QSignalSpy taskStartSpy(&process, &ClaudeProcess::taskStarted);

    // 50 rapid PreToolUse/PostToolUse cycles
    for (int i = 0; i < 50; ++i) {
        QJsonObject preData;
        preData[QStringLiteral("tool_name")] = QStringLiteral("Tool_%1").arg(i);
        process.handleHookEvent(QStringLiteral("PreToolUse"), toJson(preData));

        QJsonObject postData;
        postData[QStringLiteral("tool_name")] = QStringLiteral("Tool_%1").arg(i);
        postData[QStringLiteral("tool_response")] = QStringLiteral("ok");
        process.handleHookEvent(QStringLiteral("PostToolUse"), toJson(postData));
    }

    QCOMPARE(toolCompleteSpy.count(), 50);
    QCOMPARE(taskStartSpy.count(), 50);
    QCOMPARE(process.state(), ClaudeProcess::State::Working);
}

void ClaudeProcessHookEventTest::testMixedEventRapidFire()
{
    ClaudeProcess process;
    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);

    // Mix of different event types in rapid succession
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}")); // -> Working
    process.handleHookEvent(QStringLiteral("PostToolUse"), QStringLiteral("{\"tool_name\":\"Read\"}")); // stays Working
    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}")); // -> Idle

    QJsonObject subData;
    subData[QStringLiteral("agent_id")] = QStringLiteral("mix-agent");
    subData[QStringLiteral("agent_type")] = QStringLiteral("analyzer");
    process.handleHookEvent(QStringLiteral("SubagentStart"), toJson(subData)); // no state change

    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}")); // -> Working

    QJsonObject permData;
    permData[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    permData[QStringLiteral("yolo_approved")] = false;
    process.handleHookEvent(QStringLiteral("PermissionRequest"), toJson(permData)); // -> WaitingInput

    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}")); // -> Working
    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}")); // -> Idle

    QCOMPARE(process.state(), ClaudeProcess::State::Idle);

    // State transitions: Working, Idle, Working, WaitingInput, Working, Idle = 6
    QCOMPARE(stateSpy.count(), 6);
}

// ================================================================
// shortModelName
// ================================================================

void ClaudeProcessHookEventTest::testShortModelName()
{
    QCOMPARE(ClaudeProcess::shortModelName(ClaudeProcess::Model::Opus), QStringLiteral("opus"));
    QCOMPARE(ClaudeProcess::shortModelName(ClaudeProcess::Model::Sonnet), QStringLiteral("sonnet"));
    QCOMPARE(ClaudeProcess::shortModelName(ClaudeProcess::Model::Haiku), QStringLiteral("haiku"));
    QCOMPARE(ClaudeProcess::shortModelName(ClaudeProcess::Model::Default), QString());
}

// ================================================================
// Notification: permission_required variant
// ================================================================

void ClaudeProcessHookEventTest::testNotificationPermissionRequired()
{
    ClaudeProcess process;
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);
    QSignalSpy notifSpy(&process, &ClaudeProcess::notificationReceived);

    QJsonObject data;
    data[QStringLiteral("type")] = QStringLiteral("permission_required");
    data[QStringLiteral("action")] = QStringLiteral("delete_file");
    data[QStringLiteral("description")] = QStringLiteral("Delete /tmp/test");
    data[QStringLiteral("message")] = QStringLiteral("Permission required");

    process.handleHookEvent(QStringLiteral("Notification"), toJson(data));

    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
    QCOMPARE(permSpy.count(), 1);
    QCOMPARE(permSpy.at(0).at(0).toString(), QStringLiteral("delete_file"));
    QCOMPARE(permSpy.at(0).at(1).toString(), QStringLiteral("Delete /tmp/test"));
    QCOMPARE(notifSpy.count(), 1);
}

// ================================================================
// Notification: idle variant
// ================================================================

void ClaudeProcessHookEventTest::testNotificationIdleVariant()
{
    ClaudeProcess process;

    QJsonObject data;
    data[QStringLiteral("type")] = QStringLiteral("idle");
    data[QStringLiteral("message")] = QStringLiteral("Claude is idle");

    process.handleHookEvent(QStringLiteral("Notification"), toJson(data));

    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
}

QTEST_GUILESS_MAIN(ClaudeProcessHookEventTest)

#include "moc_ClaudeProcessHookEventTest.cpp"
