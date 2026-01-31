/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SESSIONMANAGERPANEL_H
#define SESSIONMANAGERPANEL_H

#include "konsoleprivate_export.h"

#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace Konsolai
{

class ClaudeSession;
class ClaudeSessionRegistry;

/**
 * Session metadata stored persistently
 */
struct KONSOLEPRIVATE_EXPORT SessionMetadata {
    QString sessionId;
    QString sessionName;
    QString profileName;
    QString workingDirectory;
    bool isPinned = false;
    bool isArchived = false;
    bool isExpired = false;
    QDateTime lastAccessed;
    QDateTime createdAt;
};

/**
 * SessionManagerPanel provides a collapsible sidebar for managing all Claude sessions.
 *
 * Features:
 * - Shows all sessions: pinned, active, and archived
 * - Pin sessions to keep them at the top
 * - Archive sessions (kills tmux but preserves metadata for later restoration)
 * - Only active sessions appear as tabs
 * - Double-click to attach/open a session
 */
class KONSOLEPRIVATE_EXPORT SessionManagerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SessionManagerPanel(QWidget *parent = nullptr);
    ~SessionManagerPanel() override;

    /**
     * Register an active session
     */
    void registerSession(ClaudeSession *session);

    /**
     * Unregister a session (when tab is closed)
     */
    void unregisterSession(ClaudeSession *session);

    /**
     * Get all session metadata
     */
    QList<SessionMetadata> allSessions() const;

    /**
     * Get pinned sessions
     */
    QList<SessionMetadata> pinnedSessions() const;

    /**
     * Get archived sessions
     */
    QList<SessionMetadata> archivedSessions() const;

    /**
     * Check if panel is collapsed
     */
    bool isCollapsed() const
    {
        return m_collapsed;
    }

public Q_SLOTS:
    /**
     * Toggle panel collapsed state
     */
    void setCollapsed(bool collapsed);

    /**
     * Refresh the session list
     */
    void refresh();

    /**
     * Pin a session
     */
    void pinSession(const QString &sessionId);

    /**
     * Unpin a session
     */
    void unpinSession(const QString &sessionId);

    /**
     * Archive a session (kills tmux, preserves metadata)
     */
    void archiveSession(const QString &sessionId);

    /**
     * Unarchive a session (restarts Claude with same session ID)
     */
    void unarchiveSession(const QString &sessionId);

    /**
     * Mark a session as expired (dead tmux backend) and auto-archive it
     */
    void markExpired(const QString &sessionName);

Q_SIGNALS:
    /**
     * Emitted when user wants to attach to a session
     */
    void attachRequested(const QString &sessionName);

    /**
     * Emitted when user wants to create a new session
     */
    void newSessionRequested();

    /**
     * Emitted when user wants to unarchive and start a session
     */
    void unarchiveRequested(const QString &sessionId, const QString &workingDirectory);

    /**
     * Emitted when collapsed state changes
     */
    void collapsedChanged(bool collapsed);

private Q_SLOTS:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onContextMenu(const QPoint &pos);
    void onNewSessionClicked();

private:
    void setupUi();
    void loadMetadata();
    void saveMetadata();
    void updateTreeWidget();
    void addSessionToTree(const SessionMetadata &meta, QTreeWidgetItem *parent);
    void showApprovalLog(ClaudeSession *session);
    SessionMetadata *findMetadata(const QString &sessionId);
    QTreeWidgetItem *findTreeItem(const QString &sessionId);

    QTreeWidget *m_treeWidget = nullptr;
    QPushButton *m_newSessionButton = nullptr;
    QPushButton *m_collapseButton = nullptr;
    QTreeWidgetItem *m_pinnedCategory = nullptr;
    QTreeWidgetItem *m_activeCategory = nullptr;
    QTreeWidgetItem *m_closedCategory = nullptr;
    QTreeWidgetItem *m_archivedCategory = nullptr;
    QTreeWidgetItem *m_discoveredCategory = nullptr;

    QMap<QString, SessionMetadata> m_metadata;
    QMap<QString, ClaudeSession *> m_activeSessions;
    ClaudeSessionRegistry *m_registry = nullptr;
    bool m_collapsed = false;
};

} // namespace Konsolai

#endif // SESSIONMANAGERPANEL_H
