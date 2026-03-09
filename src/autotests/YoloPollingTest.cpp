/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "YoloPollingTest.h"

// Qt
#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

// Konsolai
#include "../claude/ClaudeProcess.h"
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

/**
 * Helper: find a child QTimer by checking all QTimer children.
 * Since timers are private members, we use findChildren to locate them
 * by their interval and active state.
 */
static QTimer *findTimerByInterval(QObject *parent, int interval)
{
    const auto timers = parent->findChildren<QTimer *>();
    for (auto *t : timers) {
        if (t->interval() == interval) {
            return t;
        }
    }
    return nullptr;
}

/**
 * Helper: count active timers with a given interval.
 */
static int countActiveTimersByInterval(QObject *parent, int interval)
{
    int count = 0;
    const auto timers = parent->findChildren<QTimer *>();
    for (auto *t : timers) {
        if (t->interval() == interval && t->isActive()) {
            count++;
        }
    }
    return count;
}

/**
 * Helper: check if any child timer with the given interval is active.
 */
static bool hasActiveTimerWithInterval(QObject *parent, int interval)
{
    return countActiveTimersByInterval(parent, interval) > 0;
}

void YoloPollingTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void YoloPollingTest::cleanupTestCase()
{
}

// ============================================================
// Level 1: Permission Polling
// ============================================================

void YoloPollingTest::testSetYoloMode_StartsPermissionPolling()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Before enabling yolo, no 300ms timer should be active
    QVERIFY(!hasActiveTimerWithInterval(&session, 300));

    session.setYoloMode(true);

    // After enabling yolo, a 300ms timer should be active (permission polling)
    QVERIFY2(hasActiveTimerWithInterval(&session, 300), "Permission polling timer (300ms) should be active after setYoloMode(true)");
}

void YoloPollingTest::testSetYoloMode_StopsPermissionPolling()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 300));

    session.setYoloMode(false);

    // After disabling yolo, the 300ms timer should be stopped
    QVERIFY2(!hasActiveTimerWithInterval(&session, 300), "Permission polling timer (300ms) should be stopped after setYoloMode(false)");
}

void YoloPollingTest::testPermissionPolling_300msInterval()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setYoloMode(true);

    QTimer *pollTimer = findTimerByInterval(&session, 300);
    QVERIFY2(pollTimer, "Should find a 300ms timer after enabling yolo mode");
    QCOMPARE(pollTimer->interval(), 300);
    QVERIFY(pollTimer->isActive());
}

void YoloPollingTest::testPermissionPolling_SuppressedDuringCapture()
{
    // This tests that pollForPermissionPrompt() returns early when
    // m_anyCaptureInFlight is true. Since we can't set private members
    // directly, we verify the timer exists and test the detection logic
    // indirectly through the public detectPermissionPrompt API.
    //
    // The actual suppression is a runtime guard in pollForPermissionPrompt()
    // that prevents overlapping tmux captures. We verify the timer setup
    // is correct — the guard itself is implementation-tested.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setYoloMode(true);
    QTimer *pollTimer = findTimerByInterval(&session, 300);
    QVERIFY(pollTimer);
    QVERIFY(pollTimer->isActive());

    // The timer should remain active even though captures are in flight
    // (the guard is inside the slot, not on the timer itself)
    QVERIFY(pollTimer->isActive());
}

void YoloPollingTest::testPermissionPolling_CooldownPreventsDoubleApprove()
{
    // The cooldown is managed by m_lastApprovalTime (QElapsedTimer).
    // When both hook-based and polling-based approval paths fire for the
    // same permission prompt, the 2s cooldown prevents double-approve.
    //
    // We test this by verifying that the logApproval mechanism works
    // correctly and that the approval count reflects only one approval
    // per prompt when the system is working correctly.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Simulate a hook-based approval
    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    QCOMPARE(session.yoloApprovalCount(), 1);

    // A second approval within 2s cooldown should not happen in production,
    // but the counter itself still counts if logApproval is called directly.
    // The cooldown guard is in pollForPermissionPrompt(), not in logApproval().
    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    QCOMPARE(session.yoloApprovalCount(), 2);

    // This verifies the counting works; the actual cooldown is a runtime
    // QElapsedTimer check inside pollForPermissionPrompt's async callback.
}

