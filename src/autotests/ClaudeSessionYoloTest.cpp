/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "ClaudeSessionYoloTest.h"

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

QTEST_GUILESS_MAIN(ClaudeSessionYoloTest)

#include "moc_ClaudeSessionYoloTest.cpp"
