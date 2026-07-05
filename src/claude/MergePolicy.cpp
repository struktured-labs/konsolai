/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "MergePolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QPair>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace Konsolai
{

namespace
{

// Pick the longest non-empty trimmed string from {primary, others...}; ties
// keep the earlier (primary first). Returns primary's value if all are empty.
QString chooseLongestDescription(const SessionMetadata &primary, const QList<SessionMetadata> &others)
{
    QString best = primary.description;
    int bestLen = primary.description.trimmed().length();
    for (const auto &other : others) {
        const int len = other.description.trimmed().length();
        if (len > bestLen) {
            best = other.description;
            bestLen = len;
        }
    }
    return best;
}

QDateTime maxDateTime(const QDateTime &a, const QDateTime &b)
{
    if (!a.isValid()) {
        return b;
    }
    if (!b.isValid()) {
        return a;
    }
    return a > b ? a : b;
}

QString claudeProjectDirHash(const QString &workingDirectory)
{
    // Claude stores conversation .jsonl files under
    //   ~/.claude/projects/<workdir-with-slashes-replaced-by-dashes>/<uuid>.jsonl
    QString hashed = workingDirectory;
    hashed.replace(QLatin1Char('/'), QLatin1Char('-'));
    return hashed;
}

} // namespace

SessionMetadata applyMerge(const SessionMetadata &primary,
                           const QList<SessionMetadata> &others,
                           const MergeFieldChoices &choices,
                           const QHash<QString, qint64> &jsonlSizeByResumeId)
{
    SessionMetadata result = primary;

    if (others.isEmpty()) {
        return result;
    }

    // --- Description: longest wins ---
    if (choices.inheritDescription) {
        result.description = chooseLongestDescription(primary, others);
    }

    // --- Pinned / yolo: OR across all sources ---
    if (choices.inheritPinned) {
        bool pinned = primary.isPinned;
        for (const auto &o : others) {
            pinned = pinned || o.isPinned;
        }
        result.isPinned = pinned;
    }

    if (choices.inheritYolo) {
        bool yolo = primary.yoloMode;
        bool doubleYolo = primary.doubleYoloMode;
        for (const auto &o : others) {
            yolo = yolo || o.yoloMode;
            doubleYolo = doubleYolo || o.doubleYoloMode;
        }
        result.yoloMode = yolo;
        result.doubleYoloMode = doubleYolo;
    }

    // --- lastAccessed: max ---
    if (choices.inheritLastAccessed) {
        QDateTime latest = primary.lastAccessed;
        for (const auto &o : others) {
            latest = maxDateTime(latest, o.lastAccessed);
        }
        result.lastAccessed = latest;
    }

    // --- Resume conversation: pick the candidate with the largest .jsonl ---
    if (choices.inheritResumeConversation) {
        QString chosen = primary.lastResumeSessionId;
        qint64 chosenSize = jsonlSizeByResumeId.value(primary.lastResumeSessionId, 0);
        for (const auto &o : others) {
            if (o.lastResumeSessionId.isEmpty()) {
                continue;
            }
            const qint64 sz = jsonlSizeByResumeId.value(o.lastResumeSessionId, 0);
            if (sz > chosenSize) {
                chosen = o.lastResumeSessionId;
                chosenSize = sz;
            }
        }
        result.lastResumeSessionId = chosen;
    }

    // --- Approval log: concatenate, sort by timestamp, dedupe by (ts, action) ---
    if (choices.inheritApprovalLog) {
        QVector<ApprovalLogEntry> combined = primary.approvalLog;
        for (const auto &o : others) {
            combined.append(o.approvalLog);
        }
        std::sort(combined.begin(), combined.end(), [](const ApprovalLogEntry &a, const ApprovalLogEntry &b) {
            return a.timestamp < b.timestamp;
        });
        QVector<ApprovalLogEntry> deduped;
        deduped.reserve(combined.size());
        QSet<QPair<qint64, QString>> seen;
        for (const auto &entry : combined) {
            const qint64 ms = entry.timestamp.isValid() ? entry.timestamp.toMSecsSinceEpoch() : 0;
            const QPair<qint64, QString> key{ms, entry.action};
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            deduped.append(entry);
        }
        result.approvalLog = deduped;
    }

    // --- Approval counts: sum ---
    if (choices.inheritApprovalCounts) {
        int yoloCount = primary.yoloApprovalCount;
        int doubleCount = primary.doubleYoloApprovalCount;
        for (const auto &o : others) {
            yoloCount += o.yoloApprovalCount;
            doubleCount += o.doubleYoloApprovalCount;
        }
        result.yoloApprovalCount = yoloCount;
        result.doubleYoloApprovalCount = doubleCount;
    }

    // --- Prompt round: max ---
    if (choices.inheritPromptRound) {
        int round = primary.currentPromptRound;
        for (const auto &o : others) {
            if (o.currentPromptRound > round) {
                round = o.currentPromptRound;
            }
        }
        result.currentPromptRound = round;
    }

    // --- Budget: max-cap across sources, or leave primary alone ---
    if (choices.inheritBudget) {
        int timeLimit = primary.budgetTimeLimitMinutes;
        double costCeiling = primary.budgetCostCeilingUSD;
        quint64 tokenCeiling = primary.budgetTokenCeiling;
        for (const auto &o : others) {
            if (o.budgetTimeLimitMinutes > timeLimit) {
                timeLimit = o.budgetTimeLimitMinutes;
            }
            if (o.budgetCostCeilingUSD > costCeiling) {
                costCeiling = o.budgetCostCeilingUSD;
            }
            if (o.budgetTokenCeiling > tokenCeiling) {
                tokenCeiling = o.budgetTokenCeiling;
            }
        }
        result.budgetTimeLimitMinutes = timeLimit;
        result.budgetCostCeilingUSD = costCeiling;
        result.budgetTokenCeiling = tokenCeiling;
    }

    return result;
}

QHash<QString, qint64> jsonlSizesForResumeIds(const QString &workingDirectory, const QStringList &resumeIds)
{
    QHash<QString, qint64> sizes;
    if (workingDirectory.isEmpty()) {
        return sizes;
    }
    const QString hashed = claudeProjectDirHash(workingDirectory);
    const QString baseDir = QDir::homePath() + QStringLiteral("/.claude/projects/") + hashed;
    for (const QString &resumeId : resumeIds) {
        if (resumeId.isEmpty()) {
            continue;
        }
        const QString path = baseDir + QLatin1Char('/') + resumeId + QStringLiteral(".jsonl");
        QFileInfo fi(path);
        sizes.insert(resumeId, fi.exists() ? fi.size() : 0);
    }
    return sizes;
}

} // namespace Konsolai