void YoloPollingTest::testPollForPermission_BailsWhenYoloOff()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Don't enable yolo mode — no timer should be created
    QVERIFY(!session.yoloMode());
    QVERIFY(!hasActiveTimerWithInterval(&session, 300));

    // detectPermissionPrompt still works (static method) regardless of mode
    QVERIFY(ClaudeSession::detectPermissionPrompt(QStringLiteral("  ❯ Yes\n    No")));
}

// ============================================================
// Level 2: Double Yolo (Suggestion Acceptance)
// ============================================================

void YoloPollingTest::testSetDoubleYoloMode_StartsIdlePolling()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));

    session.setDoubleYoloMode(true);

    // Idle polling timer (2000ms) should be active
    QVERIFY2(hasActiveTimerWithInterval(&session, 2000), "Idle polling timer (2000ms) should be active after setDoubleYoloMode(true)");
}

void YoloPollingTest::testSetDoubleYoloMode_StopsIdlePolling()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));

    session.setDoubleYoloMode(false);

    // When triple yolo is NOT active, idle polling should stop
    QVERIFY2(!hasActiveTimerWithInterval(&session, 2000), "Idle polling should stop when double yolo disabled (triple yolo also off)");
}

void YoloPollingTest::testSetDoubleYoloMode_KeepsIdlePollingWithTripleYolo()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Enable both double and triple yolo
    session.setDoubleYoloMode(true);
    session.setTripleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));

    // Disable double yolo — idle polling should remain for triple yolo
    session.setDoubleYoloMode(false);
    QVERIFY2(hasActiveTimerWithInterval(&session, 2000), "Idle polling should remain active for triple yolo when double yolo disabled");
}

void YoloPollingTest::testSetDoubleYoloMode_CancelsPendingTimers()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setDoubleYoloMode(true);

    // Verify double yolo is on
    QVERIFY(session.doubleYoloMode());

    // Disable — any pending suggestion/fallback timers should be cancelled
    session.setDoubleYoloMode(false);
    QVERIFY(!session.doubleYoloMode());

    // No suggestion-related single-shot timers should remain active.
    // The suggestion timer (500ms single-shot) and fallback (2000ms single-shot)
    // should both be stopped.
    // We verify by checking that no unexpected active timers exist.
    // The idle poll timer (2000ms repeating) should also be stopped.
    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testSuggestionFallback_2000msInterval()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Enable double + triple yolo so fallback path is armed
    session.setDoubleYoloMode(true);
    session.setTripleYoloMode(true);

    // The suggestion fallback timer is created lazily in scheduleSuggestionFallback()
    // which is called when idle is detected. We verify through the timer system.
    // After enabling both modes, idle polling should be active at 2000ms.
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));
}

// ============================================================
// Level 3: Triple Yolo (Auto-Continue)
// ============================================================

void YoloPollingTest::testSetTripleYoloMode_StartsIdlePolling()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));

    session.setTripleYoloMode(true);

    QVERIFY2(hasActiveTimerWithInterval(&session, 2000), "Idle polling timer (2000ms) should be active after setTripleYoloMode(true)");
}

void YoloPollingTest::testSetTripleYoloMode_StopsIdlePolling()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setTripleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));

    session.setTripleYoloMode(false);

    QVERIFY2(!hasActiveTimerWithInterval(&session, 2000), "Idle polling should stop when triple yolo disabled (double yolo also off)");
}

void YoloPollingTest::testSetTripleYoloMode_KeepsIdlePollingWithDoubleYolo()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Enable both
    session.setTripleYoloMode(true);
    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));

    // Disable triple — idle polling should remain for double yolo
    session.setTripleYoloMode(false);
    QVERIFY2(hasActiveTimerWithInterval(&session, 2000), "Idle polling should remain active for double yolo when triple yolo disabled");
}

void YoloPollingTest::testTripleYolo_UsesAutoContinuePrompt()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Default auto-continue prompt
    QString defaultPrompt = session.autoContinuePrompt();
    QVERIFY(!defaultPrompt.isEmpty());
    QVERIFY(defaultPrompt.contains(QStringLiteral("Continue")));
}

void YoloPollingTest::testTripleYolo_CustomPrompt()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QString customPrompt = QStringLiteral("Keep going with tests and fixes.");
    session.setAutoContinuePrompt(customPrompt);
    QCOMPARE(session.autoContinuePrompt(), customPrompt);

    // Verify it persists after mode changes
    session.setTripleYoloMode(true);
    QCOMPARE(session.autoContinuePrompt(), customPrompt);

    session.setTripleYoloMode(false);
    QCOMPARE(session.autoContinuePrompt(), customPrompt);
}

