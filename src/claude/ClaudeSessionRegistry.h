/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDESESSIONREGISTRY_H
#define CLAUDESESSIONREGISTRY_H

#include "ClaudeSessionState.h"
#include "TmuxManager.h"

#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QTimer>

namespace Konsolai
{

class ClaudeSession;

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
class ClaudeSessionRegistry : public QObject
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
     * Check if a session exists (in tmux)
     */
    bool sessionExists(const QString &sessionName) const;

    /**
     * Refresh the list of orphaned sessions from tmux
     */
    void refreshOrphanedSessions();

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

private Q_SLOTS:
    void onPeriodicRefresh();

private:
    static ClaudeSessionRegistry *s_instance;

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
