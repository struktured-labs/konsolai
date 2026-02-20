/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeSessionYoloTest.h"

// Qt
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/ClaudeProcess.h"
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

void ClaudeSessionYoloTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void ClaudeSessionYoloTest::cleanupTestCase()
{
}

// ============================================================
// detectPermissionPrompt tests
// ============================================================

void ClaudeSessionYoloTest::testDetectPermissionPrompt_YesBasic()
{
    // Compact form: ❯ Yes
    QString output = QStringLiteral("  ❯ Yes\n    No\n    Always allow");
    QVERIFY(ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_YesVerbose()
{
    // Verbose form: ❯ Yes, allow once
    QString output = QStringLiteral("  ❯ Yes, allow once\n    Always allow\n    Deny");
    QVERIFY(ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_AllowOnce()
{
    // Alternate wording: ❯ Allow once
    QString output = QStringLiteral("  ❯ Allow once\n    Always allow for this project\n    Deny");
    QVERIFY(ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_AlwaysAllow()
{
    // When "Always allow" is pre-selected (e.g., after navigating down)
    QString output = QStringLiteral("    Yes\n  ❯ Always allow\n    Deny");
    QVERIFY(ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_CaseInsensitive()
{
    // Lowercase variants
    QString output1 = QStringLiteral("  ❯ yes\n    no");
    QVERIFY(ClaudeSession::detectPermissionPrompt(output1));

    QString output2 = QStringLiteral("  ❯ allow once\n    deny");
    QVERIFY(ClaudeSession::detectPermissionPrompt(output2));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_WithAnsiCodes()
{
    // ANSI escape codes between the selector and keyword
    // ESC[1m = bold, ESC[0m = reset
    QString output = QStringLiteral("  \x1b[1m❯\x1b[0m \x1b[32mYes\x1b[0m\n    No\n    Always allow");
    QVERIFY(ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_NoMatch_IdlePrompt()
{
    // Idle prompt should NOT match as permission prompt
    QString output = QStringLiteral(">");
    QVERIFY(!ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_NoMatch_EmptyOutput()
{
    QVERIFY(!ClaudeSession::detectPermissionPrompt(QString()));
    QVERIFY(!ClaudeSession::detectPermissionPrompt(QStringLiteral("")));
    QVERIFY(!ClaudeSession::detectPermissionPrompt(QStringLiteral("\n\n")));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_NoMatch_NoSelector()
{
    // Has "Yes" but no selector arrow
    QString output = QStringLiteral("  Yes\n  No\n  Always allow");
    QVERIFY(!ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_NoMatch_SelectorWithoutKeyword()
{
    // Selector arrow but no Yes/Allow keyword
    QString output = QStringLiteral("  ❯ Run tests\n    Skip\n    Cancel");
    QVERIFY(!ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_MultiLine()
{
    // Full multi-line Claude Code permission output
    QString output = QStringLiteral(
        "Claude wants to use Bash\n"
        "  Command: git status\n"
        "\n"
        "  ❯ Yes, allow once\n"
        "    Always allow for this project\n"
        "    Deny\n");
    QVERIFY(ClaudeSession::detectPermissionPrompt(output));
}

// ============================================================
// detectIdlePrompt tests
// ============================================================

void ClaudeSessionYoloTest::testDetectIdlePrompt_BasicCaret()
{
    // Simple ">" prompt
    QString output = QStringLiteral(">");
    QVERIFY(ClaudeSession::detectIdlePrompt(output));
}

void ClaudeSessionYoloTest::testDetectIdlePrompt_CaretWithSpace()
{
    // "> " with trailing space (common format)
    QString output = QStringLiteral("> ");
    QVERIFY(ClaudeSession::detectIdlePrompt(output));
}

void ClaudeSessionYoloTest::testDetectIdlePrompt_ProjectPrefixed()
{
    // Project-prefixed prompt like "project-name >"
    QString output = QStringLiteral("konsolai > ");
    // The prompt starts with ">", but this has a prefix.
    // detectIdlePrompt checks startsWith(">") so this should match.
    QVERIFY(ClaudeSession::detectIdlePrompt(output));
}

void ClaudeSessionYoloTest::testDetectIdlePrompt_NoMatch_PermissionPrompt()
{
    // Permission prompt should NOT match as idle
    QString output = QStringLiteral("  ❯ Yes\n    No\n    Always allow");
    QVERIFY(!ClaudeSession::detectIdlePrompt(output));
}

void ClaudeSessionYoloTest::testDetectIdlePrompt_NoMatch_AllowDenyPresent()
{
    // Lines containing "Allow" or "Deny" should disqualify
    QString output = QStringLiteral("Allow once\n    Deny\n>");
    QVERIFY(!ClaudeSession::detectIdlePrompt(output));
}

void ClaudeSessionYoloTest::testDetectIdlePrompt_NoMatch_EmptyOutput()
{
    QVERIFY(!ClaudeSession::detectIdlePrompt(QString()));
    QVERIFY(!ClaudeSession::detectIdlePrompt(QStringLiteral("")));
    QVERIFY(!ClaudeSession::detectIdlePrompt(QStringLiteral("\n\n")));
}

void ClaudeSessionYoloTest::testDetectIdlePrompt_NoMatch_WorkingOutput()
{
    // Claude working output should not match
    QString output = QStringLiteral("Reading file src/main.cpp...\nDone processing 3 files.");
    QVERIFY(!ClaudeSession::detectIdlePrompt(output));
}

void ClaudeSessionYoloTest::testDetectIdlePrompt_TrailingBlankLines()
{
    // Idle prompt with trailing blank lines (trimmed output)
    QString output = QStringLiteral(">\n\n\n");
    QVERIFY(ClaudeSession::detectIdlePrompt(output));
}

// ============================================================
// PermissionRequest hook event tests
// ============================================================

void ClaudeSessionYoloTest::testPermissionRequest_YoloApproved()
{
    ClaudeProcess process;

    QSignalSpy yoloSpy(&process, &ClaudeProcess::yoloApprovalOccurred);
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_input")] = QStringLiteral("git status");
    data[QStringLiteral("yolo_approved")] = true;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(data).toJson()));

    // Should emit yoloApprovalOccurred, NOT permissionRequested
    QCOMPARE(yoloSpy.count(), 1);
    QCOMPARE(yoloSpy.at(0).at(0).toString(), QStringLiteral("Bash"));
    QCOMPARE(permSpy.count(), 0);

    // State should NOT be WaitingInput (it was auto-approved)
    QVERIFY(process.state() != ClaudeProcess::State::WaitingInput);
}

void ClaudeSessionYoloTest::testPermissionRequest_NotYoloApproved()
{
    ClaudeProcess process;

    QSignalSpy yoloSpy(&process, &ClaudeProcess::yoloApprovalOccurred);
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_input")] = QStringLiteral("rm -rf /");
    data[QStringLiteral("yolo_approved")] = false;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(data).toJson()));

    // Should emit permissionRequested, NOT yoloApprovalOccurred
    QCOMPARE(permSpy.count(), 1);
    QCOMPARE(permSpy.at(0).at(0).toString(), QStringLiteral("Bash"));
    QCOMPARE(yoloSpy.count(), 0);

    // State should be WaitingInput
    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
}

void ClaudeSessionYoloTest::testPermissionRequest_MissingYoloField()
{
    ClaudeProcess process;

    QSignalSpy yoloSpy(&process, &ClaudeProcess::yoloApprovalOccurred);
    QSignalSpy permSpy(&process, &ClaudeProcess::permissionRequested);

    // No yolo_approved field — should default to false (needs manual approval)
    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Read");
    data[QStringLiteral("tool_input")] = QStringLiteral("/etc/passwd");

    process.handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(permSpy.count(), 1);
    QCOMPARE(yoloSpy.count(), 0);
    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
}

// ============================================================
// State transition tests
// ============================================================

void ClaudeSessionYoloTest::testStateTransition_PreToolUseToStopBecomesIdle()
{
    ClaudeProcess process;

    QSignalSpy stateSpy(&process, &ClaudeProcess::stateChanged);

    // Working → Idle sequence (the path that triggers double/triple yolo)
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    process.handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Idle);

    // Verify both state transitions were emitted
    QCOMPARE(stateSpy.count(), 2);
    QCOMPARE(stateSpy.at(0).at(0).value<ClaudeProcess::State>(), ClaudeProcess::State::Working);
    QCOMPARE(stateSpy.at(1).at(0).value<ClaudeProcess::State>(), ClaudeProcess::State::Idle);
}

void ClaudeSessionYoloTest::testStateTransition_PermissionRequestWaitsInput()
{
    ClaudeProcess process;

    // Start in working state
    process.handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process.state(), ClaudeProcess::State::Working);

    // Permission request without yolo — should go to WaitingInput
    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Write");
    data[QStringLiteral("yolo_approved")] = false;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(process.state(), ClaudeProcess::State::WaitingInput);
}

// ============================================================
// Detection edge cases
// ============================================================

void ClaudeSessionYoloTest::testDetectPermissionPrompt_NoMatch_CrossLine()
{
    // ❯ on one line, "Yes" on a separate line — must NOT match.
    // The per-line check prevents cross-line false positives.
    QString output = QStringLiteral("  ❯ Run tests\n    Yes\n    No");
    QVERIFY(!ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectPermissionPrompt_NoMatch_SelectorOnDeny()
{
    // Selector on "Deny" line — should NOT match (no Yes/Allow keyword on that line)
    QString output = QStringLiteral("    Yes\n    Always allow\n  ❯ Deny");
    QVERIFY(!ClaudeSession::detectPermissionPrompt(output));
}

void ClaudeSessionYoloTest::testDetectIdlePrompt_NoMatch_CaretInMiddle()
{
    // ">" appears in middle of output but last non-empty line is normal text
    QString output = QStringLiteral("> old prompt\nReading file src/main.cpp...\nDone.");
    QVERIFY(!ClaudeSession::detectIdlePrompt(output));
}

// ============================================================
// Approval logging tests
// ============================================================

void ClaudeSessionYoloTest::testLogApproval_Level1()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    QCOMPARE(session.yoloApprovalCount(), 0);

    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    QCOMPARE(session.yoloApprovalCount(), 1);
    QCOMPARE(session.doubleYoloApprovalCount(), 0);
    QCOMPARE(session.tripleYoloApprovalCount(), 0);
}

void ClaudeSessionYoloTest::testLogApproval_Level2()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.logApproval(QStringLiteral("suggestion"), QStringLiteral("auto-accepted"), 2);
    QCOMPARE(session.yoloApprovalCount(), 0);
    QCOMPARE(session.doubleYoloApprovalCount(), 1);
    QCOMPARE(session.tripleYoloApprovalCount(), 0);
}

void ClaudeSessionYoloTest::testLogApproval_Level3()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.logApproval(QStringLiteral("auto-continue"), QStringLiteral("auto-continued"), 3);
    QCOMPARE(session.yoloApprovalCount(), 0);
    QCOMPARE(session.doubleYoloApprovalCount(), 0);
    QCOMPARE(session.tripleYoloApprovalCount(), 1);
}

void ClaudeSessionYoloTest::testLogApproval_AppendsToLog()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    QVERIFY(session.approvalLog().isEmpty());

    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    session.logApproval(QStringLiteral("Write"), QStringLiteral("auto-approved"), 1);
    session.logApproval(QStringLiteral("suggestion"), QStringLiteral("auto-accepted"), 2);

    QCOMPARE(session.approvalLog().size(), 3);

    // Check entry fields
    const auto &entry = session.approvalLog().at(0);
    QCOMPARE(entry.toolName, QStringLiteral("Bash"));
    QCOMPARE(entry.action, QStringLiteral("auto-approved"));
    QCOMPARE(entry.yoloLevel, 1);
    QVERIFY(entry.timestamp.isValid());
    // No token usage set on test session — should be zero
    QCOMPARE(entry.totalTokens, quint64(0));
    QCOMPARE(entry.estimatedCostUSD, 0.0);

    const auto &entry2 = session.approvalLog().at(2);
    QCOMPARE(entry2.toolName, QStringLiteral("suggestion"));
    QCOMPARE(entry2.yoloLevel, 2);
}

void ClaudeSessionYoloTest::testLogApproval_EmitsSignals()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QSignalSpy countSpy(&session, &ClaudeSession::approvalCountChanged);
    QSignalSpy logSpy(&session, &ClaudeSession::approvalLogged);

    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);

    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(logSpy.count(), 1);

    // Verify the logged entry is passed in the signal
    auto loggedEntry = logSpy.at(0).at(0).value<ApprovalLogEntry>();
    QCOMPARE(loggedEntry.toolName, QStringLiteral("Bash"));
    QCOMPARE(loggedEntry.yoloLevel, 1);
}

void ClaudeSessionYoloTest::testTotalApprovalCount()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    session.logApproval(QStringLiteral("suggestion"), QStringLiteral("auto-accepted"), 2);
    session.logApproval(QStringLiteral("auto-continue"), QStringLiteral("auto-continued"), 3);
    session.logApproval(QStringLiteral("auto-continue"), QStringLiteral("auto-continued"), 3);

    QCOMPARE(session.yoloApprovalCount(), 2);
    QCOMPARE(session.doubleYoloApprovalCount(), 1);
    QCOMPARE(session.tripleYoloApprovalCount(), 2);
    QCOMPARE(session.totalApprovalCount(), 5);
}

void ClaudeSessionYoloTest::testRestoreApprovalState()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Build a log to restore
    QVector<ApprovalLogEntry> log;
    ApprovalLogEntry e1;
    e1.timestamp = QDateTime::currentDateTime();
    e1.toolName = QStringLiteral("Bash");
    e1.action = QStringLiteral("auto-approved");
    e1.yoloLevel = 1;
    log.append(e1);

    ApprovalLogEntry e2;
    e2.timestamp = QDateTime::currentDateTime();
    e2.toolName = QStringLiteral("suggestion");
    e2.action = QStringLiteral("auto-accepted");
    e2.yoloLevel = 2;
    log.append(e2);

    QSignalSpy countSpy(&session, &ClaudeSession::approvalCountChanged);

    session.restoreApprovalState(5, 3, 2, log);

    QCOMPARE(session.yoloApprovalCount(), 5);
    QCOMPARE(session.doubleYoloApprovalCount(), 3);
    QCOMPARE(session.tripleYoloApprovalCount(), 2);
    QCOMPARE(session.totalApprovalCount(), 10);
    QCOMPARE(session.approvalLog().size(), 2);
    QCOMPARE(countSpy.count(), 1);
}

// ============================================================
// Tool input/output in approval log
// ============================================================

void ClaudeSessionYoloTest::testLogApproval_WithToolInput()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QString toolInput = QStringLiteral("{\n  \"command\": \"ls -la\"\n}");
    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1, toolInput);

    QCOMPARE(session.approvalLog().size(), 1);
    const auto &entry = session.approvalLog().at(0);
    QCOMPARE(entry.toolName, QStringLiteral("Bash"));
    QCOMPARE(entry.toolInput, toolInput);
    QVERIFY(entry.toolOutput.isEmpty());
}

void ClaudeSessionYoloTest::testYoloApproval_CarriesToolInput()
{
    ClaudeProcess process;

    QSignalSpy yoloSpy(&process, &ClaudeProcess::yoloApprovalOccurred);

    // Build a PermissionRequest with a JSON object tool_input
    QJsonObject inputObj;
    inputObj[QStringLiteral("command")] = QStringLiteral("git status");
    inputObj[QStringLiteral("description")] = QStringLiteral("Show git status");

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("tool_input")] = inputObj;
    data[QStringLiteral("yolo_approved")] = true;

    process.handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(yoloSpy.count(), 1);
    QCOMPARE(yoloSpy.at(0).at(0).toString(), QStringLiteral("Bash"));
    // Second argument should be the tool input as formatted JSON
    QString capturedInput = yoloSpy.at(0).at(1).toString();
    QVERIFY(capturedInput.contains(QStringLiteral("git status")));
    QVERIFY(capturedInput.contains(QStringLiteral("description")));
}

void ClaudeSessionYoloTest::testPostToolUse_EmitsToolUseCompleted()
{
    ClaudeProcess process;

    QSignalSpy completedSpy(&process, &ClaudeProcess::toolUseCompleted);

    // Build a PostToolUse event with tool_response
    QJsonObject responseObj;
    responseObj[QStringLiteral("success")] = true;
    responseObj[QStringLiteral("filePath")] = QStringLiteral("/tmp/test.txt");

    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Write");
    data[QStringLiteral("tool_response")] = responseObj;

    process.handleHookEvent(QStringLiteral("PostToolUse"), QString::fromUtf8(QJsonDocument(data).toJson()));

    QCOMPARE(completedSpy.count(), 1);
    QCOMPARE(completedSpy.at(0).at(0).toString(), QStringLiteral("Write"));
    QString capturedResponse = completedSpy.at(0).at(1).toString();
    QVERIFY(capturedResponse.contains(QStringLiteral("success")));
    QVERIFY(capturedResponse.contains(QStringLiteral("/tmp/test.txt")));
}

void ClaudeSessionYoloTest::testToolOutput_CorrelatedWithApproval()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    auto *process = session.claudeProcess();

    // Simulate: PermissionRequest (yolo-approved) → PostToolUse for same tool
    QJsonObject inputObj;
    inputObj[QStringLiteral("command")] = QStringLiteral("echo hello");

    QJsonObject permData;
    permData[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    permData[QStringLiteral("tool_input")] = inputObj;
    permData[QStringLiteral("yolo_approved")] = true;

    process->handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(permData).toJson()));

    // Should have logged the approval with toolInput
    QCOMPARE(session.approvalLog().size(), 1);
    QVERIFY(session.approvalLog().at(0).toolInput.contains(QStringLiteral("echo hello")));
    QVERIFY(session.approvalLog().at(0).toolOutput.isEmpty());

    // Now PostToolUse fires with tool_response
    QJsonObject responseObj;
    responseObj[QStringLiteral("stdout")] = QStringLiteral("hello\n");

    QJsonObject postData;
    postData[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    postData[QStringLiteral("tool_response")] = responseObj;

    process->handleHookEvent(QStringLiteral("PostToolUse"), QString::fromUtf8(QJsonDocument(postData).toJson()));

    // The tool output should now be attached to the approval entry
    QCOMPARE(session.approvalLog().size(), 1);
    QVERIFY(!session.approvalLog().at(0).toolOutput.isEmpty());
    QVERIFY(session.approvalLog().at(0).toolOutput.contains(QStringLiteral("hello")));
}

// ============================================================
// Yolo mode signal tests
// ============================================================

void ClaudeSessionYoloTest::testSetYoloMode_EmitsSignal()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QSignalSpy spy(&session, &ClaudeSession::yoloModeChanged);

    session.setYoloMode(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    session.setYoloMode(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);

    // Setting same value again should NOT emit
    session.setYoloMode(false);
    QCOMPARE(spy.count(), 2);
}

void ClaudeSessionYoloTest::testSetDoubleYoloMode_EmitsSignal()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QSignalSpy spy(&session, &ClaudeSession::doubleYoloModeChanged);

    session.setDoubleYoloMode(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    // Setting same value should NOT emit
    session.setDoubleYoloMode(true);
    QCOMPARE(spy.count(), 1);
}

void ClaudeSessionYoloTest::testSetTripleYoloMode_EmitsSignal()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QSignalSpy spy(&session, &ClaudeSession::tripleYoloModeChanged);

    session.setTripleYoloMode(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    session.setTripleYoloMode(false);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
}

// ============================================================
// Yolo file management tests
// ============================================================

void ClaudeSessionYoloTest::testSetYoloMode_CreatesAndRemovesFile()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Derive the expected yolo file path
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString yoloPath = dataDir + QStringLiteral("/konsolai/sessions/") + session.sessionId() + QStringLiteral(".yolo");

    // Ensure parent directory exists (hook handler normally does this in start())
    QDir().mkpath(QFileInfo(yoloPath).absolutePath());

    QVERIFY(!QFile::exists(yoloPath));

    session.setYoloMode(true);
    QVERIFY2(QFile::exists(yoloPath), qPrintable(QStringLiteral("Expected yolo file at: ") + yoloPath));

    session.setYoloMode(false);
    QVERIFY2(!QFile::exists(yoloPath), "Yolo file should be removed after disabling");
}

void ClaudeSessionYoloTest::testSetTripleYoloMode_CreatesAndRemovesTeamFile()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Derive the expected team yolo file path
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QString teamYoloPath = dataDir + QStringLiteral("/konsolai/sessions/") + session.sessionId() + QStringLiteral(".yolo-team");

    // Ensure parent directory exists
    QDir().mkpath(QFileInfo(teamYoloPath).absolutePath());

    QVERIFY(!QFile::exists(teamYoloPath));

    session.setTripleYoloMode(true);
    QVERIFY2(QFile::exists(teamYoloPath), qPrintable(QStringLiteral("Expected team yolo file at: ") + teamYoloPath));

    session.setTripleYoloMode(false);
    QVERIFY2(!QFile::exists(teamYoloPath), "Team yolo file should be removed after disabling");
}