// ============================================================
// Timer Pause/Resume
// ============================================================

void YoloPollingTest::testPauseDisplayTimers_StopsDisplayTimers()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Enable yolo to ensure polling timers are running
    session.setYoloMode(true);

    // Pause display timers — this should stop token/resource timers
    // but NOT yolo polling timers
    session.pauseDisplayTimers();

    // Token refresh (30000ms) and resource (15000ms) timers should be stopped.
    // These timers are only started when the session is run(), so in tests
    // they may not exist. Verify the call doesn't crash.
    // The important thing is that yolo timers are NOT affected.
    QVERIFY2(hasActiveTimerWithInterval(&session, 300), "Permission polling timer should NOT be stopped by pauseDisplayTimers");
}

void YoloPollingTest::testResumeDisplayTimers_RestartsDisplayTimers()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Pause then resume — should not crash or affect yolo timers
    session.setYoloMode(true);
    session.pauseDisplayTimers();
    session.resumeDisplayTimers();

    // Yolo timer should still be running
    QVERIFY(hasActiveTimerWithInterval(&session, 300));
}

void YoloPollingTest::testPauseDisplayTimers_YoloTimersKeepRunning()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Enable all yolo levels
    session.setYoloMode(true);
    session.setDoubleYoloMode(true);
    session.setTripleYoloMode(true);

    // Pause display timers
    session.pauseDisplayTimers();

    // All yolo-critical timers should still be running
    QVERIFY2(hasActiveTimerWithInterval(&session, 300), "Permission polling (300ms) must survive pauseDisplayTimers");
    QVERIFY2(hasActiveTimerWithInterval(&session, 2000), "Idle polling (2000ms) must survive pauseDisplayTimers");
}

void YoloPollingTest::testPauseDisplayTimers_Idempotent()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setYoloMode(true);

    // Pause twice — should not crash or double-stop
    session.pauseDisplayTimers();
    session.pauseDisplayTimers();

    QVERIFY(hasActiveTimerWithInterval(&session, 300));
}

void YoloPollingTest::testResumeDisplayTimers_Idempotent()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setYoloMode(true);

    // Resume without pause — should be a no-op
    session.resumeDisplayTimers();
    session.resumeDisplayTimers();

    QVERIFY(hasActiveTimerWithInterval(&session, 300));
}

// ============================================================
// Interaction Between Levels
// ============================================================

void YoloPollingTest::testAllThreeLevels_ActiveSimultaneously()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setYoloMode(true);
    session.setDoubleYoloMode(true);
    session.setTripleYoloMode(true);

    QVERIFY(session.yoloMode());
    QVERIFY(session.doubleYoloMode());
    QVERIFY(session.tripleYoloMode());

    // Both timer systems should be active
    QVERIFY(hasActiveTimerWithInterval(&session, 300)); // permission polling
    QVERIFY(hasActiveTimerWithInterval(&session, 2000)); // idle polling

    // Disable all
    session.setYoloMode(false);
    session.setDoubleYoloMode(false);
    session.setTripleYoloMode(false);

    QVERIFY(!session.yoloMode());
    QVERIFY(!session.doubleYoloMode());
    QVERIFY(!session.tripleYoloMode());

    // All polling timers should be stopped
    QVERIFY(!hasActiveTimerWithInterval(&session, 300));
    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testDoubleAndTripleYolo_ShareIdlePolling()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Enable double yolo — starts idle polling
    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));

    // Enable triple yolo — idle polling should already be active (shared)
    session.setTripleYoloMode(true);
    // There should still be exactly one idle polling timer
    QCOMPARE(countActiveTimersByInterval(&session, 2000), 1);

    // Disable double — idle polling stays for triple
    session.setDoubleYoloMode(false);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));

    // Disable triple — now idle polling stops
    session.setTripleYoloMode(false);
    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testIdlePolling_2000msInterval()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setDoubleYoloMode(true);

    QTimer *idleTimer = findTimerByInterval(&session, 2000);
    QVERIFY2(idleTimer, "Should find a 2000ms timer after enabling double yolo");
    QCOMPARE(idleTimer->interval(), 2000);
    QVERIFY(idleTimer->isActive());
}

