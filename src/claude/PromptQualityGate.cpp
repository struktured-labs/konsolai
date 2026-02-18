/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "PromptQualityGate.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace Konsolai
{

Assessment PromptQualityGate::assess(const QString &prompt, const QString &workingDir)
{
    Assessment a;

    if (prompt.trimmed().isEmpty()) {
        a.score = 0;
        a.grade = Assessment::TooVague;
        a.suggestions = generateSuggestions(0, 0, 0, 0, 0);
        a.suggestedYoloLevel = 1;
        a.estimatedDurationMinutes = 60;
        a.estimatedCostUSD = 1.20;
        return a;
    }

    int fileScore = scoreFilePaths(prompt, a.detectedFiles);
    int acceptScore = scoreAcceptanceCriteria(prompt);
    int scopeScore = scoreBoundedScope(prompt);
    int clarityScore = scoreClarity(prompt);
    int structureScore = scoreStructure(prompt);

    a.score = fileScore + acceptScore + scopeScore + clarityScore + structureScore;

    // Bonus for CLAUDE.md presence in working directory
    if (!workingDir.isEmpty()) {
        const QString claudeMdPath = workingDir + QStringLiteral("/CLAUDE.md");
        if (QFileInfo::exists(claudeMdPath)) {
            a.score = qMin(100, a.score + 5);
        }
    }

    a.grade = gradeFromScore(a.score);
    a.suggestions = generateSuggestions(fileScore, acceptScore, scopeScore, clarityScore, structureScore);
    a.suggestedYoloLevel = estimateYoloLevel(a);
    a.estimatedDurationMinutes = estimateDuration(a);
    a.estimatedCostUSD = estimateCost(a);

    return a;
}

int PromptQualityGate::scoreFilePaths(const QString &prompt, QStringList &detectedFiles)
{
    int score = 0;
    detectedFiles.clear();

    // Match file paths: src/..., *.cpp, *.h, *.py, etc.
    static const QRegularExpression filePathRx(QStringLiteral(
        R"((?:\b(?:src|lib|test|tests|bin|include)/[\w/.+-]+)|(?:[\w/.-]+\.(?:cpp|h|hpp|py|ts|js|json|yaml|yml|toml|cmake|txt|md|rs|go|java|xml|qml))\b)"));
    auto it = filePathRx.globalMatch(prompt);
    while (it.hasNext()) {
        auto match = it.next();
        const QString path = match.captured(0);
        if (!detectedFiles.contains(path)) {
            detectedFiles.append(path);
        }
    }

    // Match CamelCase tokens (class names like FooBar, MyWidget)
    static const QRegularExpression camelCaseRx(QStringLiteral(R"(\b[A-Z][a-z]+(?:[A-Z][a-z]+)+\b)"));
    auto ccIt = camelCaseRx.globalMatch(prompt);
    while (ccIt.hasNext()) {
        auto match = ccIt.next();
        const QString token = match.captured(0);
        if (!detectedFiles.contains(token)) {
            detectedFiles.append(token);
        }
    }

    if (!detectedFiles.isEmpty()) {
        // Scale: 1 reference = 10, 2 = 18, 3+ = 25
        if (detectedFiles.size() >= 3) {
            score = 25;
        } else if (detectedFiles.size() == 2) {
            score = 18;
        } else {
            score = 10;
        }
    }

    return score;
}

int PromptQualityGate::scoreAcceptanceCriteria(const QString &prompt)
{
    static const QStringList keywords = {
        QStringLiteral("build"),
        QStringLiteral("test"),
        QStringLiteral("pass"),
        QStringLiteral("verify"),
        QStringLiteral("assert"),
        QStringLiteral("compile"),
        QStringLiteral("check"),
        QStringLiteral("ensure"),
    };

    const QString lower = prompt.toLower();
    int matches = 0;
    for (const auto &kw : keywords) {
        if (lower.contains(kw)) {
            ++matches;
        }
    }

    if (matches >= 3) {
        return 25;
    } else if (matches == 2) {
        return 18;
    } else if (matches == 1) {
        return 10;
    }
    return 0;
}

int PromptQualityGate::scoreBoundedScope(const QString &prompt)
{
    static const QStringList scopeKeywords = {
        QStringLiteral("only"),
        QStringLiteral("just"),
        QStringLiteral("limited to"),
        QStringLiteral("single"),
        QStringLiteral("specific"),
    };

    const QString lower = prompt.toLower();
    int matches = 0;
    for (const auto &kw : scopeKeywords) {
        if (lower.contains(kw)) {
            ++matches;
        }
    }

    // Also check for explicit file lists (comma-separated paths or bullet points)
    static const QRegularExpression fileListRx(QStringLiteral(R"([\w/.-]+\.(?:cpp|h|py),\s*[\w/.-]+\.(?:cpp|h|py))"));
    if (fileListRx.match(prompt).hasMatch()) {
        matches += 2;
    }

    if (matches >= 3) {
        return 20;
    } else if (matches == 2) {
        return 14;
    } else if (matches == 1) {
        return 8;
    }
    return 0;
}

int PromptQualityGate::scoreClarity(const QString &prompt)
{
    static const QStringList actionVerbs = {
        QStringLiteral("fix"),
        QStringLiteral("add"),
        QStringLiteral("implement"),
        QStringLiteral("refactor"),
        QStringLiteral("create"),
        QStringLiteral("update"),
        QStringLiteral("remove"),
        QStringLiteral("move"),
    };

    static const QStringList vagueTerms = {
        QStringLiteral("improve"),
        QStringLiteral("make better"),
        QStringLiteral("clean up"),
        QStringLiteral("somehow"),
        QStringLiteral("maybe"),
    };

    const QString lower = prompt.toLower();
    int score = 0;

    // Action verbs: +5 each, max 10
    int verbCount = 0;
    for (const auto &v : actionVerbs) {
        if (lower.contains(v)) {
            ++verbCount;
        }
    }
    score += qMin(verbCount * 5, 10);

    // Vague terms: -3 each
    for (const auto &vt : vagueTerms) {
        if (lower.contains(vt)) {
            score -= 3;
        }
    }

    // Adequate length bonus (>20 chars)
    if (prompt.length() > 20) {
        score += 5;
    }

    return qBound(0, score, 15);
}

int PromptQualityGate::scoreStructure(const QString &prompt)
{
    const int len = prompt.trimmed().length();
    int score = 0;

    // Sweet spot: 50-2000 chars
    if (len >= 50 && len <= 2000) {
        score += 10;
    } else if (len >= 30 && len < 50) {
        score += 5;
    } else if (len > 2000) {
        score += 7; // slight penalty for very long
    }
    // < 30 chars = 0 base score (penalty)

    // Sentence structure: contains period, comma, or colon
    if (prompt.contains(QLatin1Char('.')) || prompt.contains(QLatin1Char(':')) || prompt.contains(QLatin1Char(','))) {
        score += 5;
    }

    return qBound(0, score, 15);
}

Assessment::Grade PromptQualityGate::gradeFromScore(int score)
{
    if (score >= 75) {
        return Assessment::Excellent;
    } else if (score >= 50) {
        return Assessment::Good;
    } else if (score >= 25) {
        return Assessment::NeedsWork;
    }
    return Assessment::TooVague;
}

QStringList PromptQualityGate::generateSuggestions(int fileScore, int acceptScore, int scopeScore, int clarityScore, int structureScore)
{
    QStringList suggestions;

    if (fileScore < 10) {
        suggestions.append(QStringLiteral("Mention specific files or classes to target"));
    }
    if (acceptScore < 10) {
        suggestions.append(QStringLiteral("Add acceptance criteria (e.g. 'verify by running ctest')"));
    }
    if (scopeScore < 8) {
        suggestions.append(QStringLiteral("Bound the scope (e.g. 'only modify src/claude/')"));
    }
    if (clarityScore < 5) {
        suggestions.append(QStringLiteral("Use clear action verbs (fix, add, implement, refactor)"));
    }
    if (structureScore < 5) {
        suggestions.append(QStringLiteral("Add more detail — aim for 50-2000 characters with sentence structure"));
    }

    return suggestions;
}

int PromptQualityGate::estimateYoloLevel(const Assessment &a)
{
    switch (a.grade) {
    case Assessment::Excellent:
        return 3;
    case Assessment::Good:
        return 2;
    case Assessment::NeedsWork:
    case Assessment::TooVague:
    default:
        return 1;
    }
}

int PromptQualityGate::estimateDuration(const Assessment &a)
{
    switch (a.grade) {
    case Assessment::Excellent:
        return 10;
    case Assessment::Good:
        return 15;
    case Assessment::NeedsWork:
        return 30;
    case Assessment::TooVague:
    default:
        return 60;
    }
}

double PromptQualityGate::estimateCost(const Assessment &a)
{
    // Proportional to duration, $0.02 per minute
    return estimateDuration(a) * 0.02;
}

} // namespace Konsolai
