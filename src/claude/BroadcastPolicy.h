/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KONSOLAI_BROADCASTPOLICY_H
#define KONSOLAI_BROADCASTPOLICY_H

#include "konsoleprivate_export.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace Konsolai
{

struct SessionMetadata; // forward — from SessionManagerPanel.h

/**
 * One recipient's substitution variables. Built per-session at broadcast time
 * by `buildVars()`; consumed by `substituteTemplate()`.
 */
struct KONSOLEPRIVATE_EXPORT BroadcastVars {
    QString sessionId;
    QString sessionName; // displayName: description if set, else basename(workingDirectory)
    QString project; // smart-category key (e.g. "cowir") — caller supplies via map
    QString workingDirectory; // full cwd path
    QString tmuxSession; // tmux session name (= meta.sessionName field)
    int index = 0; // 1-based recipient index
    int count = 0; // total checked recipients in this broadcast
};

/**
 * Substitute Python str.format–style {key} tokens in `tmpl` using `vars`.
 *
 * Recognized keys (case-sensitive):
 *   {session_name}, {session_id}, {project}, {cwd}, {tmux_session},
 *   {index}, {count}
 *
 * Unknown keys are LEFT LITERAL (no error, no replacement). Doubled braces
 * `{{`/`}}` aren't special — keep the implementation simple; if the user
 * literally wants a brace in the output, they can include one. Document this.
 */
KONSOLEPRIVATE_EXPORT QString substituteTemplate(const QString &tmpl, const BroadcastVars &vars);

/**
 * Helper to construct BroadcastVars from a SessionMetadata + the displayName
 * the tree shows + the category key. Pure function so it's testable without
 * a SessionManagerPanel instance.
 */
KONSOLEPRIVATE_EXPORT BroadcastVars buildVars(const SessionMetadata &meta, const QString &displayName, const QString &categoryKey, int index, int count);

/**
 * Return the canonical list of variable names available in the template, in
 * the order the help menu should show them. Used by the dialog's "?" button.
 */
KONSOLEPRIVATE_EXPORT QStringList templateVariableNames();

} // namespace Konsolai

#endif // KONSOLAI_BROADCASTPOLICY_H