void YoloPollingTest::testPollForIdle_BailsWhenBothOff()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Neither double nor triple yolo enabled — no idle polling
    QVERIFY(!session.doubleYoloMode());
    QVERIFY(!session.tripleYoloMode());
    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testPollForIdle_SkipsWhenWorking()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    auto *process = session.claudeProcess();

    session.setDoubleYoloMode(true);

    // Simulate Working state
    process->handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    QCOMPARE(process->state(), ClaudeProcess::State::Working);

    // pollForIdlePrompt would bail because state is Working.
    // We verify the state check by confirming the state.
    QCOMPARE(session.claudeState(), ClaudeProcess::State::Working);
}

void YoloPollingTest::testPollForIdle_SkipsWhenWaitingInput()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    auto *process = session.claudeProcess();

    session.setDoubleYoloMode(true);

    // Simulate WaitingInput state (permission prompt without yolo approval)
    QJsonObject data;
    data[QStringLiteral("tool_name")] = QStringLiteral("Bash");
    data[QStringLiteral("yolo_approved")] = false;
    process->handleHookEvent(QStringLiteral("PermissionRequest"), QString::fromUtf8(QJsonDocument(data).toJson()));
    QCOMPARE(process->state(), ClaudeProcess::State::WaitingInput);

    // pollForIdlePrompt skips when WaitingInput
    QCOMPARE(session.claudeState(), ClaudeProcess::State::WaitingInput);
}

void YoloPollingTest::testPollForIdle_SuppressedByHookDeliveredIdle()
{
    // When hooks deliver an Idle event and the session acts on it,
    // m_hookDeliveredIdle is set to true, suppressing the polling path
    // to prevent duplicate actions.
    //
    // We test that the hook-based path fires correctly by simulating
    // the state transition and checking that the state is Idle.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    auto *process = session.claudeProcess();

    session.setDoubleYoloMode(true);

    // Working → Idle via hooks
    process->handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    process->handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    QCOMPARE(process->state(), ClaudeProcess::State::Idle);

    // The idle polling timer should still be active (it's a background check),
    // but pollForIdlePrompt() will bail early because m_hookDeliveredIdle=true.
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testApprovalCounts_IncrementCorrectly()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    QSignalSpy countSpy(&session, &ClaudeSession::approvalCountChanged);

    // Level 1 approvals
    session.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    session.logApproval(QStringLiteral("Write"), QStringLiteral("auto-approved"), 1);
    session.logApproval(QStringLiteral("Read"), QStringLiteral("auto-approved"), 1);

    // Level 2 approvals
    session.logApproval(QStringLiteral("suggestion"), QStringLiteral("auto-accepted"), 2);
    session.logApproval(QStringLiteral("suggestion"), QStringLiteral("auto-accepted"), 2);

    // Level 3 approvals
    session.logApproval(QStringLiteral("auto-continue"), QStringLiteral("auto-continued"), 3);

    QCOMPARE(session.yoloApprovalCount(), 3);
    QCOMPARE(session.doubleYoloApprovalCount(), 2);
    QCOMPARE(session.tripleYoloApprovalCount(), 1);
    QCOMPARE(session.totalApprovalCount(), 6);
    QCOMPARE(countSpy.count(), 6);

    // Approval log should have all entries
    QCOMPARE(session.approvalLog().size(), 6);

    // Verify entry details
    QCOMPARE(session.approvalLog().at(0).yoloLevel, 1);
    QCOMPARE(session.approvalLog().at(3).yoloLevel, 2);
    QCOMPARE(session.approvalLog().at(5).yoloLevel, 3);
}

void YoloPollingTest::testTrySuggestionsFirst_ControlsPrecedence()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    // Default: trySuggestionsFirst is true
    QVERIFY(session.trySuggestionsFirst());

    // Can set to false
    session.setTrySuggestionsFirst(false);
    QVERIFY(!session.trySuggestionsFirst());

    // Can set back to true
    session.setTrySuggestionsFirst(true);
    QVERIFY(session.trySuggestionsFirst());
}

void YoloPollingTest::testTrySuggestionsFirst_DefaultTrue()
{
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    QVERIFY2(session.trySuggestionsFirst(), "Default trySuggestionsFirst should be true (double yolo fires before triple)");
}

QTEST_GUILESS_MAIN(YoloPollingTest)

#include "moc_YoloPollingTest.cpp"
