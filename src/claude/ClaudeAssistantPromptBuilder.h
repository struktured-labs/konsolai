/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_CLAUDEASSISTANTPROMPTBUILDER_H
#define KONSOLAI_CLAUDEASSISTANTPROMPTBUILDER_H

#include "konsoleprivate_export.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace Konsolai
{

/**
 * Snapshot of the tree structure passed into the prompt builder. Kept small
 * and boring — the whole point is testability.
 */
struct KONSOLEPRIVATE_EXPORT TreeInventory {
    struct Project {
        QString workingDirectory;
        QString basename; // basename(workingDirectory)
        QString description; // may be empty
        int sessionCount = 0;
    };
    struct Category {
        QString key; // e.g. "cowir"
        QStringList projectWorkdirs; // members
    };
    QList<Project> projects;
    QList<Category> categories; // includes standalone (1-project) buckets
    QStringList userCategories; // empty user-created buckets
    QHash<QString, QString> existingAliases;
    QHash<QString, QString> existingWorkdirOverrides;
    QStringList existingSuppressedCategories;
};

/**
 * Parsed proposal returned by the LLM. Anything Claude proposes lands here;
 * the panel decides what to apply.
 */
struct KONSOLEPRIVATE_EXPORT ReorganizeProposal {
    QHash<QString, QString> categoryAliases;
    QHash<QString, QString> workdirOverrides;
    QStringList suppressedCategories;
    QStringList userCategories;
    QString rationale; // free-form; shown in the dialog

    bool isEmpty() const
    {
        return categoryAliases.isEmpty() && workdirOverrides.isEmpty() && suppressedCategories.isEmpty() && userCategories.isEmpty();
    }
};

/**
 * Compose the freeform prompt sent to `claude -p` for a "reorganize the tree"
 * request. Returns a self-contained string — no follow-up messages needed.
 * Includes the strict JSON schema Claude MUST match, so parseReorganizeResponse
 * gets a clean payload.
 */
KONSOLEPRIVATE_EXPORT QString buildReorganizePrompt(const TreeInventory &inventory, const QString &userIntent);

/**
 * Parse Claude's JSON reply. Tolerant: strips markdown fences if present,
 * accepts objects with unknown extra keys, only fails on outright non-JSON
 * or missing top-level object.
 *
 * On failure, returns empty proposal and sets `*errorOut` (if provided) to
 * a human-readable message. Caller decides whether to show it.
 */
KONSOLEPRIVATE_EXPORT ReorganizeProposal parseReorganizeResponse(const QString &responseText, QString *errorOut = nullptr);

/**
 * Compose the prompt for the "suggest a short category name for these projects"
 * feature. Response is expected as a single line — no schema, no JSON.
 */
KONSOLEPRIVATE_EXPORT QString buildSuggestNamePrompt(const QStringList &projectBasenames, const QStringList &projectDescriptions);

/**
 * Extract the suggested name from Claude's response — strip whitespace,
 * trailing punctuation, backticks, quotes. Return kebab-case-normalized string.
 */
KONSOLEPRIVATE_EXPORT QString parseSuggestNameResponse(const QString &responseText);

} // namespace Konsolai

#endif // KONSOLAI_CLAUDEASSISTANTPROMPTBUILDER_H
