/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROMPTTEMPLATEMANAGER_H
#define PROMPTTEMPLATEMANAGER_H

#include "konsoleprivate_export.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Konsolai
{

/**
 * A reusable prompt template with placeholder fields.
 */
struct KONSOLEPRIVATE_EXPORT PromptTemplate {
    QString id;
    QString name;
    QString templateText;
    QStringList requiredFields;
    int suggestedYoloLevel = 1;
    double estimatedCostRange[2] = {0.0, 0.0};
};

/**
 * Manages built-in and user-defined prompt templates.
 *
 * User templates are stored as JSON files in
 * ~/.local/share/konsolai/prompt-templates/.
 */
class KONSOLEPRIVATE_EXPORT PromptTemplateManager
{
public:
    static QList<PromptTemplate> builtinTemplates();
    static QList<PromptTemplate> userTemplates();
    static QList<PromptTemplate> allTemplates();

    static QString instantiate(const PromptTemplate &tmpl, const QMap<QString, QString> &fields);
    static bool saveUserTemplate(const PromptTemplate &tmpl);

    static PromptTemplate fromJson(const QJsonObject &obj);
    static QJsonObject toJson(const PromptTemplate &tmpl);

private:
    PromptTemplateManager() = default;

    static QString userTemplateDir();
};

} // namespace Konsolai

#endif // PROMPTTEMPLATEMANAGER_H
