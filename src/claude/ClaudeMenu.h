/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef CLAUDEMENU_H
#define CLAUDEMENU_H

#include <QMenu>
#include <QAction>

namespace Konsolai
{

class ClaudeSession;
class ClaudeSessionRegistry;

/**
 * ClaudeMenu provides the "Claude" menu for the menu bar.
 *
 * Menu structure:
 * - Approve Permission (Ctrl+Shift+A)
 * - Deny Permission (Ctrl+Shift+D)
 * - Stop Claude (Ctrl+Shift+S)
 * - Restart Claude (Ctrl+Shift+R)
 * - ---
 * - Detach Session
 * - Kill Session
 * - ---
 * - Reattach Session >
 *   - [List of orphaned sessions]
 * - ---
 * - Configure Hooks...
 */
class ClaudeMenu : public QMenu
{
    Q_OBJECT

public:
    explicit ClaudeMenu(QWidget *parent = nullptr);
    ~ClaudeMenu() override;

    /**
     * Set the currently active Claude session
     */
    void setActiveSession(ClaudeSession *session);

    /**
     * Get actions for adding to other menus/toolbars
     */
    QAction* approveAction() const { return m_approveAction; }
    QAction* denyAction() const { return m_denyAction; }
    QAction* stopAction() const { return m_stopAction; }
    QAction* restartAction() const { return m_restartAction; }
    QAction* detachAction() const { return m_detachAction; }
    QAction* killAction() const { return m_killAction; }

Q_SIGNALS:
    /**
     * Emitted when user wants to reattach to a session
     */
    void reattachRequested(const QString &sessionName);

    /**
     * Emitted when user wants to configure hooks
     */
    void configureHooksRequested();

private Q_SLOTS:
    void onApprove();
    void onDeny();
    void onStop();
    void onRestart();
    void onDetach();
    void onKill();
    void onAboutToShow();
    void onReattachSession();
    void onConfigureHooks();
    void onOrphanedSessionsChanged();
    void updateActionStates();

private:
    void createActions();
    void createReattachMenu();
    void updateReattachMenu();

    ClaudeSession *m_activeSession = nullptr;
    ClaudeSessionRegistry *m_registry = nullptr;

    // Actions
    QAction *m_approveAction = nullptr;
    QAction *m_denyAction = nullptr;
    QAction *m_stopAction = nullptr;
    QAction *m_restartAction = nullptr;
    QAction *m_detachAction = nullptr;
    QAction *m_killAction = nullptr;
    QAction *m_configureHooksAction = nullptr;

    // Reattach submenu
    QMenu *m_reattachMenu = nullptr;
};

} // namespace Konsolai

#endif // CLAUDEMENU_H
