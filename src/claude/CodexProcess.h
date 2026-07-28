/*
    SPDX-FileCopyrightText: 2026 Struktured Labs

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CODEXPROCESS_H
#define CODEXPROCESS_H

#include "konsoleprivate_export.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace Konsolai
{

/**
 * A recorded Codex session, parsed from a rollout transcript.
 *
 * Codex writes one JSONL per session under
 * ~/.codex/sessions/YYYY/MM/DD/rollout-<timestamp>-<uuid>.jsonl, whose first
 * line is a "session_meta" record carrying the id, cwd, and start time. That
 * is the Codex analogue of ClaudeConversation.
 */
struct KONSOLEPRIVATE_EXPORT CodexConversation {
    QString sessionId; // UUID — the SESSION_ID accepted by `codex resume`
    QString workingDirectory; // cwd recorded at session start
    QString firstPrompt;
    QString model;
    QString transcriptPath;
    QDateTime created;
    QDateTime modified;
};

/**
 * Command construction and session discovery for the Codex CLI.
 *
 * Mirrors ClaudeProcess: pure static helpers with no process ownership, so
 * the session layer can build a command string and hand it to tmux exactly as
 * it does for Claude. Codex differs from Claude in two ways that matter here:
 *
 *  - Resume is a subcommand (`codex resume <uuid>`), not a flag.
 *  - Transcripts live in one date-partitioned tree keyed by cwd, rather than
 *    per-project directories with hashed names.
 */
class KONSOLEPRIVATE_EXPORT CodexProcess
{
public:
    /** True when a codex binary can be located. */
    static bool isAvailable();

    /**
     * Absolute path to the codex binary, or empty when not found.
     *
     * Codex is normally installed via npm, which under nvm puts it in a
     * version-specific directory that is not on a non-interactive PATH — so
     * the nvm node dirs are searched explicitly after PATH.
     */
    static QString executablePath();

    /**
     * The binary token to place in a launch command.
     *
     * This is executablePath() when codex resolves, and a bare "codex"
     * otherwise. tmux execs the command without a login shell, so an
     * npm/nvm-installed codex is not on PATH and a bare name dies instantly
     * with "command not found" — the resolved absolute path is required.
     */
    static QString launchBinary();

    /**
     * Build the codex command line.
     *
     * With an empty resumeSessionId this starts a fresh session; otherwise it
     * emits `codex resume <uuid>`. workingDir is passed via -C so the command
     * is correct even when the caller does not set the process cwd.
     */
    static QString buildCommand(const QString &workingDir = QString(),
                                const QString &resumeSessionId = QString(),
                                const QString &model = QString(),
                                const QStringList &additionalArgs = QStringList());

    /** Root of the Codex transcript tree (~/.codex/sessions). */
    static QString sessionsRoot();

    /**
     * Parse a rollout JSONL's session_meta line.
     *
     * Returns a conversation with an empty sessionId when the file is missing,
     * empty, or does not start with a session_meta record.
     */
    static CodexConversation parseSessionMeta(const QString &jsonlPath);

    /**
     * All recorded sessions, newest first.
     *
     * When workingDir is non-empty only sessions recorded in that directory
     * are returned, which is what the session tree needs to group Codex
     * sessions by project alongside Claude ones.
     */
    static QList<CodexConversation> discoverConversations(const QString &workingDir = QString());
};

}

#endif // CODEXPROCESS_H
