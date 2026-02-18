/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "OneShotControllerTest.h"

// Qt
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/OneShotController.h"

using namespace Konsolai;

void OneShotControllerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void OneShotControllerTest::cleanupTestCase()
{
}

// ============================================================
// testDefaultConfig
// ============================================================

void OneShotControllerTest::testDefaultConfig()
{
    OneShotConfig config;

    QVERIFY(config.prompt.isEmpty());
    QVERIFY(config.workingDir.isEmpty());
    QVERIFY(config.model.isEmpty());
    QCOMPARE(config.timeLimitMinutes, 0);
    QCOMPARE(config.costCeilingUSD, 0.0);
    QCOMPARE(config.tokenCeiling, quint64(0));
    QCOMPARE(config.yoloLevel, 3);
    QVERIFY(!config.useGsd);
    QCOMPARE(config.qualityScore, 0);
}

// ============================================================
// testDefaultResult
// ============================================================

void OneShotControllerTest::testDefaultResult()
{
    OneShotResult result;

    QVERIFY(!result.success);
    QVERIFY(result.summary.isEmpty());
    QCOMPARE(result.costUSD, 0.0);
    QCOMPARE(result.durationSeconds, 0);
    QCOMPARE(result.totalTokens, quint64(0));
    QCOMPARE(result.filesModified, 0);
    QCOMPARE(result.commits, 0);
    QVERIFY(result.errors.isEmpty());
}

// ============================================================
// testSetConfig
// ============================================================

void OneShotControllerTest::testSetConfig()
{
    OneShotController ctrl;

    OneShotConfig config;
    config.prompt = QStringLiteral("Fix the login bug");
    config.workingDir = QStringLiteral("/home/user/project");
    config.model = QStringLiteral("opus");
    config.timeLimitMinutes = 15;
    config.costCeilingUSD = 0.50;
    config.tokenCeiling = 100000;
    config.yoloLevel = 2;
    config.useGsd = true;
    config.qualityScore = 85;

    ctrl.setConfig(config);

    const auto &c = ctrl.config();
    QCOMPARE(c.prompt, QStringLiteral("Fix the login bug"));
    QCOMPARE(c.workingDir, QStringLiteral("/home/user/project"));
    QCOMPARE(c.model, QStringLiteral("opus"));
    QCOMPARE(c.timeLimitMinutes, 15);
    QCOMPARE(c.costCeilingUSD, 0.50);
    QCOMPARE(c.tokenCeiling, quint64(100000));
    QCOMPARE(c.yoloLevel, 2);
    QVERIFY(c.useGsd);
    QCOMPARE(c.qualityScore, 85);
}

// ============================================================
// testIsRunning
// ============================================================

void OneShotControllerTest::testIsRunning()
{
    OneShotController ctrl;

    // Initially not running
    QVERIFY(!ctrl.isRunning());

    // After start(), should be running
    ctrl.start();
    QVERIFY(ctrl.isRunning());
}

// ============================================================
// testFormatBudgetStatus
// ============================================================

void OneShotControllerTest::testFormatBudgetStatus()
{
    OneShotController ctrl;

    // Without a session, should return empty
    QString status = ctrl.formatBudgetStatus();
    QVERIFY(status.isEmpty());
}

// ============================================================
// testFormatStateLabel
// ============================================================

void OneShotControllerTest::testFormatStateLabel()
{
    OneShotController ctrl;

    // Without a session, should return "No session"
    QString label = ctrl.formatStateLabel();
    QCOMPARE(label, QStringLiteral("No session"));
}

QTEST_GUILESS_MAIN(Konsolai::OneShotControllerTest)

#include "OneShotControllerTest.moc"
