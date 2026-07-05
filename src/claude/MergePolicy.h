/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_MERGEPOLICY_H
#define KONSOLAI_MERGEPOLICY_H

#include "konsoleprivate_export.h"

#include "SessionManagerPanel.h" // for SessionMetadata

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace Konsolai
{

/**
 * Per-field inheritance choices used by applyMerge(). The defaults match the
 * common case: union/max most fields, but keep the primary's budgets intact
 * because budget caps are an intentional per-session decision.
 */
struct KONSOLEPRIVATE_EXPORT MergeFieldChoices {
    bool inheritDescription = true;
    bool inheritPinned = true;
    bool inheritYolo = true; // doubleYolo follows yolo
    bool inheritLastAccessed = true;
    bool inheritResumeConversation = true;
    bool inheritApprovalLog = true;
    bool inheritApprovalCounts = true;
    bool inheritPromptRound = true;
    bool inheritBudget = false; // default OFF — budgets are intentional
};

/**
 * Compute the post-merge metadata given the primary (kept) and the others
 * (to be dismissed). Pure function — no I/O, no QObject.
 *
 * Identity fields (sessionId, sessionName, profileName, workingDirectory,
 * createdAt, isRemote, sshHost/Username/Port) are always taken from the
 * primary unchanged.
 *
 * jsonlSizeByResumeId is required when inheritResumeConversation is true:
 * pick the candidate resumeId whose .jsonl is largest. Tie or missing → keep
 * primary's. Caller can populate this via jsonlSizesForResumeIds().
 */
KONSOLEPRIVATE_EXPORT SessionMetadata applyMerge(const SessionMetadata &primary,
                                                 const QList<SessionMetadata> &others,
                                                 const MergeFieldChoices &choices,
                                                 const QHash<QString, qint64> &jsonlSizeByResumeId);

/**
 * Inspect ~/.claude/projects/<hashed-workdir>/<resumeId>.jsonl for each
 * candidate resumeId and return its on-disk size in bytes (0 if missing).
 * Empty resumeIds are skipped.
 *
 * The workingDirectory is hashed Claude-style by replacing every '/' with '-'.
 */
KONSOLEPRIVATE_EXPORT QHash<QString, qint64> jsonlSizesForResumeIds(const QString &workingDirectory, const QStringList &resumeIds);

} // namespace Konsolai

#endif // KONSOLAI_MERGEPOLICY_H
