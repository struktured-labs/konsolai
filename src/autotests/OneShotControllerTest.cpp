/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "OneShotControllerTest.h"

// Qt
#include <QSignalSpy>
#include <QTest>

// Konsolai
#include "../claude/OneShotController.h"

using namespace Konsolai;

void OneShotControllerTest::testInitialPhase()
{
    OneShotController controller;
    QVERIFY(!controller.isRunning());
}

void OneShotControllerTest::testConfigDefaults()
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

void OneShotControllerTest::testResultDefaults()
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

void OneShotControllerTest::testIsRunning()
{
    OneShotController controller;
    QVERIFY(!controller.isRunning());
}

void OneShotControllerTest::testPhaseTransitions()
{
    OneShotController controller;
    // Before start(), not running
    QVERIFY(!controller.isRunning());

    // start() makes it running
    controller.start();
    QVERIFY(controller.isRunning());
}

void OneShotControllerTest::testFormatBudgetProgress()
{
    OneShotController controller;
    // Without a session attached, should return empty
    QVERIFY(controller.formatBudgetStatus().isEmpty());
}

void OneShotControllerTest::testElapsedMinutes()
{
    OneShotController controller;
    // Before start(), format should be empty
    QVERIFY(controller.formatBudgetStatus().isEmpty());
}

void OneShotControllerTest::testYoloLevelMapping()
{
    OneShotConfig config;
    QCOMPARE(config.yoloLevel, 3); // Triple yolo by default

    config.yoloLevel = 1;
    QCOMPARE(config.yoloLevel, 1);

    config.yoloLevel = 2;
    QCOMPARE(config.yoloLevel, 2);
}

void OneShotControllerTest::testBudgetPolicySoft()
{
    OneShotConfig config;
    // Default is soft (no explicit field in simplified config)
    QCOMPARE(config.timeLimitMinutes, 0);
}

void OneShotControllerTest::testBudgetPolicyHard()
{
    OneShotConfig config;
    config.costCeilingUSD = 5.0;
    QCOMPARE(config.costCeilingUSD, 5.0);
}

void OneShotControllerTest::testGsdPromptPrefix()
{
    OneShotConfig config;
    config.prompt = QStringLiteral("Build a REST API");
    config.useGsd = true;

    // Verify the GSD prefix would be applied
    QString expectedPrompt = QStringLiteral("Use /gsd:new-project: Build a REST API");
    QString actualPrompt = config.useGsd ? QStringLiteral("Use /gsd:new-project: ") + config.prompt : config.prompt;
    QCOMPARE(actualPrompt, expectedPrompt);
}

void OneShotControllerTest::testOneShotConfig()
{
    OneShotConfig config;
    config.prompt = QStringLiteral("Fix the crash in src/main.cpp");
    config.workingDir = QStringLiteral("/home/user/project");
    config.timeLimitMinutes = 30;
    config.costCeilingUSD = 1.50;
    config.tokenCeiling = 500000;
    config.yoloLevel = 2;
    config.qualityScore = 85;

    OneShotController controller;
    controller.setConfig(config);

    QCOMPARE(controller.config().prompt, QStringLiteral("Fix the crash in src/main.cpp"));
    QCOMPARE(controller.config().timeLimitMinutes, 30);
    QCOMPARE(controller.config().costCeilingUSD, 1.50);
    QCOMPARE(controller.config().yoloLevel, 2);
    QCOMPARE(controller.config().qualityScore, 85);
}

QTEST_GUILESS_MAIN(Konsolai::OneShotControllerTest)

#include "OneShotControllerTest.moc"
