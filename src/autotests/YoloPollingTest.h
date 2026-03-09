/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef YOLOPOLLINGTEST_H
#define YOLOPOLLINGTEST_H

#include <QObject>

namespace Konsolai
{

class YoloPollingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

    // ========== Level 1: Permission Polling ==========

    // setYoloMode(true) starts permission polling timer
    void testSetYoloMode_StartsPermissionPolling();

    // setYoloMode(false) stops permission polling timer
    void testSetYoloMode_StopsPermissionPolling();

    // Permission polling timer has 300ms interval
    void testPermissionPolling_300msInterval();

    // Permission polling is suppressed while m_anyCaptureInFlight is true
    void testPermissionPolling_SuppressedDuringCapture();

    // After yolo approval, cooldown prevents double-approve (m_lastApprovalTime)
    void testPermissionPolling_CooldownPreventsDoubleApprove();

    // pollForPermissionPrompt bails when yoloMode is false
    void testPollForPermission_BailsWhenYoloOff();

    // ========== Level 2: Double Yolo (Suggestion Acceptance) ==========

    // setDoubleYoloMode(true) starts idle polling
    void testSetDoubleYoloMode_StartsIdlePolling();

    // setDoubleYoloMode(false) stops idle polling (when triple not active)
    void testSetDoubleYoloMode_StopsIdlePolling();

    // setDoubleYoloMode(false) keeps idle polling if triple yolo is on
    void testSetDoubleYoloMode_KeepsIdlePollingWithTripleYolo();

    // When double yolo disabled, pending suggestion timers are cancelled
    void testSetDoubleYoloMode_CancelsPendingTimers();

    // Suggestion fallback timer is single-shot with 2000ms interval
    void testSuggestionFallback_2000msInterval();

    // ========== Level 3: Triple Yolo (Auto-Continue) ==========

    // setTripleYoloMode(true) starts idle polling
    void testSetTripleYoloMode_StartsIdlePolling();

    // setTripleYoloMode(false) stops idle polling (when double not active)
    void testSetTripleYoloMode_StopsIdlePolling();

    // setTripleYoloMode(false) keeps idle polling if double yolo is on
    void testSetTripleYoloMode_KeepsIdlePollingWithDoubleYolo();

    // Auto-continue uses m_autoContinuePrompt text
    void testTripleYolo_UsesAutoContinuePrompt();

    // Custom auto-continue prompt is preserved
    void testTripleYolo_CustomPrompt();

    // ========== Timer Pause/Resume ==========

    // pauseDisplayTimers() stops token refresh and resource timers
    void testPauseDisplayTimers_StopsDisplayTimers();

    // resumeDisplayTimers() restarts display timers
    void testResumeDisplayTimers_RestartsDisplayTimers();

    // Yolo-critical timers keep running during display pause
    void testPauseDisplayTimers_YoloTimersKeepRunning();

    // pauseDisplayTimers is idempotent
    void testPauseDisplayTimers_Idempotent();

    // resumeDisplayTimers is idempotent
    void testResumeDisplayTimers_Idempotent();

    // ========== Interaction Between Levels ==========

    // All three yolo levels can be active simultaneously
    void testAllThreeLevels_ActiveSimultaneously();

    // Both double and triple yolo share idle polling timer
    void testDoubleAndTripleYolo_ShareIdlePolling();

    // Idle polling has 2000ms interval
    void testIdlePolling_2000msInterval();

    // pollForIdlePrompt bails when both double and triple are off
    void testPollForIdle_BailsWhenBothOff();

    // pollForIdlePrompt skips when state is Working
    void testPollForIdle_SkipsWhenWorking();

    // pollForIdlePrompt skips when state is WaitingInput
    void testPollForIdle_SkipsWhenWaitingInput();

    // hookDeliveredIdle suppresses idle polling
    void testPollForIdle_SuppressedByHookDeliveredIdle();

    // Approval counts increment correctly for each level
    void testApprovalCounts_IncrementCorrectly();

    // trySuggestionsFirst controls double vs triple precedence
    void testTrySuggestionsFirst_ControlsPrecedence();

    // Default trySuggestionsFirst is true
    void testTrySuggestionsFirst_DefaultTrue();
};

}

#endif // YOLOPOLLINGTEST_H
