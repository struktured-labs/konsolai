/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSIONREGISTRY_H
#define CLAUDESESSIONREGISTRY_H

#include "konsoleprivate_export.h"

#include "ClaudeSessionState.h"
#include "TmuxManager.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

namespace Konsolai
{

class ClaudeSession;

/**
 * A Claude CLI conversation entry from sessions-index.json
 */
struct KONSOLEPRIVATE_EXPORT ClaudeConversation {
    QString sessionId; // UUID
    QString summary;
    QString firstPrompt;
    int messageCount = 0;
    QDateTime created;
    QDateTime modified;
};

/**
 * ClaudeSessionRegistry tracks all Claude sessions across Konsolai.
 *
 * Features:
 * - Tracks active sessions attached to Konsolai windows
 * - Detects orphaned tmux sessions (from previous Konsolai runs)
 * - Persists session state to ~/.local/share/konsolai/sessions.json
 * - Provides session list for "Claude → Reattach Session" menu
 *
 * On startup:
 * 1. Load persisted session state
 * 2. Query tmux for existing konsolai-* sessions
 * 3. Cross-reference to identify orphaned sessions
 * 4. Update menu with reattach options
 */
class KONSOLEPRIVATE_EXPORT ClaudeSessionRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ClaudeSessionRegistry(QObject *parent = nullptr);
    ~ClaudeSessionRegistry() override;

    /**
     * Get the singleton instance
     */
    static ClaudeSessionRegistry* instance();

    /**
     * Register a new Claude session
     */
    void registerSession(ClaudeSession *session);

    /**
     * Unregister a Claude session
     */
    void unregisterSession(ClaudeSession *session);

    /**
     * Mark a session as attached (Konsolai has a window connected to it)
     */
    void markAttached(const QString &sessionName);

    /**
     * Mark a session as detached (no Konsolai window connected)
     */
    void markDetached(const QString &sessionName);

    /**
     * Get all registered sessions
     */
    QList<ClaudeSession*> activeSessions() const { return m_activeSessions.values(); }

    /**
     * Get all orphaned sessions (tmux sessions without attached Konsolai)
     */
    QList<ClaudeSessionState> orphanedSessions() const;

    /**
     * Get all known session states (active + orphaned)
     */
    QList<ClaudeSessionState> allSessionStates() const;

    /**
     * Find session by name
     */
    ClaudeSession* findSession(const QString &sessionName) const;

    /**
     * Look up the last auto-continue prompt used for a working directory.
     * Returns the prompt from the most recently accessed session with that directory,
     * or empty string if none found.
     */
    QString lastAutoContinuePrompt(const QString &workingDirectory) const;

    /**
     * Update the auto-continue prompt for a specific session and persist.
     */
    void updateSessionPrompt(const QString &sessionName, const QString &prompt);

    /**
     * Look up the last session state for a working directory.
     * Returns a pointer to the most recently accessed state with that directory,
     * or nullptr if none found.
     */
    const ClaudeSessionState *lastSessionState(const QString &workingDirectory) const;

    /**
     * Look up persisted state by session name.
     * Returns nullptr if the session name is not in the registry.
     */
    const ClaudeSessionState *sessionState(const QString &sessionName) const;

    /**
     * Check if a session exists (in tmux)
     */
    bool sessionExists(const QString &sessionName) const;

    /**
     * Refresh the list of orphaned sessions from tmux
     */
    void refreshOrphanedSessions();

    /**
     * Discover Claude sessions by scanning directories for .claude footprints.
     * Finds any project that has been used with Claude (not just konsolai sessions).
     *
     * @param searchRoot Directory to scan (e.g., ~/projects)
     * @return List of session states for discovered projects
     */
    QList<ClaudeSessionState> discoverSessions(const QString &searchRoot) const;

    /**
     * Read Claude CLI conversation history for a project path.
     *
     * Reads ~/.claude/projects/{hashed-path}/sessions-index.json and returns
     * conversation entries sorted by modified date (most recent first).
     *
     * @param projectPath Absolute path to the project directory
     * @return List of conversations, empty if none found
     */
    static QList<ClaudeConversation> readClaudeConversations(const QString &projectPath);

    /**
     * Get path to sessions state file
     */
    static QString sessionStateFilePath();

    /**
     * Load session state from disk
     */
    void loadState();

    /**
     * Save session state to disk
     */
    void saveState();

Q_SIGNALS:
    /**
     * Emitted when the list of orphaned sessions changes
     */
    void orphanedSessionsChanged();

    /**
     * Emitted when a session is registered
     */
    void sessionRegistered(ClaudeSession *session);

    /**
     * Emitted when a session is unregistered
     */
    void sessionUnregistered(const QString &sessionName);

    /**
     * Emitted when new sessions are discovered from project scanning
     */
    void sessionsDiscovered(const QList<ClaudeSessionState> &sessions);

private Q_SLOTS:
    void onPeriodicRefresh();

private:
    TmuxManager *m_tmuxManager = nullptr;

    // Active sessions (attached to Konsolai windows)
    QHash<QString, ClaudeSession*> m_activeSessions;

    // All known session states (including orphaned)
    QHash<QString, ClaudeSessionState> m_sessionStates;

    // Timer for periodic orphan detection
    QTimer *m_refreshTimer = nullptr;
    static constexpr int REFRESH_INTERVAL_MS = 30000;  // 30 seconds
};

} // namespace Konsolai

#endif // CLAUDESESSIONREGISTRY_H
