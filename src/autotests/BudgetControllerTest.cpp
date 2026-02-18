/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "BudgetControllerTest.h"

// Qt
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/BudgetController.h"
#include "../claude/ClaudeSession.h"

using namespace Konsolai;

void BudgetControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void BudgetControllerTest::cleanupTestCase()
{
}

// ============================================================
// testBudgetDefaults
// ============================================================

void BudgetControllerTest::testBudgetDefaults()
{
    BudgetController ctrl;
    const auto &b = ctrl.budget();

    QCOMPARE(b.timeLimitMinutes, 0);
    QCOMPARE(b.costCeilingUSD, 0.0);
    QCOMPARE(b.tokenCeiling, quint64(0));
    QCOMPARE(b.timePolicy, SessionBudget::Soft);
    QCOMPARE(b.costPolicy, SessionBudget::Soft);
    QCOMPARE(b.tokenPolicy, SessionBudget::Soft);
    QCOMPARE(b.warningThresholdPercent, 80.0);
    QVERIFY(!b.timeExceeded);
    QVERIFY(!b.costExceeded);
    QVERIFY(!b.tokenExceeded);
    QVERIFY(!b.hasAnyLimit());
    QVERIFY(!ctrl.shouldBlockYolo());
}

// ============================================================
// testTimeBudgetWarning
// ============================================================

void BudgetControllerTest::testTimeBudgetWarning()
{
    BudgetController ctrl;
    QSignalSpy warningSpy(&ctrl, &BudgetController::budgetWarning);
    QSignalSpy exceededSpy(&ctrl, &BudgetController::budgetExceeded);

    SessionBudget b;
    b.timeLimitMinutes = 5;
    // Set startedAt to 4 minutes ago (80% = at warning threshold)
    b.startedAt = QDateTime::currentDateTime().addSecs(-4 * 60);
    ctrl.setBudget(b);

    ctrl.checkTimeBudget();

    QCOMPARE(warningSpy.count(), 1);
    QCOMPARE(warningSpy.at(0).at(0).toString(), QStringLiteral("time"));
    QVERIFY(warningSpy.at(0).at(1).toDouble() >= 80.0);

    // Not exceeded yet
    QCOMPARE(exceededSpy.count(), 0);
    QVERIFY(!ctrl.budget().timeExceeded);
}

// ============================================================
// testCostBudgetExceeded
// ============================================================

void BudgetControllerTest::testCostBudgetExceeded()
{
    BudgetController ctrl;
    QSignalSpy exceededSpy(&ctrl, &BudgetController::budgetExceeded);

    SessionBudget b;
    b.costCeilingUSD = 1.0;
    ctrl.setBudget(b);

    // Create token usage that costs more than $1.00
    // estimatedCostUSD = (input*3 + output*15 + cacheCreate*0.30 + cacheRead*0.30) / 1e6
    // To get $1.10: use outputTokens = 73334 → 73334*15/1e6 = $1.10
    TokenUsage usage;
    usage.inputTokens = 0;
    usage.outputTokens = 73334;
    usage.cacheReadTokens = 0;
    usage.cacheCreationTokens = 0;

    ctrl.onTokenUsageChanged(usage);

    QCOMPARE(exceededSpy.count(), 1);
    QCOMPARE(exceededSpy.at(0).at(0).toString(), QStringLiteral("cost"));
    QVERIFY(ctrl.budget().costExceeded);
    QVERIFY(ctrl.shouldBlockYolo());
}

// ============================================================
// testTokenBudgetExceeded
// ============================================================

void BudgetControllerTest::testTokenBudgetExceeded()
{
    BudgetController ctrl;
    QSignalSpy exceededSpy(&ctrl, &BudgetController::budgetExceeded);

    SessionBudget b;
    b.tokenCeiling = 10000;
    ctrl.setBudget(b);

    TokenUsage usage;
    usage.inputTokens = 6000;
    usage.outputTokens = 5000;
    usage.cacheReadTokens = 0;
    usage.cacheCreationTokens = 0;
    // totalTokens = 11000, exceeds 10000

    ctrl.onTokenUsageChanged(usage);

    QCOMPARE(exceededSpy.count(), 1);
    QCOMPARE(exceededSpy.at(0).at(0).toString(), QStringLiteral("token"));
    QVERIFY(ctrl.budget().tokenExceeded);
    QVERIFY(ctrl.shouldBlockYolo());
}

// ============================================================
// testResourceGateDebounce
// ============================================================

void BudgetControllerTest::testResourceGateDebounce()
{
    BudgetController ctrl;
    QSignalSpy gateSpy(&ctrl, &BudgetController::resourceGateTriggered);

    // Default debounce count is 6
    ResourceUsage high;
    high.cpuPercent = 96.0;
    high.rssBytes = 1024; // well below any threshold

    // First 5 ticks — should NOT trigger
    for (int i = 0; i < 5; ++i) {
        ctrl.onResourceUsageChanged(high);
    }
    QCOMPARE(gateSpy.count(), 0);
    QVERIFY(!ctrl.resourceGate().gateTriggered);

    // 6th tick — should trigger
    ctrl.onResourceUsageChanged(high);
    QCOMPARE(gateSpy.count(), 1);
    QVERIFY(ctrl.resourceGate().gateTriggered);
    QVERIFY(ctrl.shouldBlockYolo());
}

