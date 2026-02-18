/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "PromptTemplateManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace Konsolai
{

QList<PromptTemplate> PromptTemplateManager::builtinTemplates()
{
    QList<PromptTemplate> list;

    // 1. Bug Fix
    {
        PromptTemplate t;
        t.id = QStringLiteral("bugfix");
        t.name = QStringLiteral("Bug Fix");
        t.templateText = QStringLiteral("Fix {{symptom}} in {{file_path}}. Root cause: {{root_cause}}. Verify by running {{test_command}}.");
        t.requiredFields = {QStringLiteral("symptom"), QStringLiteral("file_path"), QStringLiteral("root_cause"), QStringLiteral("test_command")};
        t.suggestedYoloLevel = 3;
        t.estimatedCostRange[0] = 0.10;
        t.estimatedCostRange[1] = 0.30;
        list.append(t);
    }

    // 2. Feature Add
    {
        PromptTemplate t;
        t.id = QStringLiteral("feature");
        t.name = QStringLiteral("Feature Add");
        t.templateText = QStringLiteral("Add {{feature}} to {{component}}. Requirements: {{requirements}}. Add tests covering: {{test_scenarios}}.");
        t.requiredFields = {QStringLiteral("feature"), QStringLiteral("component"), QStringLiteral("requirements"), QStringLiteral("test_scenarios")};
        t.suggestedYoloLevel = 2;
        t.estimatedCostRange[0] = 0.30;
        t.estimatedCostRange[1] = 1.50;
        list.append(t);
    }

    // 3. Refactor
    {
        PromptTemplate t;
        t.id = QStringLiteral("refactor");
        t.name = QStringLiteral("Refactor");
        t.templateText = QStringLiteral("Refactor {{target}} to use {{pattern}}. All existing tests must pass. Affected files: {{affected_files}}.");
        t.requiredFields = {QStringLiteral("target"), QStringLiteral("pattern"), QStringLiteral("affected_files")};
        t.suggestedYoloLevel = 1;
        t.estimatedCostRange[0] = 0.20;
        t.estimatedCostRange[1] = 0.80;
        list.append(t);
    }

    // 4. Test Suite
    {
        PromptTemplate t;
        t.id = QStringLiteral("tests");
        t.name = QStringLiteral("Test Suite");
        t.templateText = QStringLiteral("Write tests for {{component}}. Cover: {{scenarios}}. Use the existing test framework.");
        t.requiredFields = {QStringLiteral("component"), QStringLiteral("scenarios")};
        t.suggestedYoloLevel = 3;
        t.estimatedCostRange[0] = 0.15;
        t.estimatedCostRange[1] = 0.50;
        list.append(t);
    }

    // 5. GSD Project
    {
        PromptTemplate t;
        t.id = QStringLiteral("gsd");
        t.name = QStringLiteral("GSD Project");
        t.templateText = QStringLiteral("Use /gsd:new-project: {{description}}");
        t.requiredFields = {QStringLiteral("description")};
        t.suggestedYoloLevel = 3;
        t.estimatedCostRange[0] = 1.0;
        t.estimatedCostRange[1] = 5.0;
        list.append(t);
    }

    return list;
}

QString PromptTemplateManager::userTemplateDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/konsolai/prompt-templates");
}

QList<PromptTemplate> PromptTemplateManager::userTemplates()
{
    QList<PromptTemplate> list;

    const QDir dir(userTemplateDir());
    if (!dir.exists()) {
        return list;
    }

    const QStringList jsonFiles = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &fileName : jsonFiles) {
        QFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray data = file.readAll();
        file.close();

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        list.append(fromJson(doc.object()));
    }

    return list;
}

QList<PromptTemplate> PromptTemplateManager::allTemplates()
{
    QList<PromptTemplate> list = builtinTemplates();
    list.append(userTemplates());
    return list;
}

QString PromptTemplateManager::instantiate(const PromptTemplate &tmpl, const QMap<QString, QString> &fields)
{
    QString result = tmpl.templateText;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const QString placeholder = QStringLiteral("{{") + it.key() + QStringLiteral("}}");
        result.replace(placeholder, it.value());
    }
    return result;
}

bool PromptTemplateManager::saveUserTemplate(const PromptTemplate &tmpl)
{
    const QString dirPath = userTemplateDir();
    QDir dir(dirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return false;
    }

    const QString filePath = dirPath + QLatin1Char('/') + tmpl.id + QStringLiteral(".json");
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    const QJsonDocument doc(toJson(tmpl));
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

PromptTemplate PromptTemplateManager::fromJson(const QJsonObject &obj)
{
    PromptTemplate t;
    t.id = obj[QStringLiteral("id")].toString();
    t.name = obj[QStringLiteral("name")].toString();
    t.templateText = obj[QStringLiteral("templateText")].toString();
    t.suggestedYoloLevel = obj[QStringLiteral("suggestedYoloLevel")].toInt(1);

    const QJsonArray fieldsArr = obj[QStringLiteral("requiredFields")].toArray();
    for (const auto &v : fieldsArr) {
        t.requiredFields.append(v.toString());
    }

    t.estimatedCostRange[0] = obj[QStringLiteral("estimatedCostMin")].toDouble(0.0);
    t.estimatedCostRange[1] = obj[QStringLiteral("estimatedCostMax")].toDouble(0.0);

    return t;
}

QJsonObject PromptTemplateManager::toJson(const PromptTemplate &tmpl)
{
    QJsonObject obj;
    obj[QStringLiteral("id")] = tmpl.id;
    obj[QStringLiteral("name")] = tmpl.name;
    obj[QStringLiteral("templateText")] = tmpl.templateText;
    obj[QStringLiteral("suggestedYoloLevel")] = tmpl.suggestedYoloLevel;

    QJsonArray fieldsArr;
    for (const auto &f : tmpl.requiredFields) {
        fieldsArr.append(f);
    }
    obj[QStringLiteral("requiredFields")] = fieldsArr;

    obj[QStringLiteral("estimatedCostMin")] = tmpl.estimatedCostRange[0];
    obj[QStringLiteral("estimatedCostMax")] = tmpl.estimatedCostRange[1];

    return obj;
}

} // namespace Konsolai
