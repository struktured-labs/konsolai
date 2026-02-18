/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QSignalSpy>
#include <QTest>

#include "SessionObserver.h"

using namespace Konsolai;

// ClaudeProcess::State values
static constexpr int StateIdle = 2;
static constexpr int StateWorking = 3;

class SessionObserverTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testIdleLoopDetection()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.idleLoopCycleThreshold = 3;
        cfg.idleLoopMinWorkSeconds = 15;
        cfg.idleLoopMinTokens = 5000;
        cfg.interventionCooldownSecs = 0;
        cfg.errorLoopEnabled = false; // isolate idle loop
        cfg.costSpiralEnabled = false;
        cfg.contextRotEnabled = false;
        cfg.permissionStormEnabled = false;
        cfg.subagentChurnEnabled = false;
        observer.setConfig(cfg);

        QSignalSpy spy(&observer, &SessionObserver::stuckDetected);
        QVERIFY(spy.isValid());

        // Simulate 3 quick Working->Idle cycles with no token progress
        for (int i = 0; i < 3; ++i) {
            observer.onStateChanged(StateWorking);
            observer.onStateChanged(StateIdle);
        }

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), static_cast<int>(StuckPattern::IdleLoop));
        QCOMPARE(spy.at(0).at(1).toInt(), 1); // severity 1
    }

    void testIdleLoopFalsePositive()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.idleLoopCycleThreshold = 3;
        cfg.idleLoopMinWorkSeconds = 15;
        cfg.idleLoopMinTokens = 5000;
        cfg.interventionCooldownSecs = 0;
        cfg.errorLoopEnabled = false;
        cfg.costSpiralEnabled = false;
        cfg.contextRotEnabled = false;
        cfg.permissionStormEnabled = false;
        cfg.subagentChurnEnabled = false;
        observer.setConfig(cfg);

        QSignalSpy spy(&observer, &SessionObserver::stuckDetected);
        QVERIFY(spy.isValid());

        // Simulate 3 cycles with large token deltas (legitimate work)
        for (int i = 0; i < 3; ++i) {
            observer.onStateChanged(StateWorking);
            // Add significant tokens between working and idle
            observer.onTokenUsageChanged(10000 * (i + 1), 5000 * (i + 1), 15000 * (i + 1), 0.1 * (i + 1));
            observer.onStateChanged(StateIdle);
        }

        // Check that IdleLoop was NOT triggered
        bool idleLoopTriggered = false;
        for (int i = 0; i < spy.count(); ++i) {
            if (spy.at(i).at(0).toInt() == static_cast<int>(StuckPattern::IdleLoop)) {
                idleLoopTriggered = true;
            }
        }
        QVERIFY(!idleLoopTriggered);
    }

    void testErrorLoopDetection()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.errorLoopCount = 3;
        cfg.errorLoopWindowSeconds = 300;
        cfg.interventionCooldownSecs = 0;
        cfg.idleLoopEnabled = false; // isolate error loop
        cfg.costSpiralEnabled = false;
        cfg.contextRotEnabled = false;
        cfg.permissionStormEnabled = false;
        cfg.subagentChurnEnabled = false;
        observer.setConfig(cfg);

        QSignalSpy spy(&observer, &SessionObserver::stuckDetected);
        QVERIFY(spy.isValid());

        // 3 quick Working->Idle transitions produce 3 error signatures
        for (int i = 0; i < 3; ++i) {
            observer.onStateChanged(StateWorking);
            observer.onStateChanged(StateIdle);
        }

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), static_cast<int>(StuckPattern::ErrorLoop));
        QCOMPARE(spy.at(0).at(1).toInt(), 2); // severity 2
    }

    void testCostSpiralDetection()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.costSpiralTokenThreshold = 100000;
        cfg.costSpiralCostThreshold = 1.0;
        cfg.costSpiralWindowSeconds = 300;
        cfg.interventionCooldownSecs = 0;
        cfg.idleLoopEnabled = false;
        cfg.errorLoopEnabled = false;
        cfg.contextRotEnabled = false;
        cfg.permissionStormEnabled = false;
        cfg.subagentChurnEnabled = false;
        observer.setConfig(cfg);

        QSignalSpy spy(&observer, &SessionObserver::stuckDetected);
        QVERIFY(spy.isValid());

        // First call initializes the cost window
        observer.onTokenUsageChanged(0, 0, 0, 0.0);

        // Large jump within same window: 150K tokens, $2.00
        observer.onTokenUsageChanged(100000, 50000, 150000, 2.0);

        bool costSpiralFound = false;
        for (int i = 0; i < spy.count(); ++i) {
            if (spy.at(i).at(0).toInt() == static_cast<int>(StuckPattern::CostSpiral)) {
                costSpiralFound = true;
            }
        }
        QVERIFY(costSpiralFound);
    }

    void testContextRotDetection()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.contextRotInputThreshold = 800000;
        cfg.contextRotOutputRatio = 0.5;
        cfg.interventionCooldownSecs = 0;
        cfg.idleLoopEnabled = false;
        cfg.errorLoopEnabled = false;
        cfg.costSpiralEnabled = false;
        cfg.permissionStormEnabled = false;
        cfg.subagentChurnEnabled = false;
        observer.setConfig(cfg);

        QSignalSpy spy(&observer, &SessionObserver::stuckDetected);
        QVERIFY(spy.isValid());

        // Provide 3 samples to establish initial ratio (~0.3)
        observer.onTokenUsageChanged(10000, 3000, 13000, 0.01);
        observer.onTokenUsageChanged(20000, 6000, 26000, 0.02);
        observer.onTokenUsageChanged(30000, 9000, 39000, 0.03);

        // Now degrade: high input, very low output ratio
        // currentRatio = 50000/900000 = 0.056
        // threshold = 0.3 * 0.5 = 0.15
        // 0.056 < 0.15 -> trigger
        observer.onTokenUsageChanged(900000, 50000, 950000, 1.0);

        bool contextRotFound = false;
        for (int i = 0; i < spy.count(); ++i) {
            if (spy.at(i).at(0).toInt() == static_cast<int>(StuckPattern::ContextRot)) {
                contextRotFound = true;
            }
        }
        QVERIFY(contextRotFound);
    }

    void testPermissionStormDetection()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.permStormCount = 10;
        cfg.permStormWindowSeconds = 30;
        cfg.permStormSameToolPercent = 60.0;
        cfg.interventionCooldownSecs = 0;
        cfg.idleLoopEnabled = false;
        cfg.errorLoopEnabled = false;
        cfg.costSpiralEnabled = false;
        cfg.contextRotEnabled = false;
        cfg.subagentChurnEnabled = false;
        observer.setConfig(cfg);

        QSignalSpy spy(&observer, &SessionObserver::stuckDetected);
        QVERIFY(spy.isValid());

        const auto now = QDateTime::currentDateTime();

        // Fire 11 approvals: 8 Bash (73%) + 3 Write
        for (int i = 0; i < 8; ++i) {
            observer.onApprovalLogged(QStringLiteral("Bash"), 1, now);
        }
        for (int i = 0; i < 3; ++i) {
            observer.onApprovalLogged(QStringLiteral("Write"), 1, now);
        }

        bool permStormFound = false;
        for (int i = 0; i < spy.count(); ++i) {
            if (spy.at(i).at(0).toInt() == static_cast<int>(StuckPattern::PermissionStorm)) {
                permStormFound = true;
            }
        }
        QVERIFY(permStormFound);
    }

    void testSubagentChurnDetection()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.subagentChurnCount = 5;
        cfg.subagentChurnWindowSeconds = 600;
        cfg.subagentChurnCompletionPercent = 20.0;
        cfg.interventionCooldownSecs = 0;
        cfg.idleLoopEnabled = false;
        cfg.errorLoopEnabled = false;
        cfg.costSpiralEnabled = false;
        cfg.contextRotEnabled = false;
        cfg.permissionStormEnabled = false;
        observer.setConfig(cfg);

        QSignalSpy spy(&observer, &SessionObserver::stuckDetected);
        QVERIFY(spy.isValid());

        // Start and immediately stop 5 agents (0s duration -> not completed -> 0% completion)
        for (int i = 0; i < 5; ++i) {
            QString agentId = QStringLiteral("agent-%1").arg(i);
            observer.onSubagentStarted(agentId);
            observer.onSubagentStopped(agentId);
        }

        bool churnFound = false;
        for (int i = 0; i < spy.count(); ++i) {
            if (spy.at(i).at(0).toInt() == static_cast<int>(StuckPattern::SubagentChurn)) {
                churnFound = true;
            }
        }
        QVERIFY(churnFound);
    }

    void testInterventionCooldown()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.idleLoopCycleThreshold = 3;
        cfg.idleLoopMinWorkSeconds = 15;
        cfg.idleLoopMinTokens = 5000;
        cfg.interventionCooldownSecs = 3600; // very long cooldown
        cfg.errorLoopEnabled = false;
        cfg.costSpiralEnabled = false;
        cfg.contextRotEnabled = false;
        cfg.permissionStormEnabled = false;
        cfg.subagentChurnEnabled = false;
        observer.setConfig(cfg);

        QSignalSpy spy(&observer, &SessionObserver::stuckDetected);
        QVERIFY(spy.isValid());

        // Trigger IdleLoop
        for (int i = 0; i < 3; ++i) {
            observer.onStateChanged(StateWorking);
            observer.onStateChanged(StateIdle);
        }
        QCOMPARE(spy.count(), 1);

        // Reset tracking state (but cooldowns persist)
        observer.reset();

        // Try to trigger again — should be blocked by cooldown
        for (int i = 0; i < 3; ++i) {
            observer.onStateChanged(StateWorking);
            observer.onStateChanged(StateIdle);
        }
        QCOMPARE(spy.count(), 1); // Still 1 — cooldown blocked re-trigger
    }

    void testComposedSeverity()
    {
        SessionObserver observer;
        ObserverConfig cfg;
        cfg.idleLoopCycleThreshold = 3;
        cfg.idleLoopMinWorkSeconds = 15;
        cfg.idleLoopMinTokens = 5000;
        cfg.errorLoopCount = 3;
        cfg.errorLoopWindowSeconds = 300;
        cfg.interventionCooldownSecs = 0;
        cfg.costSpiralEnabled = false;
        cfg.contextRotEnabled = false;
        cfg.permissionStormEnabled = false;
        cfg.subagentChurnEnabled = false;
        observer.setConfig(cfg);

        QCOMPARE(observer.composedSeverity(), 0);

        // 3 quick Working->Idle cycles trigger both IdleLoop (sev 1) and ErrorLoop (sev 2)
        for (int i = 0; i < 3; ++i) {
            observer.onStateChanged(StateWorking);
            observer.onStateChanged(StateIdle);
        }

        QCOMPARE(observer.composedSeverity(), 3); // 1 + 2
        QCOMPARE(observer.activeEvents().size(), 2);
    }

    void testCorrectivePrompts()
    {
        QVERIFY(!SessionObserver::correctivePrompt(StuckPattern::IdleLoop).isEmpty());
        QVERIFY(!SessionObserver::correctivePrompt(StuckPattern::ErrorLoop).isEmpty());
        QVERIFY(!SessionObserver::correctivePrompt(StuckPattern::CostSpiral).isEmpty());
        QVERIFY(!SessionObserver::correctivePrompt(StuckPattern::ContextRot).isEmpty());
        QVERIFY(!SessionObserver::correctivePrompt(StuckPattern::PermissionStorm).isEmpty());
        QVERIFY(!SessionObserver::correctivePrompt(StuckPattern::SubagentChurn).isEmpty());
    }
};

QTEST_GUILESS_MAIN(SessionObserverTest)

#include "SessionObserverTest.moc"
