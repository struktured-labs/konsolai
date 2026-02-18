/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "PromptQualityGateTest.h"

// Qt
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/PromptQualityGate.h"
#include "../claude/PromptTemplateManager.h"

using namespace Konsolai;

void PromptQualityGateTest::testVaguePrompt()
{
    const auto result = PromptQualityGate::assess(QStringLiteral("make it better"));
    QVERIFY(result.score < 25);
    QCOMPARE(result.grade, Assessment::TooVague);
}

void PromptQualityGateTest::testSpecificPrompt()
{
    const auto result = PromptQualityGate::assess(
        QStringLiteral("Fix the null pointer crash in src/claude/ClaudeSession.cpp when sendPrompt is called with empty string. Verify by running ctest."));
    QVERIFY2(result.score >= 50, qPrintable(QStringLiteral("Expected score >= 50, got %1").arg(result.score)));
    QVERIFY(result.grade == Assessment::Excellent || result.grade == Assessment::Good);
}

void PromptQualityGateTest::testMediumPrompt()
{
    const auto result = PromptQualityGate::assess(QStringLiteral("Add a new button to the settings page. Ensure it compiles."));
    QVERIFY2(result.score >= 25, qPrintable(QStringLiteral("Expected score >= 25, got %1").arg(result.score)));
    QVERIFY(result.score < 75);
    QVERIFY(result.grade == Assessment::Good || result.grade == Assessment::NeedsWork);
}

void PromptQualityGateTest::testFileDetection()
{
    const auto result =
        PromptQualityGate::assess(QStringLiteral("Refactor src/claude/ClaudeSession.cpp and src/claude/ClaudeProcess.h to share common logic."));
    QVERIFY(!result.detectedFiles.isEmpty());

    bool foundCpp = false;
    bool foundH = false;
    for (const auto &f : result.detectedFiles) {
        if (f.contains(QStringLiteral("ClaudeSession.cpp"))) {
            foundCpp = true;
        }
        if (f.contains(QStringLiteral("ClaudeProcess.h"))) {
            foundH = true;
        }
    }
    QVERIFY2(foundCpp, "Expected to detect ClaudeSession.cpp");
    QVERIFY2(foundH, "Expected to detect ClaudeProcess.h");
}

void PromptQualityGateTest::testAcceptanceCriteria()
{
    const int score = PromptQualityGate::scoreAcceptanceCriteria(QStringLiteral("Build the project, run the test suite, and verify all checks pass."));
    QVERIFY2(score >= 18, qPrintable(QStringLiteral("Expected acceptance score >= 18, got %1").arg(score)));
}

void PromptQualityGateTest::testBoundedScope()
{
    const int score =
        PromptQualityGate::scoreBoundedScope(QStringLiteral("Only modify the specific file src/claude/ClaudeSession.cpp, limited to the sendPrompt method."));
    QVERIFY2(score >= 14, qPrintable(QStringLiteral("Expected scope score >= 14, got %1").arg(score)));
}

void PromptQualityGateTest::testSuggestions()
{
    const auto result = PromptQualityGate::assess(QStringLiteral("do stuff"));
    QVERIFY(!result.suggestions.isEmpty());
}

void PromptQualityGateTest::testTemplateInstantiation()
{
    PromptTemplate tmpl;
    tmpl.id = QStringLiteral("test");
    tmpl.name = QStringLiteral("Test");
    tmpl.templateText = QStringLiteral("Fix {{bug}} in {{file}}.");
    tmpl.requiredFields = {QStringLiteral("bug"), QStringLiteral("file")};

    QMap<QString, QString> fields;
    fields[QStringLiteral("bug")] = QStringLiteral("crash");
    fields[QStringLiteral("file")] = QStringLiteral("main.cpp");

    const QString result = PromptTemplateManager::instantiate(tmpl, fields);
    QCOMPARE(result, QStringLiteral("Fix crash in main.cpp."));
}

void PromptQualityGateTest::testBuiltinTemplates()
{
    const auto templates = PromptTemplateManager::builtinTemplates();
    QCOMPARE(templates.size(), 5);

    // Verify each has a non-empty id and fields
    QStringList ids;
    for (const auto &t : templates) {
        QVERIFY2(!t.id.isEmpty(), "Template must have an id");
        QVERIFY2(!t.name.isEmpty(), "Template must have a name");
        QVERIFY2(!t.templateText.isEmpty(), "Template must have text");
        QVERIFY2(!t.requiredFields.isEmpty(), "Template must have required fields");
        ids.append(t.id);
    }

    QVERIFY(ids.contains(QStringLiteral("bugfix")));
    QVERIFY(ids.contains(QStringLiteral("feature")));
    QVERIFY(ids.contains(QStringLiteral("refactor")));
    QVERIFY(ids.contains(QStringLiteral("tests")));
    QVERIFY(ids.contains(QStringLiteral("gsd")));
}

void PromptQualityGateTest::testTemplateSerialization()
{
    PromptTemplate original;
    original.id = QStringLiteral("roundtrip");
    original.name = QStringLiteral("Round Trip Test");
    original.templateText = QStringLiteral("Do {{thing}} in {{place}}.");
    original.requiredFields = {QStringLiteral("thing"), QStringLiteral("place")};
    original.suggestedYoloLevel = 2;
    original.estimatedCostRange[0] = 0.5;
    original.estimatedCostRange[1] = 2.0;

    const QJsonObject json = PromptTemplateManager::toJson(original);
    const PromptTemplate restored = PromptTemplateManager::fromJson(json);

    QCOMPARE(restored.id, original.id);
    QCOMPARE(restored.name, original.name);
    QCOMPARE(restored.templateText, original.templateText);
    QCOMPARE(restored.requiredFields, original.requiredFields);
    QCOMPARE(restored.suggestedYoloLevel, original.suggestedYoloLevel);
    QCOMPARE(restored.estimatedCostRange[0], original.estimatedCostRange[0]);
    QCOMPARE(restored.estimatedCostRange[1], original.estimatedCostRange[1]);
}

void PromptQualityGateTest::testYoloLevelEstimation()
{
    // Excellent → level 3
    Assessment excellent;
    excellent.grade = Assessment::Excellent;
    QCOMPARE(PromptQualityGate::estimateYoloLevel(excellent), 3);

    // Good → level 2
    Assessment good;
    good.grade = Assessment::Good;
    QCOMPARE(PromptQualityGate::estimateYoloLevel(good), 2);

    // NeedsWork → level 1
    Assessment needsWork;
    needsWork.grade = Assessment::NeedsWork;
    QCOMPARE(PromptQualityGate::estimateYoloLevel(needsWork), 1);

    // TooVague → level 1
    Assessment tooVague;
    tooVague.grade = Assessment::TooVague;
    QCOMPARE(PromptQualityGate::estimateYoloLevel(tooVague), 1);
}

void PromptQualityGateTest::testEmptyPrompt()
{
    const auto result = PromptQualityGate::assess(QString());
    QCOMPARE(result.score, 0);
    QCOMPARE(result.grade, Assessment::TooVague);
    QVERIFY(result.detectedFiles.isEmpty());
}

QTEST_GUILESS_MAIN(Konsolai::PromptQualityGateTest)

#include "PromptQualityGateTest.moc"
