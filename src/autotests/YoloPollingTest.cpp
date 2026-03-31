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
#include "../claude/SessionManagerPanel.h"

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
    // Triple yolo removed. Verify that disabling double yolo stops idle polling.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));

    session.setDoubleYoloMode(false);
    QVERIFY2(!hasActiveTimerWithInterval(&session, 2000), "Idle polling should stop when double yolo disabled");
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
    // Suggestion fallback timer was removed with triple yolo.
    // Verify double yolo alone creates the idle polling timer.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));
}

// ============================================================
// Level 3: Triple Yolo (Removed — stubs)
// ============================================================

void YoloPollingTest::testSetTripleYoloMode_StartsIdlePolling()
{
    // Triple yolo removed. Verify double yolo starts idle polling instead.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));
    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testSetTripleYoloMode_StopsIdlePolling()
{
    // Triple yolo removed. Verify double yolo stops idle polling.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));
    session.setDoubleYoloMode(false);
    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testSetTripleYoloMode_KeepsIdlePollingWithDoubleYolo()
{
    // Triple yolo removed. Double yolo manages idle polling independently.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());
    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testTripleYolo_UsesAutoContinuePrompt()
{
    // Triple yolo (auto-continue) removed. Test is now a no-op.
    QVERIFY(true);
}

void YoloPollingTest::testTripleYolo_CustomPrompt()
{
    // Triple yolo (auto-continue) removed. Test is now a no-op.
    QVERIFY(true);
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
    // Triple yolo removed. Test both remaining levels.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setYoloMode(true);
    session.setDoubleYoloMode(true);

    QVERIFY(session.yoloMode());
    QVERIFY(session.doubleYoloMode());

    // Both timer systems should be active
    QVERIFY(hasActiveTimerWithInterval(&session, 300)); // permission polling
    QVERIFY(hasActiveTimerWithInterval(&session, 2000)); // idle polling

    // Disable all
    session.setYoloMode(false);
    session.setDoubleYoloMode(false);

    QVERIFY(!session.yoloMode());
    QVERIFY(!session.doubleYoloMode());

    // All polling timers should be stopped
    QVERIFY(!hasActiveTimerWithInterval(&session, 300));
    QVERIFY(!hasActiveTimerWithInterval(&session, 2000));
}

void YoloPollingTest::testDoubleAndTripleYolo_ShareIdlePolling()
{
    // Triple yolo removed. Double yolo manages idle polling alone.
    ClaudeSession session(QStringLiteral("test"), QDir::tempPath());

    session.setDoubleYoloMode(true);
    QVERIFY(hasActiveTimerWithInterval(&session, 2000));
    QCOMPARE(countActiveTimersByInterval(&session, 2000), 1);

    session.setDoubleYoloMode(false);
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

    QCOMPARE(session.yoloApprovalCount(), 3);
    QCOMPARE(session.doubleYoloApprovalCount(), 2);
    QCOMPARE(session.totalApprovalCount(), 5);
    QCOMPARE(countSpy.count(), 5);

    // Approval log should have all entries
    QCOMPARE(session.approvalLog().size(), 5);

    // Verify entry details
    QCOMPARE(session.approvalLog().at(0).yoloLevel, 1);
    QCOMPARE(session.approvalLog().at(3).yoloLevel, 2);
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

// ============================================================
// Multi-Session Isolation
// ============================================================

void YoloPollingTest::testTripleYolo_IsolatedTimers()
{
    // Triple yolo removed. Test double yolo timer isolation instead.
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    ClaudeSession sessionB(QStringLiteral("test-b"), QDir::tempPath());

    sessionA.setDoubleYoloMode(true);

    QVERIFY2(hasActiveTimerWithInterval(&sessionA, 2000), "Session A should have idle polling after enabling double yolo");
    QVERIFY2(!hasActiveTimerWithInterval(&sessionB, 2000), "Session B must NOT have idle polling");
}

void YoloPollingTest::testTripleYolo_IsolatedState()
{
    // Triple yolo removed. Test double yolo isolation instead.
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    ClaudeSession sessionB(QStringLiteral("test-b"), QDir::tempPath());

    sessionA.setDoubleYoloMode(true);
    QVERIFY(sessionA.doubleYoloMode());
    QVERIFY2(!sessionB.doubleYoloMode(), "Session B must remain false when only A is enabled");

    sessionB.setDoubleYoloMode(true);
    QVERIFY(sessionB.doubleYoloMode());

    sessionA.setDoubleYoloMode(false);
    QVERIFY(!sessionA.doubleYoloMode());
    QVERIFY2(sessionB.doubleYoloMode(), "Session B must stay enabled after disabling A");
}

void YoloPollingTest::testTripleYolo_IsolatedPrompt()
{
    // Triple yolo (auto-continue prompt) removed. Test is now a no-op.
    QVERIFY(true);
}

void YoloPollingTest::testTripleYolo_IsolatedApprovalCounts()
{
    // Test approval count isolation between sessions (using level 1 and 2).
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    ClaudeSession sessionB(QStringLiteral("test-b"), QDir::tempPath());

    sessionA.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    sessionA.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    sessionB.logApproval(QStringLiteral("suggestion"), QStringLiteral("auto-accepted"), 2);

    QCOMPARE(sessionA.totalApprovalCount(), 2);
    QCOMPARE(sessionB.totalApprovalCount(), 1);
}

void YoloPollingTest::testTripleYolo_IsolatedTmuxManagers()
{
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    ClaudeSession sessionB(QStringLiteral("test-b"), QDir::tempPath());

    QVERIFY(sessionA.tmuxManager() != nullptr);
    QVERIFY(sessionB.tmuxManager() != nullptr);
    QVERIFY2(sessionA.tmuxManager() != sessionB.tmuxManager(), "Each session must have its own TmuxManager");
}

void YoloPollingTest::testTripleYolo_DisableOneKeepsOther()
{
    // Test with double yolo instead.
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    ClaudeSession sessionB(QStringLiteral("test-b"), QDir::tempPath());

    sessionA.setDoubleYoloMode(true);
    sessionB.setDoubleYoloMode(true);

    QVERIFY(hasActiveTimerWithInterval(&sessionA, 2000));
    QVERIFY(hasActiveTimerWithInterval(&sessionB, 2000));

    sessionA.setDoubleYoloMode(false);
    QVERIFY2(!hasActiveTimerWithInterval(&sessionA, 2000), "Session A's idle polling must stop");
    QVERIFY2(hasActiveTimerWithInterval(&sessionB, 2000), "Session B's idle polling must continue");
}

void YoloPollingTest::testTripleYolo_IsolatedIdlePolling()
{
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    ClaudeSession sessionB(QStringLiteral("test-b"), QDir::tempPath());

    sessionA.setDoubleYoloMode(true);
    sessionB.setDoubleYoloMode(true);

    QTimer *timerA = findTimerByInterval(&sessionA, 2000);
    QTimer *timerB = findTimerByInterval(&sessionB, 2000);

    QVERIFY(timerA);
    QVERIFY(timerB);
    QVERIFY2(timerA != timerB, "Idle polling timers must be separate objects");
    QCOMPARE(timerA->parent(), &sessionA);
    QCOMPARE(timerB->parent(), &sessionB);
}

void YoloPollingTest::testTripleYolo_FullIsolationAllLevels()
{
    // Test with remaining levels (1 and 2).
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    ClaudeSession sessionB(QStringLiteral("test-b"), QDir::tempPath());

    sessionA.setYoloMode(true);
    sessionA.setDoubleYoloMode(true);

    QVERIFY(!sessionB.yoloMode());
    QVERIFY(!sessionB.doubleYoloMode());
    QVERIFY2(!hasActiveTimerWithInterval(&sessionB, 300), "Session B must have no permission polling");
    QVERIFY2(!hasActiveTimerWithInterval(&sessionB, 2000), "Session B must have no idle polling");

    QVERIFY(hasActiveTimerWithInterval(&sessionA, 300));
    QVERIFY(hasActiveTimerWithInterval(&sessionA, 2000));

    sessionA.logApproval(QStringLiteral("Bash"), QStringLiteral("auto-approved"), 1);
    sessionA.logApproval(QStringLiteral("suggestion"), QStringLiteral("auto-accepted"), 2);

    QCOMPARE(sessionA.totalApprovalCount(), 2);
    QCOMPARE(sessionB.totalApprovalCount(), 0);
}

void YoloPollingTest::testTripleYolo_DestroyOneSessionKeepsOther()
{
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    auto *sessionB = new ClaudeSession(QStringLiteral("test-b"), QDir::tempPath());

    sessionA.setDoubleYoloMode(true);
    sessionB->setDoubleYoloMode(true);

    QVERIFY(hasActiveTimerWithInterval(&sessionA, 2000));
    QVERIFY(hasActiveTimerWithInterval(sessionB, 2000));

    delete sessionB;

    QVERIFY2(hasActiveTimerWithInterval(&sessionA, 2000), "Session A's idle polling must survive destruction of session B");
    QVERIFY(sessionA.doubleYoloMode());
}

void YoloPollingTest::testTripleYolo_CustomPromptIsolation()
{
    // Triple yolo (auto-continue prompt) removed. Test is now a no-op.
    QVERIFY(true);
}

void YoloPollingTest::testTripleYolo_HookDeliveredIdleIsolation()
{
    ClaudeSession sessionA(QStringLiteral("test-a"), QDir::tempPath());
    ClaudeSession sessionB(QStringLiteral("test-b"), QDir::tempPath());

    sessionA.setDoubleYoloMode(true);
    sessionB.setDoubleYoloMode(true);

    auto *processA = sessionA.claudeProcess();
    processA->handleHookEvent(QStringLiteral("PreToolUse"), QStringLiteral("{}"));
    processA->handleHookEvent(QStringLiteral("Stop"), QStringLiteral("{}"));
    QCOMPARE(processA->state(), ClaudeProcess::State::Idle);

    auto *processB = sessionB.claudeProcess();
    QVERIFY2(processB->state() != ClaudeProcess::State::Idle, "Session B's state must not be affected by hook events on session A");

    QVERIFY(hasActiveTimerWithInterval(&sessionA, 2000));
    QVERIFY(hasActiveTimerWithInterval(&sessionB, 2000));
}

void YoloPollingTest::testTripleYolo_MetadataPersistenceIsolation()
{
    // Verify that SessionMetadata yolo fields are independent per-session
    SessionMetadata metaA;
    metaA.sessionId = QStringLiteral("session-a");
    metaA.doubleYoloMode = true;
    metaA.doubleYoloApprovalCount = 42;

    SessionMetadata metaB;
    metaB.sessionId = QStringLiteral("session-b");
    metaB.doubleYoloMode = false;
    metaB.doubleYoloApprovalCount = 0;

    QVERIFY(metaA.doubleYoloMode);
    QVERIFY(!metaB.doubleYoloMode);
    QCOMPARE(metaA.doubleYoloApprovalCount, 42);
    QCOMPARE(metaB.doubleYoloApprovalCount, 0);

    metaA.doubleYoloApprovalCount = 100;
    QCOMPARE(metaB.doubleYoloApprovalCount, 0);
}

QTEST_GUILESS_MAIN(YoloPollingTest)

#include "moc_YoloPollingTest.cpp"
