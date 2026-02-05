/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSIONSTATE_H
#define CLAUDESESSIONSTATE_H

#include "konsoleprivate_export.h"

#include <QString>
#include <QDateTime>
#include <QJsonObject>

namespace Konsolai
{

/**
 * ClaudeSessionState represents the persistent state of a Claude session.
 *
 * This is used to save/restore session state across Konsolai restarts.
 * The state includes:
 * - Session identification (name, ID, profile)
 * - Timestamps (created, last accessed)
 * - Working directory
 * - Claude model
 * - Attachment status (whether Konsolai is currently attached)
 */
class KONSOLEPRIVATE_EXPORT ClaudeSessionState
{
public:
    ClaudeSessionState() = default;
    ~ClaudeSessionState() = default;

    // Session identification
    QString sessionName;        // Full tmux session name (e.g., "konsolai-default-a1b2c3d4")
    QString sessionId;          // Unique session ID (8 hex chars)
    QString profileName;        // Konsole profile used

    // Timestamps
    QDateTime created;          // When session was created
    QDateTime lastAccessed;     // When session was last accessed

    // Session properties
    QString workingDirectory;   // Initial working directory
    QString claudeModel;        // Claude model name (empty for default)
    QString taskDescription; // From wizard Task Description field

    // Status
    bool isAttached = false;    // Whether Konsolai is currently attached

    // Per-session settings
    QString autoContinuePrompt; // empty = use global default
    bool yoloMode = false;
    bool doubleYoloMode = false;
    bool tripleYoloMode = false;

    /**
     * Check if this is a valid session state
     */
    bool isValid() const {
        return !sessionName.isEmpty() && !sessionId.isEmpty();
    }

    /**
     * Serialize to JSON
     */
    QJsonObject toJson() const;

    /**
     * Deserialize from JSON
     */
    static ClaudeSessionState fromJson(const QJsonObject &obj);

    /**
     * Compare by session name
     */
    bool operator==(const ClaudeSessionState &other) const {
        return sessionName == other.sessionName;
    }
};

} // namespace Konsolai

#endif // CLAUDESESSIONSTATE_H