// ============================================================
// hasActiveTeam and subagent tracking tests
// ============================================================

void ClaudeSessionYoloTest::testHasActiveTeam_NoSubagents()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // No subagents → no active team
    QVERIFY(!session.hasActiveTeam());
    QVERIFY(session.subagents().isEmpty());
}

void ClaudeSessionYoloTest::testHasActiveTeam_WithWorkingSubagent()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Simulate SubagentStart via the session's ClaudeProcess
    QJsonObject data;
    data[QStringLiteral("agent_id")] = QStringLiteral("agent-test-001");
    data[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");

    session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStart"), QString::fromUtf8(QJsonDocument(data).toJson()));

    // Should now have an active team
    QVERIFY(session.hasActiveTeam());
    QCOMPARE(session.subagents().size(), 1);
    QCOMPARE(session.subagents().value(QStringLiteral("agent-test-001")).agentType, QStringLiteral("general-purpose"));
    QCOMPARE(session.subagents().value(QStringLiteral("agent-test-001")).state, ClaudeProcess::State::Working);
}

void ClaudeSessionYoloTest::testHasActiveTeam_SubagentStoppedNoTeam()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Start a subagent
    QJsonObject startData;
    startData[QStringLiteral("agent_id")] = QStringLiteral("agent-test-002");
    startData[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStart"), QString::fromUtf8(QJsonDocument(startData).toJson()));
    QVERIFY(session.hasActiveTeam());

    // Stop the subagent
    QJsonObject stopData;
    stopData[QStringLiteral("agent_id")] = QStringLiteral("agent-test-002");
    stopData[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    stopData[QStringLiteral("agent_transcript_path")] = QStringLiteral("/tmp/done.jsonl");
    session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStop"), QString::fromUtf8(QJsonDocument(stopData).toJson()));

    // Team should no longer be active (only subagent is NotRunning)
    QVERIFY(!session.hasActiveTeam());
    QCOMPARE(session.subagents().size(), 1); // Entry remains but state is NotRunning
    QCOMPARE(session.subagents().value(QStringLiteral("agent-test-002")).state, ClaudeProcess::State::NotRunning);
}

void ClaudeSessionYoloTest::testHasActiveTeam_MultipleSubagentsOneStops()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Start two subagents
    for (const auto &id : {QStringLiteral("agent-a"), QStringLiteral("agent-b")}) {
        QJsonObject data;
        data[QStringLiteral("agent_id")] = id;
        data[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");
        session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStart"), QString::fromUtf8(QJsonDocument(data).toJson()));
    }
    QCOMPARE(session.subagents().size(), 2);
    QVERIFY(session.hasActiveTeam());

    // Stop only one
    QJsonObject stopData;
    stopData[QStringLiteral("agent_id")] = QStringLiteral("agent-a");
    stopData[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");
    session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStop"), QString::fromUtf8(QJsonDocument(stopData).toJson()));

    // Still has active team (agent-b is still Working)
    QVERIFY(session.hasActiveTeam());

    // Stop second
    stopData[QStringLiteral("agent_id")] = QStringLiteral("agent-b");
    session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStop"), QString::fromUtf8(QJsonDocument(stopData).toJson()));

    // No more active team
    QVERIFY(!session.hasActiveTeam());
}

void ClaudeSessionYoloTest::testSubagentTracking_TeamName()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Start a subagent
    QJsonObject startData;
    startData[QStringLiteral("agent_id")] = QStringLiteral("agent-team-1");
    startData[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");
    session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStart"), QString::fromUtf8(QJsonDocument(startData).toJson()));

    // TeammateIdle sets team name
    QJsonObject idleData;
    idleData[QStringLiteral("teammate_name")] = QStringLiteral("researcher");
    idleData[QStringLiteral("team_name")] = QStringLiteral("my-project-team");
    session.claudeProcess()->handleHookEvent(QStringLiteral("TeammateIdle"), QString::fromUtf8(QJsonDocument(idleData).toJson()));

    QCOMPARE(session.teamName(), QStringLiteral("my-project-team"));
}

void ClaudeSessionYoloTest::testSubagentTracking_TeammateIdle()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Start a subagent
    QJsonObject startData;
    startData[QStringLiteral("agent_id")] = QStringLiteral("agent-idle-1");
    startData[QStringLiteral("agent_type")] = QStringLiteral("Explore");
    session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStart"), QString::fromUtf8(QJsonDocument(startData).toJson()));

    QCOMPARE(session.subagents().value(QStringLiteral("agent-idle-1")).state, ClaudeProcess::State::Working);

    // TeammateIdle should set state to Idle and assign teammate name
    QJsonObject idleData;
    idleData[QStringLiteral("teammate_name")] = QStringLiteral("explorer");
    idleData[QStringLiteral("team_name")] = QStringLiteral("test-team");
    session.claudeProcess()->handleHookEvent(QStringLiteral("TeammateIdle"), QString::fromUtf8(QJsonDocument(idleData).toJson()));

    const auto &info = session.subagents().value(QStringLiteral("agent-idle-1"));
    QCOMPARE(info.state, ClaudeProcess::State::Idle);
    QCOMPARE(info.teammateName, QStringLiteral("explorer"));
    QVERIFY(info.lastUpdated.isValid());

    // Idle subagent should still count as active team
    QVERIFY(session.hasActiveTeam());
}

void ClaudeSessionYoloTest::testSubagentTracking_TaskCompleted()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QSignalSpy teamSpy(&session, &ClaudeSession::teamInfoChanged);

    // Start a subagent
    QJsonObject startData;
    startData[QStringLiteral("agent_id")] = QStringLiteral("agent-task-1");
    startData[QStringLiteral("agent_type")] = QStringLiteral("general-purpose");
    session.claudeProcess()->handleHookEvent(QStringLiteral("SubagentStart"), QString::fromUtf8(QJsonDocument(startData).toJson()));

    // Assign teammate name via TeammateIdle
    QJsonObject idleData;
    idleData[QStringLiteral("teammate_name")] = QStringLiteral("builder");
    idleData[QStringLiteral("team_name")] = QStringLiteral("build-team");
    session.claudeProcess()->handleHookEvent(QStringLiteral("TeammateIdle"), QString::fromUtf8(QJsonDocument(idleData).toJson()));

    // TaskCompleted should set task subject on the matching subagent
    QJsonObject taskData;
    taskData[QStringLiteral("task_id")] = QStringLiteral("task-42");
    taskData[QStringLiteral("task_subject")] = QStringLiteral("Implement auth module");
    taskData[QStringLiteral("teammate_name")] = QStringLiteral("builder");
    taskData[QStringLiteral("team_name")] = QStringLiteral("build-team");
    session.claudeProcess()->handleHookEvent(QStringLiteral("TaskCompleted"), QString::fromUtf8(QJsonDocument(taskData).toJson()));

    const auto &info = session.subagents().value(QStringLiteral("agent-task-1"));
    QCOMPARE(info.currentTaskSubject, QStringLiteral("Implement auth module"));

    // teamInfoChanged should have been emitted (TeammateIdle + TaskCompleted)
    QVERIFY(teamSpy.count() >= 2);
}

QTEST_GUILESS_MAIN(ClaudeSessionYoloTest)

#include "moc_ClaudeSessionYoloTest.cpp"