// ============================================================
// testTokenVelocity
// ============================================================

void BudgetControllerTest::testTokenVelocity()
{
    TokenVelocity v;

    // Initially zero
    QCOMPARE(v.tokensPerMinute(), 0.0);
    QCOMPARE(v.costPerMinute(), 0.0);

    // Add samples simulating 1000 tokens/minute
    QDateTime now = QDateTime::currentDateTime();

    // We need to directly set timestamps for reproducible tests
    TokenVelocity::VelocitySample s1;
    s1.timestamp = now.addSecs(-300); // 5 minutes ago
    s1.totalTokens = 0;
    s1.costUSD = 0.0;

    TokenVelocity::VelocitySample s2;
    s2.timestamp = now.addSecs(-240); // 4 minutes ago
    s2.totalTokens = 1000;
    s2.costUSD = 0.05;

    TokenVelocity::VelocitySample s3;
    s3.timestamp = now.addSecs(-180); // 3 minutes ago
    s3.totalTokens = 2000;
    s3.costUSD = 0.10;

    TokenVelocity::VelocitySample s4;
    s4.timestamp = now; // now
    s4.totalTokens = 5000;
    s4.costUSD = 0.25;

    // Manually fill ring buffer
    v.samples[0] = s1;
    v.samples[1] = s2;
    v.samples[2] = s3;
    v.samples[3] = s4;
    v.head = 4;
    v.count = 4;

    // tokensPerMinute: over 4 samples, lookback = min(4,5) = 4
    // oldest = (4-4+120)%120 = 0, newest = (4-1+120)%120 = 3
    // delta = 5000-0 = 5000 tokens over 300s = 5.0 min → 1000 tokens/min
    QVERIFY(qAbs(v.tokensPerMinute() - 1000.0) < 1.0);
    QVERIFY(qAbs(v.costPerMinute() - 0.05) < 0.001);

    // estimatedMinutesRemaining: 10000 ceiling, 5000 current, 1000/min → 5 minutes
    QVERIFY(qAbs(v.estimatedMinutesRemaining(10000, 5000) - 5.0) < 0.1);

    // Format check
    QString fmt = v.formatVelocity();
    QVERIFY(fmt.contains(QStringLiteral("K/m")));
    QVERIFY(fmt.contains(QStringLiteral("$")));
}

// ============================================================
// testBudgetSerialization
// ============================================================

void BudgetControllerTest::testBudgetSerialization()
{
    SessionBudget original;
    original.timeLimitMinutes = 30;
    original.costCeilingUSD = 5.50;
    original.tokenCeiling = 100000;
    original.timePolicy = SessionBudget::Hard;
    original.costPolicy = SessionBudget::Soft;
    original.tokenPolicy = SessionBudget::Hard;
    original.warningThresholdPercent = 75.0;
    original.timeExceeded = true;
    original.costExceeded = false;
    original.tokenExceeded = true;
    original.startedAt = QDateTime::currentDateTime();

    QJsonObject json = original.toJson();
    SessionBudget restored = SessionBudget::fromJson(json);

    QCOMPARE(restored.timeLimitMinutes, original.timeLimitMinutes);
    QCOMPARE(restored.costCeilingUSD, original.costCeilingUSD);
    QCOMPARE(restored.tokenCeiling, original.tokenCeiling);
    QCOMPARE(static_cast<int>(restored.timePolicy), static_cast<int>(original.timePolicy));
    QCOMPARE(static_cast<int>(restored.costPolicy), static_cast<int>(original.costPolicy));
    QCOMPARE(static_cast<int>(restored.tokenPolicy), static_cast<int>(original.tokenPolicy));
    QCOMPARE(restored.warningThresholdPercent, original.warningThresholdPercent);
    QCOMPARE(restored.timeExceeded, original.timeExceeded);
    QCOMPARE(restored.costExceeded, original.costExceeded);
    QCOMPARE(restored.tokenExceeded, original.tokenExceeded);
    // QDateTime ISO round-trip may lose sub-second precision, compare to second
    QVERIFY(qAbs(restored.startedAt.secsTo(original.startedAt)) <= 1);
}

// ============================================================
// testShouldBlockYolo
// ============================================================

void BudgetControllerTest::testShouldBlockYolo()
{
    BudgetController ctrl;
    QVERIFY(!ctrl.shouldBlockYolo());

    // Time exceeded
    {
        SessionBudget b;
        b.timeExceeded = true;
        ctrl.setBudget(b);
        QVERIFY(ctrl.shouldBlockYolo());
    }

    // Cost exceeded
    {
        SessionBudget b;
        b.costExceeded = true;
        ctrl.setBudget(b);
        QVERIFY(ctrl.shouldBlockYolo());
    }

    // Token exceeded
    {
        SessionBudget b;
        b.tokenExceeded = true;
        ctrl.setBudget(b);
        QVERIFY(ctrl.shouldBlockYolo());
    }

    // All clear
    {
        SessionBudget b;
        ctrl.setBudget(b);
        QVERIFY(!ctrl.shouldBlockYolo());
    }
}

QTEST_GUILESS_MAIN(Konsolai::BudgetControllerTest)

#include "BudgetControllerTest.moc"
