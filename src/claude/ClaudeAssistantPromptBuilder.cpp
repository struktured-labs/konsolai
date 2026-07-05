/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeAssistantPromptBuilder.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

namespace Konsolai
{

namespace
{

QString stripMarkdownFences(const QString &input)
{
    QString text = input.trimmed();
    // Handle ```json ... ``` or ``` ... ``` fenced blocks.
    if (text.startsWith(QStringLiteral("```"))) {
        int firstNewline = text.indexOf(QLatin1Char('\n'));
        if (firstNewline > 0) {
            text = text.mid(firstNewline + 1);
        } else {
            text = text.mid(3);
        }
    }
    if (text.endsWith(QStringLiteral("```"))) {
        text.chop(3);
    }
    return text.trimmed();
}

// If the model prefaced or trailed the JSON with prose, try to isolate
// the first { ... } block. Cheap fallback — the fenced-block strip covers
// the common case.
QString extractFirstJsonObject(const QString &input)
{
    const int firstBrace = input.indexOf(QLatin1Char('{'));
    const int lastBrace = input.lastIndexOf(QLatin1Char('}'));
    if (firstBrace < 0 || lastBrace <= firstBrace) {
        return input;
    }
    return input.mid(firstBrace, lastBrace - firstBrace + 1);
}

QString basenameOf(const QString &workdir)
{
    const int slash = workdir.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? workdir.mid(slash + 1) : workdir;
}

} // namespace

QString buildReorganizePrompt(const TreeInventory &inventory, const QString &userIntent)
{
    QString out;
    out.reserve(2048);
    out += QStringLiteral(
        "The user is organizing their session tree in konsolai (a Claude terminal manager). "
        "Sessions are grouped under project directories, and project directories are grouped "
        "under categories inferred from a longest-common-prefix on project basenames, plus "
        "explicit user overrides.\n\n");

    // Projects (workdir -> session count).
    out += QStringLiteral("Current projects (workdir → session count):\n");
    if (inventory.projects.isEmpty()) {
        out += QStringLiteral("- (none)\n");
    }
    for (const TreeInventory::Project &p : inventory.projects) {
        QString base = p.basename.isEmpty() ? basenameOf(p.workingDirectory) : p.basename;
        out += QStringLiteral("- %1 (%2 session%3)")
                   .arg(p.workingDirectory, QString::number(p.sessionCount), p.sessionCount == 1 ? QString() : QStringLiteral("s"));
        if (!p.description.trimmed().isEmpty()) {
            out += QStringLiteral(" — %1").arg(p.description.trimmed());
        }
        out += QStringLiteral("  [basename=%1]\n").arg(base);
    }

    // Categories.
    out += QStringLiteral("\nCurrent categories (LCP-detected + user overrides):\n");
    if (inventory.categories.isEmpty()) {
        out += QStringLiteral("- (none)\n");
    }
    for (const TreeInventory::Category &c : inventory.categories) {
        out += QStringLiteral("- %1 → %2\n").arg(c.key, c.projectWorkdirs.join(QStringLiteral(", ")));
    }

    if (!inventory.userCategories.isEmpty()) {
        out += QStringLiteral("\nUser-created empty categories:\n");
        for (const QString &u : inventory.userCategories) {
            out += QStringLiteral("- %1\n").arg(u);
        }
    }

    // Existing overrides.
    out += QStringLiteral("\nExisting user overrides:\n");
    out += QStringLiteral("- CategoryAliases: ");
    if (inventory.existingAliases.isEmpty()) {
        out += QStringLiteral("(none)\n");
    } else {
        QStringList pairs;
        for (auto it = inventory.existingAliases.cbegin(); it != inventory.existingAliases.cend(); ++it) {
            pairs << QStringLiteral("%1 → %2").arg(it.key(), it.value());
        }
        out += pairs.join(QStringLiteral(", "));
        out += QLatin1Char('\n');
    }
    out += QStringLiteral("- WorkdirOverrides: ");
    if (inventory.existingWorkdirOverrides.isEmpty()) {
        out += QStringLiteral("(none)\n");
    } else {
        QStringList pairs;
        for (auto it = inventory.existingWorkdirOverrides.cbegin(); it != inventory.existingWorkdirOverrides.cend(); ++it) {
            pairs << QStringLiteral("%1 → %2").arg(it.key(), it.value());
        }
        out += pairs.join(QStringLiteral(", "));
        out += QLatin1Char('\n');
    }
    out += QStringLiteral("- SuppressedCategories: ");
    if (inventory.existingSuppressedCategories.isEmpty()) {
        out += QStringLiteral("(none)\n");
    } else {
        out += inventory.existingSuppressedCategories.join(QStringLiteral(", "));
        out += QLatin1Char('\n');
    }

    // User intent.
    out += QStringLiteral("\nThe user's request: ");
    out += userIntent.trimmed().isEmpty() ? QStringLiteral("(no explicit intent — propose sensible grouping)") : userIntent.trimmed();
    out += QLatin1Char('\n');

    // Schema.
    out += QStringLiteral(
        "\nPropose changes as JSON in EXACTLY this shape (no markdown, no prose outside the JSON):\n\n"
        "{\n"
        "  \"categoryAliases\": { \"sourceCategoryKey\": \"targetCategoryName\", ... },\n"
        "  \"workdirOverrides\": { \"/full/workdir/path\": \"categoryName\", ... },\n"
        "  \"suppressedCategories\": [ \"categoryKeyToUngroup\", ... ],\n"
        "  \"userCategories\": [ \"newlyCreatedEmptyCategoryName\", ... ],\n"
        "  \"rationale\": \"One short sentence per change explaining the intent.\"\n"
        "}\n\n"
        "Only include entries that are DIFFERENT from the existing state — the caller applies your JSON as a delta.\n\n"
        "If the user's request cannot be fulfilled by tree reorganization (e.g. \"delete all sessions\"),\n"
        "return {\"rationale\":\"cannot fulfill: <why>\"} with the four collection fields empty.\n");

    return out;
}

ReorganizeProposal parseReorganizeResponse(const QString &responseText, QString *errorOut)
{
    ReorganizeProposal proposal;

    QString cleaned = stripMarkdownFences(responseText);
    if (cleaned.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("empty response");
        }
        return proposal;
    }

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(cleaned.toUtf8(), &err);
    if (doc.isNull() || !doc.isObject()) {
        // Try isolating the first {...} block in case the model added chatter.
        const QString isolated = extractFirstJsonObject(cleaned);
        if (isolated != cleaned) {
            doc = QJsonDocument::fromJson(isolated.toUtf8(), &err);
        }
    }
    if (doc.isNull() || !doc.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("JSON parse error: %1").arg(err.errorString());
        }
        return proposal;
    }

    const QJsonObject obj = doc.object();

    // categoryAliases: object of strings.
    const QJsonValue aliasesVal = obj.value(QStringLiteral("categoryAliases"));
    if (aliasesVal.isObject()) {
        const QJsonObject aliases = aliasesVal.toObject();
        for (auto it = aliases.begin(); it != aliases.end(); ++it) {
            if (it.value().isString()) {
                const QString src = it.key().trimmed();
                const QString tgt = it.value().toString().trimmed();
                if (!src.isEmpty() && !tgt.isEmpty()) {
                    proposal.categoryAliases.insert(src, tgt);
                }
            }
        }
    }

    // workdirOverrides: object of strings.
    const QJsonValue overridesVal = obj.value(QStringLiteral("workdirOverrides"));
    if (overridesVal.isObject()) {
        const QJsonObject overrides = overridesVal.toObject();
        for (auto it = overrides.begin(); it != overrides.end(); ++it) {
            if (it.value().isString()) {
                const QString src = it.key().trimmed();
                const QString tgt = it.value().toString().trimmed();
                if (!src.isEmpty() && !tgt.isEmpty()) {
                    proposal.workdirOverrides.insert(src, tgt);
                }
            }
        }
    }

    // suppressedCategories: array of strings.
    const QJsonValue suppressedVal = obj.value(QStringLiteral("suppressedCategories"));
    if (suppressedVal.isArray()) {
        QSet<QString> seen;
        for (const QJsonValue &v : suppressedVal.toArray()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty() && !seen.contains(s)) {
                proposal.suppressedCategories.append(s);
                seen.insert(s);
            }
        }
    }

    // userCategories: array of strings.
    const QJsonValue userCatsVal = obj.value(QStringLiteral("userCategories"));
    if (userCatsVal.isArray()) {
        QSet<QString> seen;
        for (const QJsonValue &v : userCatsVal.toArray()) {
            const QString s = v.toString().trimmed();
            if (!s.isEmpty() && !seen.contains(s)) {
                proposal.userCategories.append(s);
                seen.insert(s);
            }
        }
    }

    // rationale: string (optional).
    const QJsonValue rationaleVal = obj.value(QStringLiteral("rationale"));
    if (rationaleVal.isString()) {
        proposal.rationale = rationaleVal.toString().trimmed();
    }

    return proposal;
}

