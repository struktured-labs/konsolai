/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PROMPTQUALITYGATE_H
#define PROMPTQUALITYGATE_H

#include "konsoleprivate_export.h"

#include <QString>
#include <QStringList>

namespace Konsolai
{

/**
 * Result of a prompt quality assessment.
 */
struct KONSOLEPRIVATE_EXPORT Assessment {
    int score = 0; // 0-100

    enum Grade {
        Excellent,
        Good,
        NeedsWork,
        TooVague
    };
    Grade grade = TooVague;

    QStringList suggestions;
    QStringList detectedFiles;
    int suggestedYoloLevel = 1;
    int estimatedDurationMinutes = 60;
    double estimatedCostUSD = 1.20;
};

/**
 * Heuristic prompt scoring — no LLM calls.
 *
 * Evaluates a prompt string for specificity, acceptance criteria,
 * bounded scope, clarity, and structure. Returns an Assessment with
 * a score (0-100), grade, improvement suggestions, and estimates.
 */
class KONSOLEPRIVATE_EXPORT PromptQualityGate
{
public:
    static Assessment assess(const QString &prompt, const QString &workingDir = QString());

    static int scoreFilePaths(const QString &prompt, QStringList &detectedFiles);
    static int scoreAcceptanceCriteria(const QString &prompt);
    static int scoreBoundedScope(const QString &prompt);
    static int scoreClarity(const QString &prompt);
    static int scoreStructure(const QString &prompt);

    static Assessment::Grade gradeFromScore(int score);
    static QStringList generateSuggestions(int fileScore, int acceptScore, int scopeScore, int clarityScore, int structureScore);
    static int estimateYoloLevel(const Assessment &a);
    static int estimateDuration(const Assessment &a);
    static double estimateCost(const Assessment &a);

private:
    PromptQualityGate() = default;
};

} // namespace Konsolai

#endif // PROMPTQUALITYGATE_H
