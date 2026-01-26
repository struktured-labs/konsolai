/*
    SPDX-FileCopyrightText: 2025 Struktured Labs
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "ClaudeMenu.h"
#include "ClaudeSession.h"
#include "ClaudeSessionRegistry.h"
#include "ClaudeSessionState.h"
#include "TmuxManager.h"

#include <KLocalizedString>
#include <QActionGroup>
#include <QInputDialog>
#include <QMessageBox>

namespace Konsolai
{

ClaudeMenu::ClaudeMenu(QWidget *parent)
    : QMenu(i18n("&Claude"), parent)
    , m_registry(ClaudeSessionRegistry::instance())
{
    createActions();
    createReattachMenu();

    // Update menu before showing
    connect(this, &QMenu::aboutToShow, this, &ClaudeMenu::onAboutToShow);

    // Watch for orphaned session changes
    if (m_registry) {
        connect(m_registry, &ClaudeSessionRegistry::orphanedSessionsChanged,
                this, &ClaudeMenu::onOrphanedSessionsChanged);
    }

    updateActionStates();
}

ClaudeMenu::~ClaudeMenu() = default;

void ClaudeMenu::createActions()
{
    // Approve Permission
    m_approveAction = addAction(i18n("&Approve Permission"));
    m_approveAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    m_approveAction->setToolTip(i18n("Approve the pending permission request"));
    connect(m_approveAction, &QAction::triggered, this, &ClaudeMenu::onApprove);

    // Deny Permission
    m_denyAction = addAction(i18n("&Deny Permission"));
    m_denyAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    m_denyAction->setToolTip(i18n("Deny the pending permission request"));
    connect(m_denyAction, &QAction::triggered, this, &ClaudeMenu::onDeny);

    addSeparator();

    // Stop Claude
    m_stopAction = addAction(i18n("&Stop Claude"));
    m_stopAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    m_stopAction->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    m_stopAction->setToolTip(i18n("Stop the current Claude operation (Ctrl+C)"));
    connect(m_stopAction, &QAction::triggered, this, &ClaudeMenu::onStop);

    // Restart Claude
    m_restartAction = addAction(i18n("&Restart Claude"));
    m_restartAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    m_restartAction->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
    m_restartAction->setToolTip(i18n("Restart the Claude session"));
    connect(m_restartAction, &QAction::triggered, this, &ClaudeMenu::onRestart);

    addSeparator();

    // Yolo Mode - auto-approve all permissions
    m_yoloModeAction = addAction(i18n("&Yolo Mode (Auto-Approve)"));
    m_yoloModeAction->setCheckable(true);
    m_yoloModeAction->setChecked(m_yoloMode);
    m_yoloModeAction->setToolTip(i18n("Automatically approve all permission requests"));
    m_yoloModeAction->setIcon(QIcon::fromTheme(QStringLiteral("security-low")));
    connect(m_yoloModeAction, &QAction::toggled, this, &ClaudeMenu::onYoloModeToggled);

    // Double Yolo Mode - auto-accept completions
    m_doubleYoloModeAction = addAction(i18n("Double &Yolo Mode (Auto-Complete)"));
    m_doubleYoloModeAction->setCheckable(true);
    m_doubleYoloModeAction->setChecked(m_doubleYoloMode);
    m_doubleYoloModeAction->setToolTip(i18n("Automatically accept tab completions"));
    m_doubleYoloModeAction->setIcon(QIcon::fromTheme(QStringLiteral("security-low")));
    connect(m_doubleYoloModeAction, &QAction::toggled, this, &ClaudeMenu::onDoubleYoloModeToggled);

    // Triple Yolo Mode - auto-continue with prompt
    m_tripleYoloModeAction = addAction(i18n("&Triple Yolo Mode (Auto-Continue)"));
    m_tripleYoloModeAction->setCheckable(true);
    m_tripleYoloModeAction->setChecked(m_tripleYoloMode);
    m_tripleYoloModeAction->setToolTip(i18n("Automatically send continue prompt when Claude becomes idle"));
    m_tripleYoloModeAction->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    connect(m_tripleYoloModeAction, &QAction::toggled, this, &ClaudeMenu::onTripleYoloModeToggled);

    // Set Auto-Continue Prompt
    m_setPromptAction = addAction(i18n("Set Auto-Continue &Prompt..."));
    m_setPromptAction->setToolTip(i18n("Configure the prompt sent when Triple Yolo auto-continues"));
    connect(m_setPromptAction, &QAction::triggered, this, &ClaudeMenu::onSetAutoContinuePrompt);

    addSeparator();

    // Detach Session
    m_detachAction = addAction(i18n("De&tach Session"));
    m_detachAction->setIcon(QIcon::fromTheme(QStringLiteral("window-close")));
    m_detachAction->setToolTip(i18n("Detach from the Claude session (keeps running in background)"));
    connect(m_detachAction, &QAction::triggered, this, &ClaudeMenu::onDetach);

    // Kill Session
    m_killAction = addAction(i18n("&Kill Session"));
    m_killAction->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
    m_killAction->setToolTip(i18n("Kill the Claude session completely"));
    connect(m_killAction, &QAction::triggered, this, &ClaudeMenu::onKill);

    addSeparator();

    // Archive All Sessions
    m_archiveAllAction = addAction(i18n("&Archive All Detached Sessions"));
    m_archiveAllAction->setIcon(QIcon::fromTheme(QStringLiteral("archive-remove")));
    m_archiveAllAction->setToolTip(i18n("Kill all detached tmux sessions"));
    connect(m_archiveAllAction, &QAction::triggered, this, &ClaudeMenu::onArchiveAll);

    addSeparator();

    // Configure Hooks
    m_configureHooksAction = addAction(i18n("Configure &Hooks..."));
    m_configureHooksAction->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    m_configureHooksAction->setToolTip(i18n("Configure Claude hooks integration"));
    connect(m_configureHooksAction, &QAction::triggered, this, &ClaudeMenu::onConfigureHooks);
}

void ClaudeMenu::createReattachMenu()
{
    m_reattachMenu = new QMenu(i18n("&Reattach Session"), this);
    m_reattachMenu->setIcon(QIcon::fromTheme(QStringLiteral("document-open-recent")));
    m_reattachMenu->setToolTip(i18n("Reattach to a detached Claude session"));

    // Insert before "Configure Hooks"
    insertMenu(m_configureHooksAction, m_reattachMenu);

    updateReattachMenu();
}

void ClaudeMenu::setActiveSession(ClaudeSession *session)
{
    if (m_activeSession == session) {
        return;
    }

    // Disconnect old session
    if (m_activeSession) {
        disconnect(m_activeSession, nullptr, this, nullptr);
    }

    m_activeSession = session;

    // Connect new session signals
    if (m_activeSession) {
        connect(m_activeSession, &ClaudeSession::stateChanged,
                this, &ClaudeMenu::updateActionStates);
        connect(m_activeSession, &QObject::destroyed, this, [this]() {
            m_activeSession = nullptr;
            updateActionStates();
            syncYoloModesFromSession();
        });

        // Sync yolo modes with per-session settings
        connect(m_activeSession, &ClaudeSession::yoloModeChanged, this, [this](bool enabled) {
            if (m_yoloModeAction)
                m_yoloModeAction->setChecked(enabled);
        });
        connect(m_activeSession, &ClaudeSession::doubleYoloModeChanged, this, [this](bool enabled) {
            if (m_doubleYoloModeAction)
                m_doubleYoloModeAction->setChecked(enabled);
        });
        connect(m_activeSession, &ClaudeSession::tripleYoloModeChanged, this, [this](bool enabled) {
            if (m_tripleYoloModeAction)
                m_tripleYoloModeAction->setChecked(enabled);
        });
    }

    updateActionStates();
    syncYoloModesFromSession();
}

void ClaudeMenu::onApprove()
{
    if (m_activeSession) {
        m_activeSession->approvePermission();
    }
}

void ClaudeMenu::onDeny()
{
    if (m_activeSession) {
        m_activeSession->denyPermission();
    }
}

void ClaudeMenu::onStop()
{
    if (m_activeSession) {
        m_activeSession->stop();
    }
}

void ClaudeMenu::onRestart()
{
    if (m_activeSession) {
        m_activeSession->restart();
    }
}

void ClaudeMenu::onDetach()
{
    qDebug() << "ClaudeMenu::onDetach() - m_activeSession:" << (void *)m_activeSession;
    if (m_activeSession) {
        qDebug() << "  Session name:" << m_activeSession->sessionName();
        m_activeSession->detach();
    } else {
        qDebug() << "  No active session!";
    }
}

void ClaudeMenu::onKill()
{
    if (m_activeSession) {
        m_activeSession->kill();
    }
}

void ClaudeMenu::onAboutToShow()
{
    updateActionStates();
    updateReattachMenu();
}

void ClaudeMenu::onReattachSession()
{
    QAction *action = qobject_cast<QAction*>(sender());
    if (action) {
        QString sessionName = action->data().toString();
        Q_EMIT reattachRequested(sessionName);
    }
}

void ClaudeMenu::onConfigureHooks()
{
    Q_EMIT configureHooksRequested();
}

void ClaudeMenu::onOrphanedSessionsChanged()
{
    updateReattachMenu();
}

void ClaudeMenu::updateActionStates()
{
    bool hasSession = (m_activeSession != nullptr);

    // Permission actions available when there's an active Claude session
    // (We can't reliably detect "waiting" state without hooks, so always enable)
    m_approveAction->setEnabled(hasSession);
    m_denyAction->setEnabled(hasSession);

    // Other actions available when session exists
    m_stopAction->setEnabled(hasSession);
    m_restartAction->setEnabled(hasSession);
    m_detachAction->setEnabled(hasSession);
    m_killAction->setEnabled(hasSession);
}

void ClaudeMenu::updateReattachMenu()
{
    m_reattachMenu->clear();

    if (!m_registry) {
        m_reattachMenu->addAction(i18n("No registry available"))->setEnabled(false);
        return;
    }

    QList<ClaudeSessionState> orphans = m_registry->orphanedSessions();

    if (orphans.isEmpty()) {
        m_reattachMenu->addAction(i18n("No detached sessions"))->setEnabled(false);
        return;
    }

    // Add submenu for each orphaned session with reattach and archive options
    for (const ClaudeSessionState &state : orphans) {
        QString displayName = state.sessionName;
        if (!state.profileName.isEmpty()) {
            displayName = QStringLiteral("%1 (%2)").arg(state.profileName, state.sessionId);
        }

        QString timeAgo = state.lastAccessed.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
        QString tooltip = QStringLiteral("Last accessed: %1\nWorking directory: %2")
                              .arg(timeAgo, state.workingDirectory);

        // Create submenu for this session
        QMenu *sessionMenu = m_reattachMenu->addMenu(displayName);
        sessionMenu->setToolTip(tooltip);

        // Reattach action
        QAction *reattachAction = sessionMenu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18n("Reattach"));
        reattachAction->setData(state.sessionName);
        connect(reattachAction, &QAction::triggered, this, &ClaudeMenu::onReattachSession);

        // Archive action
        QAction *archiveAction = sessionMenu->addAction(QIcon::fromTheme(QStringLiteral("archive-remove")), i18n("Archive (Kill)"));
        archiveAction->setData(state.sessionName);
        connect(archiveAction, &QAction::triggered, this, &ClaudeMenu::onArchiveSession);
    }

    // Add separator and "Archive All" if there are multiple sessions
    if (orphans.size() > 1) {
        m_reattachMenu->addSeparator();
        QAction *archiveAllAction = m_reattachMenu->addAction(QIcon::fromTheme(QStringLiteral("archive-remove")), i18n("Archive All..."));
        connect(archiveAllAction, &QAction::triggered, this, &ClaudeMenu::onArchiveAll);
    }
}

void ClaudeMenu::onYoloModeToggled(bool checked)
{
    setYoloMode(checked);
}

void ClaudeMenu::onDoubleYoloModeToggled(bool checked)
{
    setDoubleYoloMode(checked);
}

void ClaudeMenu::setYoloMode(bool enabled)
{
    if (m_yoloMode == enabled) {
        return;
    }

    m_yoloMode = enabled;

    if (m_yoloModeAction) {
        m_yoloModeAction->setChecked(enabled);
    }

    // Update per-session setting
    if (m_activeSession) {
        m_activeSession->setYoloMode(enabled);
    }

    Q_EMIT yoloModeChanged(enabled);
}

void ClaudeMenu::setDoubleYoloMode(bool enabled)
{
    if (m_doubleYoloMode == enabled) {
        return;
    }

    m_doubleYoloMode = enabled;

    if (m_doubleYoloModeAction) {
        m_doubleYoloModeAction->setChecked(enabled);
    }

    // Update per-session setting
    if (m_activeSession) {
        m_activeSession->setDoubleYoloMode(enabled);
    }

    Q_EMIT doubleYoloModeChanged(enabled);
}

void ClaudeMenu::onTripleYoloModeToggled(bool checked)
{
    setTripleYoloMode(checked);
}

void ClaudeMenu::onSetAutoContinuePrompt()
{
    bool ok = false;
    QString prompt = QInputDialog::getMultiLineText(this,
                                                    i18n("Auto-Continue Prompt"),
                                                    i18n("Enter the prompt to send when Claude becomes idle:"),
                                                    m_autoContinuePrompt,
                                                    &ok);

    if (ok && !prompt.isEmpty()) {
        setAutoContinuePrompt(prompt);
    }
}

void ClaudeMenu::setTripleYoloMode(bool enabled)
{
    if (m_tripleYoloMode == enabled) {
        return;
    }

    m_tripleYoloMode = enabled;

    if (m_tripleYoloModeAction) {
        m_tripleYoloModeAction->setChecked(enabled);
    }

    // Update per-session setting
    if (m_activeSession) {
        m_activeSession->setTripleYoloMode(enabled);
    }

    Q_EMIT tripleYoloModeChanged(enabled);
}

void ClaudeMenu::setAutoContinuePrompt(const QString &prompt)
{
    m_autoContinuePrompt = prompt;

    // Update per-session setting
    if (m_activeSession) {
        m_activeSession->setAutoContinuePrompt(prompt);
    }
}

void ClaudeMenu::syncYoloModesFromSession()
{
    if (m_activeSession) {
        // Sync menu checkboxes with session's settings (without triggering signals)
        if (m_yoloModeAction) {
            m_yoloModeAction->blockSignals(true);
            m_yoloModeAction->setChecked(m_activeSession->yoloMode());
            m_yoloModeAction->blockSignals(false);
        }
        if (m_doubleYoloModeAction) {
            m_doubleYoloModeAction->blockSignals(true);
            m_doubleYoloModeAction->setChecked(m_activeSession->doubleYoloMode());
            m_doubleYoloModeAction->blockSignals(false);
        }
        if (m_tripleYoloModeAction) {
            m_tripleYoloModeAction->blockSignals(true);
            m_tripleYoloModeAction->setChecked(m_activeSession->tripleYoloMode());
            m_tripleYoloModeAction->blockSignals(false);
        }
        m_autoContinuePrompt = m_activeSession->autoContinuePrompt();
    } else {
        // No session - show global defaults
        if (m_yoloModeAction) {
            m_yoloModeAction->blockSignals(true);
            m_yoloModeAction->setChecked(m_yoloMode);
            m_yoloModeAction->blockSignals(false);
        }
        if (m_doubleYoloModeAction) {
            m_doubleYoloModeAction->blockSignals(true);
            m_doubleYoloModeAction->setChecked(m_doubleYoloMode);
            m_doubleYoloModeAction->blockSignals(false);
        }
        if (m_tripleYoloModeAction) {
            m_tripleYoloModeAction->blockSignals(true);
            m_tripleYoloModeAction->setChecked(m_tripleYoloMode);
            m_tripleYoloModeAction->blockSignals(false);
        }
    }
}

void ClaudeMenu::onArchiveAll()
{
    if (!m_registry) {
        return;
    }

    QList<ClaudeSessionState> orphans = m_registry->orphanedSessions();
    if (orphans.isEmpty()) {
        return;
    }

    // Confirm with user
    int count = orphans.size();
    QMessageBox::StandardButton reply = QMessageBox::question(this,
                                                              i18n("Archive All Sessions"),
                                                              i18n("This will permanently kill %1 detached tmux session(s).\n\nAre you sure?", count),
                                                              QMessageBox::Yes | QMessageBox::No,
                                                              QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    // Kill all orphaned sessions
    TmuxManager tmux;
    for (const ClaudeSessionState &state : orphans) {
        tmux.killSession(state.sessionName);
    }

    // Refresh the registry
    m_registry->refreshOrphanedSessions();
}

void ClaudeMenu::onArchiveSession()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action) {
        return;
    }

    QString sessionName = action->data().toString();
    if (sessionName.isEmpty()) {
        return;
    }

    // Kill the tmux session
    TmuxManager tmux;
    tmux.killSession(sessionName);

    // Refresh the registry
    if (m_registry) {
        m_registry->refreshOrphanedSessions();
    }
}

} // namespace Konsolai

#include "moc_ClaudeMenu.cpp"