QString buildSuggestNamePrompt(const QStringList &projectBasenames, const QStringList &projectDescriptions)
{
    QString out;
    out.reserve(512);
    out += QStringLiteral(
        "Suggest a single, short, memorable category name for the following related projects. "
        "The name must be:\n"
        "- 1 to 3 words\n"
        "- lower-case kebab-case (words separated by hyphens)\n"
        "- no punctuation, no quotes, no explanation\n\n"
        "Reply with ONLY the name, nothing else.\n\n"
        "Projects:\n");
    const int n = projectBasenames.size();
    for (int i = 0; i < n; ++i) {
        const QString base = projectBasenames.value(i);
        const QString desc = projectDescriptions.value(i);
        if (desc.trimmed().isEmpty()) {
            out += QStringLiteral("- %1\n").arg(base);
        } else {
            out += QStringLiteral("- %1 — %2\n").arg(base, desc.trimmed());
        }
    }
    return out;
}

QString parseSuggestNameResponse(const QString &responseText)
{
    QString s = responseText.trimmed();
    if (s.isEmpty()) {
        return QString();
    }

    // Strip Markdown code fences.
    s = stripMarkdownFences(s);

    // Take just the first line — models sometimes append rationale.
    int nl = s.indexOf(QLatin1Char('\n'));
    if (nl >= 0) {
        s = s.left(nl);
    }
    s = s.trimmed();

    // Strip surrounding quotes/backticks (single or multiple).
    while (!s.isEmpty() && (s.startsWith(QLatin1Char('"')) || s.startsWith(QLatin1Char('\'')) || s.startsWith(QLatin1Char('`')))) {
        s = s.mid(1);
    }
    while (!s.isEmpty() && (s.endsWith(QLatin1Char('"')) || s.endsWith(QLatin1Char('\'')) || s.endsWith(QLatin1Char('`')))) {
        s.chop(1);
    }

    // Strip trailing punctuation.
    while (!s.isEmpty()) {
        const QChar c = s.at(s.size() - 1);
        if (c == QLatin1Char('.') || c == QLatin1Char(',') || c == QLatin1Char(';') || c == QLatin1Char(':') || c == QLatin1Char('!')
            || c == QLatin1Char('?')) {
            s.chop(1);
        } else {
            break;
        }
    }
    s = s.trimmed();

    // Lower-case and kebab-case: collapse whitespace runs to a single hyphen,
    // and collapse repeated hyphens/underscores to a single hyphen.
    s = s.toLower();
    static const QRegularExpression whitespaceRe(QStringLiteral("\\s+"));
    s.replace(whitespaceRe, QStringLiteral("-"));
    s.replace(QLatin1Char('_'), QLatin1Char('-'));
    static const QRegularExpression multiHyphenRe(QStringLiteral("-+"));
    s.replace(multiHyphenRe, QStringLiteral("-"));

    // Drop any remaining chars that aren't [a-z0-9-].
    QString cleaned;
    cleaned.reserve(s.size());
    for (const QChar &ch : s) {
        if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || ch == QLatin1Char('-')) {
            cleaned.append(ch);
        }
    }

    // Strip leading/trailing hyphens.
    while (cleaned.startsWith(QLatin1Char('-'))) {
        cleaned = cleaned.mid(1);
    }
    while (cleaned.endsWith(QLatin1Char('-'))) {
        cleaned.chop(1);
    }
    return cleaned;
}

} // namespace Konsolai
