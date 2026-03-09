/*
    SPDX-FileCopyrightText: 2025 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Own
#include "PromptTemplateManagerTest.h"

// Qt
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTest>

// Konsolai
#include "../claude/PromptTemplateManager.h"

using namespace Konsolai;

void PromptTemplateManagerTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
}

void PromptTemplateManagerTest::cleanupTestCase()
{
}

void PromptTemplateManagerTest::cleanup()
{
    // Remove user template directory between tests
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/konsolai/prompt-templates");
    QDir(dir).removeRecursively();
}

// ========== Builtin Templates ==========

void PromptTemplateManagerTest::testBuiltinTemplatesNonEmpty()
{
    const auto templates = PromptTemplateManager::builtinTemplates();
    QVERIFY(!templates.isEmpty());
    QVERIFY(templates.size() >= 5);
}

void PromptTemplateManagerTest::testBuiltinTemplateIds()
{
    const auto templates = PromptTemplateManager::builtinTemplates();
    QStringList ids;
    for (const auto &t : templates) {
        ids.append(t.id);
    }
    QVERIFY(ids.contains(QStringLiteral("bugfix")));
    QVERIFY(ids.contains(QStringLiteral("feature")));
    QVERIFY(ids.contains(QStringLiteral("refactor")));
    QVERIFY(ids.contains(QStringLiteral("tests")));
    QVERIFY(ids.contains(QStringLiteral("gsd")));
}

void PromptTemplateManagerTest::testBuiltinTemplateFields()
{
    const auto templates = PromptTemplateManager::builtinTemplates();

    // Find bugfix template and verify its required fields
    for (const auto &t : templates) {
        if (t.id == QStringLiteral("bugfix")) {
            QVERIFY(t.requiredFields.contains(QStringLiteral("symptom")));
            QVERIFY(t.requiredFields.contains(QStringLiteral("file_path")));
            QVERIFY(t.requiredFields.contains(QStringLiteral("root_cause")));
            QVERIFY(t.requiredFields.contains(QStringLiteral("test_command")));
            QCOMPARE(t.suggestedYoloLevel, 3);
            QVERIFY(t.estimatedCostRange[0] > 0.0);
            QVERIFY(t.estimatedCostRange[1] > t.estimatedCostRange[0]);
            return;
        }
    }
    QFAIL("Bugfix template not found in builtins");
}

// ========== User Templates ==========

void PromptTemplateManagerTest::testUserTemplatesInitiallyEmpty()
{
    const auto templates = PromptTemplateManager::userTemplates();
    QVERIFY(templates.isEmpty());
}

void PromptTemplateManagerTest::testSaveAndLoadUserTemplate()
{
    PromptTemplate tmpl;
    tmpl.id = QStringLiteral("custom-review");
    tmpl.name = QStringLiteral("Code Review");
    tmpl.templateText = QStringLiteral("Review {{file}} for {{criteria}}.");
    tmpl.requiredFields = {QStringLiteral("file"), QStringLiteral("criteria")};
    tmpl.suggestedYoloLevel = 1;
    tmpl.estimatedCostRange[0] = 0.05;
    tmpl.estimatedCostRange[1] = 0.15;

    QVERIFY(PromptTemplateManager::saveUserTemplate(tmpl));

    const auto loaded = PromptTemplateManager::userTemplates();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded[0].id, QStringLiteral("custom-review"));
    QCOMPARE(loaded[0].name, QStringLiteral("Code Review"));
    QCOMPARE(loaded[0].templateText, QStringLiteral("Review {{file}} for {{criteria}}."));
    QCOMPARE(loaded[0].requiredFields.size(), 2);
    QVERIFY(loaded[0].requiredFields.contains(QStringLiteral("file")));
    QVERIFY(loaded[0].requiredFields.contains(QStringLiteral("criteria")));
    QCOMPARE(loaded[0].suggestedYoloLevel, 1);
    QCOMPARE(loaded[0].estimatedCostRange[0], 0.05);
    QCOMPARE(loaded[0].estimatedCostRange[1], 0.15);
}

// ========== Instantiation ==========

void PromptTemplateManagerTest::testInstantiateBasic()
{
    PromptTemplate tmpl;
    tmpl.templateText = QStringLiteral("Hello {{name}}!");

    QMap<QString, QString> fields;
    fields[QStringLiteral("name")] = QStringLiteral("World");

    const QString result = PromptTemplateManager::instantiate(tmpl, fields);
    QCOMPARE(result, QStringLiteral("Hello World!"));
}

void PromptTemplateManagerTest::testInstantiateMultipleFields()
{
    PromptTemplate tmpl;
    tmpl.templateText = QStringLiteral("Fix {{symptom}} in {{file_path}}.");

    QMap<QString, QString> fields;
    fields[QStringLiteral("symptom")] = QStringLiteral("crash on startup");
    fields[QStringLiteral("file_path")] = QStringLiteral("main.cpp");

    const QString result = PromptTemplateManager::instantiate(tmpl, fields);
    QCOMPARE(result, QStringLiteral("Fix crash on startup in main.cpp."));
}

void PromptTemplateManagerTest::testInstantiateUnusedFieldsIgnored()
{
    PromptTemplate tmpl;
    tmpl.templateText = QStringLiteral("Hello {{name}}!");

    QMap<QString, QString> fields;
    fields[QStringLiteral("name")] = QStringLiteral("World");
    fields[QStringLiteral("extra")] = QStringLiteral("ignored");

    const QString result = PromptTemplateManager::instantiate(tmpl, fields);
    QCOMPARE(result, QStringLiteral("Hello World!"));
}

void PromptTemplateManagerTest::testInstantiateMissingFieldsLeftAsPlaceholder()
{
    PromptTemplate tmpl;
    tmpl.templateText = QStringLiteral("Fix {{symptom}} in {{file_path}}.");

    QMap<QString, QString> fields;
    fields[QStringLiteral("symptom")] = QStringLiteral("bug");
    // file_path not provided

    const QString result = PromptTemplateManager::instantiate(tmpl, fields);
    QCOMPARE(result, QStringLiteral("Fix bug in {{file_path}}."));
}

// ========== JSON Serialization ==========

void PromptTemplateManagerTest::testToJsonRoundTrip()
{
    PromptTemplate original;
    original.id = QStringLiteral("test-tmpl");
    original.name = QStringLiteral("Test Template");
    original.templateText = QStringLiteral("Do {{action}} on {{target}}.");
    original.requiredFields = {QStringLiteral("action"), QStringLiteral("target")};
    original.suggestedYoloLevel = 2;
    original.estimatedCostRange[0] = 0.10;
    original.estimatedCostRange[1] = 0.50;

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

void PromptTemplateManagerTest::testFromJsonMissingFields()
{
    // Minimal JSON — only id set
    QJsonObject obj;
    obj[QStringLiteral("id")] = QStringLiteral("minimal");

    const PromptTemplate t = PromptTemplateManager::fromJson(obj);
    QCOMPARE(t.id, QStringLiteral("minimal"));
    QCOMPARE(t.name, QString());
    QCOMPARE(t.templateText, QString());
    QVERIFY(t.requiredFields.isEmpty());
    QCOMPARE(t.suggestedYoloLevel, 1); // default
    QCOMPARE(t.estimatedCostRange[0], 0.0);
    QCOMPARE(t.estimatedCostRange[1], 0.0);
}

// ========== allTemplates ==========

void PromptTemplateManagerTest::testAllTemplatesIncludesBuiltinAndUser()
{
    // Save a user template first
    PromptTemplate userTmpl;
    userTmpl.id = QStringLiteral("user-custom");
    userTmpl.name = QStringLiteral("User Custom");
    userTmpl.templateText = QStringLiteral("Custom {{thing}}.");
    userTmpl.requiredFields = {QStringLiteral("thing")};
    QVERIFY(PromptTemplateManager::saveUserTemplate(userTmpl));

    const auto all = PromptTemplateManager::allTemplates();
    const auto builtins = PromptTemplateManager::builtinTemplates();

    // Should have all builtins plus our user template
    QCOMPARE(all.size(), builtins.size() + 1);

    // Verify user template is present
    bool foundUser = false;
    for (const auto &t : all) {
        if (t.id == QStringLiteral("user-custom")) {
            foundUser = true;
            break;
        }
    }
    QVERIFY(foundUser);
}

QTEST_GUILESS_MAIN(Konsolai::PromptTemplateManagerTest)

#include "PromptTemplateManagerTest.moc"
