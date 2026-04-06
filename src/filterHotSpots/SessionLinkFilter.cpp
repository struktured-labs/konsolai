/*
    SPDX-FileCopyrightText: 2026 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "SessionLinkFilter.h"
#include "SessionLinkHotSpot.h"

#include "claude/ClaudeSession.h"
#include "claude/ClaudeSessionRegistry.h"

#include <QDir>

using namespace Konsole;

// Match @session-name patterns:
// - Preceded by start-of-line or whitespace (negative lookbehind excludes email-style word@)
// - @ followed by 2-61 word/dot/dash characters (session names like konsolai-abc12345)
// - Minimum 2 chars after @ to avoid false positives on single-char matches
const QRegularExpression SessionLinkFilter::SessionLinkRegExp(QStringLiteral("(?<=^|(?<=\\s))@([\\w][\\w.\\-]{1,60})"));

SessionLinkFilter::SessionLinkFilter()
{
    setRegExp(SessionLinkRegExp);
}

/**
 * Fuzzy match a captured name against active Claude sessions.
 *
 * Returns the session name if a match is found, empty string otherwise.
 * Match priority:
 *   1. Exact match on directory basename (case-insensitive)
 *   2. Exact match on sessionId
 *   3. Exact match on sessionName
 *   4. Case-insensitive substring match on taskDescription
 *   5. Case-insensitive prefix match on directory basename
 */
static QString matchSessionName(const QString &name)
{
    auto *registry = Konsolai::ClaudeSessionRegistry::instance();
    if (registry == nullptr) {
        return {};
    }

    const auto sessions = registry->activeSessions();
    if (sessions.isEmpty()) {
        return {};
    }

    // Pass 1: Exact match on directory basename (case-insensitive)
    for (auto *session : sessions) {
        const QString dirName = QDir(session->workingDirectory()).dirName();
        if (dirName.compare(name, Qt::CaseInsensitive) == 0) {
            return session->sessionName();
        }
    }

    // Pass 2: Exact match on sessionId
    for (auto *session : sessions) {
        if (session->sessionId() == name) {
            return session->sessionName();
        }
    }

    // Pass 3: Exact match on sessionName
    for (auto *session : sessions) {
        if (session->sessionName().compare(name, Qt::CaseInsensitive) == 0) {
            return session->sessionName();
        }
    }

    // Pass 4: Case-insensitive substring match on taskDescription
    for (auto *session : sessions) {
        const QString desc = session->taskDescription();
        if (!desc.isEmpty() && desc.contains(name, Qt::CaseInsensitive)) {
            return session->sessionName();
        }
    }

    // Pass 5: Case-insensitive prefix match on directory basename
    for (auto *session : sessions) {
        const QString dirName = QDir(session->workingDirectory()).dirName();
        if (dirName.startsWith(name, Qt::CaseInsensitive)) {
            return session->sessionName();
        }
    }

    return {};
}

QSharedPointer<HotSpot> SessionLinkFilter::newHotSpot(int startLine, int startColumn, int endLine, int endColumn, const QStringList &capturedTexts)
{
    if (capturedTexts.size() < 2) {
        return {};
    }

    // capturedTexts[0] is the full match (e.g., "@project-name")
    // capturedTexts[1] is the capture group (e.g., "project-name")
    const QString name = capturedTexts[1];

    const QString matchedName = matchSessionName(name);
    if (matchedName.isEmpty()) {
        return {};
    }

    return QSharedPointer<HotSpot>(new SessionLinkHotSpot(startLine, startColumn, endLine, endColumn, capturedTexts, matchedName));
}
