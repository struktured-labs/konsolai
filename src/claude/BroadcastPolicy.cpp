/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "BroadcastPolicy.h"

#include "SessionManagerPanel.h" // for SessionMetadata

#include <QDir>

namespace Konsolai
{

namespace
{

// Canonical variable name → token type. Used to resolve {key} tokens in templates.
enum class VarKind {
    SessionName,
    SessionId,
    Project,
    Cwd,
    TmuxSession,
    Index,
    Count,
};

const QHash<QString, VarKind> &knownVars()
{
    static const QHash<QString, VarKind> table = {
        {QStringLiteral("session_name"), VarKind::SessionName},
        {QStringLiteral("session_id"), VarKind::SessionId},
        {QStringLiteral("project"), VarKind::Project},
        {QStringLiteral("cwd"), VarKind::Cwd},
        {QStringLiteral("tmux_session"), VarKind::TmuxSession},
        {QStringLiteral("index"), VarKind::Index},
        {QStringLiteral("count"), VarKind::Count},
    };
    return table;
}

QString resolve(VarKind kind, const BroadcastVars &vars)
{
    switch (kind) {
    case VarKind::SessionName:
        return vars.sessionName;
    case VarKind::SessionId:
        return vars.sessionId;
    case VarKind::Project:
        return vars.project;
    case VarKind::Cwd:
        return vars.workingDirectory;
    case VarKind::TmuxSession:
        return vars.tmuxSession;
    case VarKind::Index:
        return QString::number(vars.index);
    case VarKind::Count:
        return QString::number(vars.count);
    }
    return QString();
}

} // namespace

QString substituteTemplate(const QString &tmpl, const BroadcastVars &vars)
{
    if (tmpl.isEmpty()) {
        return tmpl;
    }

    const auto &vars_table = knownVars();
    QString out;
    out.reserve(tmpl.size());

    const int n = tmpl.size();
    int i = 0;
    while (i < n) {
        const QChar c = tmpl.at(i);
        if (c != QLatin1Char('{')) {
            out.append(c);
            ++i;
            continue;
        }

        // Find the matching close brace. Flat — no nesting.
        int j = i + 1;
        while (j < n && tmpl.at(j) != QLatin1Char('}')) {
            ++j;
        }

        if (j >= n) {
            // Unclosed `{` — leave the rest of the string literal from here.
            out.append(tmpl.mid(i));
            break;
        }

        // Token spans [i+1, j); both braces excluded.
        const QString token = tmpl.mid(i + 1, j - i - 1);
        const auto it = vars_table.constFind(token);
        if (it != vars_table.constEnd()) {
            out.append(resolve(it.value(), vars));
        } else {
            // Unknown key — leave the original `{token}` substring literal.
            out.append(tmpl.mid(i, j - i + 1));
        }
        i = j + 1;
    }

    return out;
}

BroadcastVars buildVars(const SessionMetadata &meta, const QString &displayName, const QString &categoryKey, int index, int count)
{
    BroadcastVars v;
    v.sessionId = meta.sessionId;
    if (!displayName.isEmpty()) {
        v.sessionName = displayName;
    } else if (!meta.description.trimmed().isEmpty()) {
        v.sessionName = meta.description.trimmed();
    } else if (!meta.workingDirectory.isEmpty()) {
        v.sessionName = QDir(meta.workingDirectory).dirName();
    } else {
        v.sessionName = meta.sessionName;
    }
    v.project = categoryKey;
    v.workingDirectory = meta.workingDirectory;
    v.tmuxSession = meta.sessionName;
    v.index = index;
    v.count = count;
    return v;
}

QStringList templateVariableNames()
{
    // Order matters: help menu shows them in this order.
    return {
        QStringLiteral("session_name"),
        QStringLiteral("session_id"),
        QStringLiteral("project"),
        QStringLiteral("cwd"),
        QStringLiteral("tmux_session"),
        QStringLiteral("index"),
        QStringLiteral("count"),
    };
}

} // namespace Konsolai
